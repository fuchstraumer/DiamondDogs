#include <gtest/gtest.h>
#include "Device.hpp"
#include "Fence.hpp"
#include "GpuTimeline.hpp"
#include "Queue.hpp"
#include "RhiFlags.hpp"
#include "RhiResult.hpp"
#include "RhiSystem.hpp"
#include "RhiTypes.hpp"
#include "async/Scheduler.hpp"
#include "async/Task.hpp"
#include "async/Timeline.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <thread>

namespace
{
    constexpr uint64_t k_TimeoutNs = 5'000'000'000ull;
    constexpr int64_t k_MaxTicks = 100'000'000;

    foundation::Task<void> AwaitMirrorThenRecord(const foundation::Timeline& mirror, uint64_t target_value,
                                                 bool& finished);
}

// A real queue submit signals the semaphore, and a coroutine resumes through the Scheduler tick that polls it
class GpuTimelineTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        rhi::RhiSystemCreateInfo createInfo{};
        createInfo.ApplicationName = "GpuTimelineTest";
        createInfo.EngineName = "DiamondDogsTestEngine";
        createInfo.VkVersion = rhi::ApiVersion::Vulkan13;
        createInfo.ValidationLevel = rhi::ValidationLayers::WithSynchronization;
        createInfo.RequiredInstanceExtensions = { "VK_EXT_debug_utils" };
        createInfo.ShaderCacheDir = (std::filesystem::temp_directory_path() / "DiamondDogsUnitTesting" / "ShaderCache").string();
        rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
        device = rhiSystem->GetDevice();
        ASSERT_NE(device, nullptr);
        queue = device->GetGeneralQueue();
    }

    void TearDown() override
    {
        if (device != nullptr)
        {
            device->WaitDeviceIdle();
        }

        rhiSystem.reset();
    }

    std::unique_ptr<rhi::RhiSystem> rhiSystem;
    rhi::Device* device{ nullptr };
    rhi::QueueHandle queue{};
};

TEST_F(GpuTimelineTest, CoroutineResumesAfterTheGpuSignalsTheSemaphore)
{
    rhi::GpuTimeline timeline(device->Handle());
    foundation::Scheduler scheduler;
    scheduler.AddSource(timeline);
    bool finished = false;

    const uint64_t signalValue = timeline.NextSignalValue();
    EXPECT_EQ(signalValue, 1u);
    scheduler.Spawn(AwaitMirrorThenRecord(timeline.Mirror(), signalValue, finished));

    // nothing is submitted yet, so no number of ticks can advance the mirror
    for (int32_t tick = 0; tick < 5; ++tick)
    {
        scheduler.Tick();
    }

    EXPECT_EQ(timeline.Mirror().Value(), 0u);
    EXPECT_EQ(scheduler.ParkedCount(), 1);
    EXPECT_FALSE(finished);

    rhi::Fence fence(device->Handle());
    const rhi::SemaphoreSubmitInfo signal{ timeline.Semaphore(), signalValue, rhi::PipelineStage::AllCommands };
    const rhi::SubmitInfo submit{ {}, {}, std::span{ &signal, 1 } };
    ASSERT_TRUE(rhi::QueueSubmit(queue, submit, fence.Handle()).IsSuccess());

    while (!scheduler.Idle() && scheduler.TickCount() < k_MaxTicks)
    {
        scheduler.Tick();
        std::this_thread::yield();
    }

    ASSERT_TRUE(scheduler.Idle()) << scheduler.DumpState();
    EXPECT_TRUE(finished);
    EXPECT_EQ(timeline.Mirror().Value(), signalValue);

    ASSERT_TRUE(fence.Wait(k_TimeoutNs));
    scheduler.RemoveSource(timeline);
}

TEST_F(GpuTimelineTest, SignalValuesAreReservedInOrderAndEachWaiterResumesAtItsOwnValue)
{
    rhi::GpuTimeline timeline(device->Handle());
    foundation::Scheduler scheduler;
    scheduler.AddSource(timeline);

    const uint64_t first = timeline.NextSignalValue();
    const uint64_t second = timeline.NextSignalValue();
    EXPECT_EQ(first, 1u);
    EXPECT_EQ(second, 2u);

    bool firstFinished = false;
    bool secondFinished = false;
    scheduler.Spawn(AwaitMirrorThenRecord(timeline.Mirror(), first, firstFinished));
    scheduler.Spawn(AwaitMirrorThenRecord(timeline.Mirror(), second, secondFinished));
    scheduler.Tick();
    EXPECT_EQ(scheduler.ParkedCount(), 2);

    // the first submit signals only the first value
    rhi::Fence firstFence(device->Handle());
    const rhi::SemaphoreSubmitInfo signalFirst{ timeline.Semaphore(), first, rhi::PipelineStage::AllCommands };
    const rhi::SubmitInfo submitFirst{ {}, {}, std::span{ &signalFirst, 1 } };
    ASSERT_TRUE(rhi::QueueSubmit(queue, submitFirst, firstFence.Handle()).IsSuccess());
    ASSERT_TRUE(firstFence.Wait(k_TimeoutNs));

    // the semaphore counter is 1 now, so one tick resumes the first waiter and leaves the second parked
    scheduler.Tick();
    EXPECT_TRUE(firstFinished);
    EXPECT_FALSE(secondFinished);
    EXPECT_EQ(scheduler.ParkedCount(), 1);

    rhi::Fence secondFence(device->Handle());
    const rhi::SemaphoreSubmitInfo signalSecond{ timeline.Semaphore(), second, rhi::PipelineStage::AllCommands };
    const rhi::SubmitInfo submitSecond{ {}, {}, std::span{ &signalSecond, 1 } };
    ASSERT_TRUE(rhi::QueueSubmit(queue, submitSecond, secondFence.Handle()).IsSuccess());
    ASSERT_TRUE(secondFence.Wait(k_TimeoutNs));

    scheduler.Tick();
    EXPECT_TRUE(secondFinished);
    EXPECT_TRUE(scheduler.Idle());
    scheduler.RemoveSource(timeline);
}

namespace
{
    foundation::Task<void> AwaitMirrorThenRecord(const foundation::Timeline& mirror, uint64_t target_value,
                                                 bool& finished)
    {
        co_await mirror.Until(target_value);
        finished = true;
    }
}
