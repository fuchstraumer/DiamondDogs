#include <gtest/gtest.h>
#include "CommandBuffer.hpp"
#include "CommandPool.hpp"
#include "Device.hpp"
#include "Fence.hpp"
#include "GpuTimers.hpp"
#include "QueryPool.hpp"
#include "Queue.hpp"
#include "RhiFlags.hpp"
#include "RhiResult.hpp"
#include "RhiSystem.hpp"
#include "RhiTypes.hpp"
#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace
{
    constexpr uint64_t k_TimeoutNs = 5'000'000'000ull;
    // large enough that a fill or copy takes measurable time
    constexpr VkDeviceSize k_BufferBytes = 16ull * 1024ull * 1024ull;

    struct TestBuffer
    {
        VkBuffer Buffer{ VK_NULL_HANDLE };
        VkDeviceMemory Memory{ VK_NULL_HANDLE };
        [[nodiscard]] rhi::BufferHandle Handle() const noexcept { return rhi::BufferHandle{ reinterpret_cast<uint64_t>(Buffer) }; }
    };
}

class GpuTimersTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        rhi::RhiSystemCreateInfo createInfo{};
        createInfo.ApplicationName = "GpuTimersTest";
        createInfo.EngineName = "DiamondDogsTestEngine";
        createInfo.VkVersion = rhi::ApiVersion::Vulkan13;
        createInfo.ValidationLevel = rhi::ValidationLayers::WithSynchronization;
        createInfo.RequiredInstanceExtensions = { "VK_EXT_debug_utils" };
        createInfo.ShaderCacheDir = (std::filesystem::temp_directory_path() / "DiamondDogsUnitTesting" / "ShaderCache").string();
        rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
        device = rhiSystem->GetDevice();
        ASSERT_NE(device, nullptr);
        vkDevice = device->Handle().As<VkDevice>();
        graphicsFamily = device->GetQueueFamilyIndices().Graphics;
        graphicsPool = std::make_unique<rhi::CommandPool>(device->Handle(), rhi::CommandPool::Type::Graphics, graphicsFamily);
        ASSERT_TRUE(graphicsPool->AllocateCommandBuffers(2).IsSuccess());
    }

    void TearDown() override
    {
        if (device != nullptr)
        {
            device->WaitDeviceIdle();
        }

        for (const TestBuffer& buffer : buffers)
        {
            vkDestroyBuffer(vkDevice, buffer.Buffer, nullptr);
            vkFreeMemory(vkDevice, buffer.Memory, nullptr);
        }

        transferPool.reset();
        graphicsPool.reset();
        rhiSystem.reset();
    }

    const TestBuffer& createBuffer(VkBufferUsageFlags usage)
    {
        TestBuffer& buffer = buffers.emplace_back();
        const VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, k_BufferBytes, usage, VK_SHARING_MODE_EXCLUSIVE, 0, nullptr };
        EXPECT_EQ(vkCreateBuffer(vkDevice, &bufferInfo, nullptr, &buffer.Buffer), VK_SUCCESS);
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(vkDevice, buffer.Buffer, &requirements);
        const VkMemoryAllocateInfo allocInfo
        {
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            nullptr,
            requirements.size,
            device->GetMemoryTypeIndex(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
        };
        EXPECT_EQ(vkAllocateMemory(vkDevice, &allocInfo, nullptr, &buffer.Memory), VK_SUCCESS);
        EXPECT_EQ(vkBindBufferMemory(vkDevice, buffer.Buffer, buffer.Memory, 0), VK_SUCCESS);
        return buffer;
    }

    void submitAndWait(rhi::QueueHandle queue, const rhi::CommandBuffer& cmd)
    {
        rhi::Fence fence(device->Handle());
        const rhi::CommandBufferHandle handle = cmd.Handle();
        ASSERT_TRUE(rhi::QueueSubmit(queue, rhi::SubmitInfo{ {}, std::span{ &handle, 1 }, {} }, fence.Handle()).IsSuccess());
        ASSERT_TRUE(fence.Wait(k_TimeoutNs));
    }

    std::unique_ptr<rhi::RhiSystem> rhiSystem;
    rhi::Device* device{ nullptr };
    VkDevice vkDevice{ VK_NULL_HANDLE };
    uint32_t graphicsFamily{ ~0u };
    std::unique_ptr<rhi::CommandPool> graphicsPool;
    std::unique_ptr<rhi::CommandPool> transferPool;
    std::deque<TestBuffer> buffers;
};

// The query pool layer is in every configuration: two timestamps around a fill, read without waiting
TEST_F(GpuTimersTest, QueryPoolTimestampsAroundWork)
{
    const TestBuffer& target = createBuffer(VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    rhi::QueryPool pool(device->Handle(), rhi::QueryType::Timestamp, 2u);
    std::array<uint64_t, 2> ticks{};

    // nothing was written yet: the read reports that instead of blocking
    EXPECT_FALSE(pool.TryGetResults(0u, 2u, ticks));

    rhi::CommandBuffer cmd(graphicsPool->GetCommandBuffer(0));
    ASSERT_TRUE(cmd.Begin().IsSuccess());
    cmd.WriteTimestamp(rhi::PipelineStage::TopOfPipe, pool.Handle(), 0u);
    cmd.FillBuffer(target.Handle(), 0, rhi::k_WholeSize, 0x12345678u);
    cmd.WriteTimestamp(rhi::PipelineStage::AllCommands, pool.Handle(), 1u);
    ASSERT_TRUE(cmd.End().IsSuccess());
    submitAndWait(device->GetGeneralQueue(), cmd);

    ASSERT_TRUE(pool.TryGetResults(0u, 2u, ticks));
    EXPECT_GE(ticks[1], ticks[0]);

    // a host reset makes the queries unavailable again
    pool.Reset(0u, 2u);
    EXPECT_FALSE(pool.TryGetResults(0u, 2u, ticks));
}

#if RHI_GPU_TIMERS_ENABLED
TEST_F(GpuTimersTest, NestedScopesDepthsAndDroppedScopes)
{
    const TestBuffer& target = createBuffer(VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    rhi::GpuTimers timers(*device, rhi::GpuTimersCreateInfo{ graphicsFamily, 2u, 2u });
    ASSERT_TRUE(timers.Active()) << timers.InactiveReason();

    rhi::CommandBuffer cmd(graphicsPool->GetCommandBuffer(0));
    ASSERT_TRUE(cmd.Begin().IsSuccess());

    {
        rhi::GpuTimerBlock block(timers, cmd, 7u);
        ASSERT_TRUE(block.Active());
        rhi::ScopedGpuTimer outer(block, "outer");
        cmd.FillBuffer(target.Handle(), 0, rhi::k_WholeSize, 1u);

        {
            rhi::ScopedGpuTimer inner(block, "inner");
            const rhi::GlobalBarrier fillToFill{ rhi::PipelineStage::Clear, rhi::Access::TransferWrite, rhi::PipelineStage::Clear, rhi::Access::TransferWrite };
            cmd.Barrier(rhi::DependencyInfo{ std::span{ &fillToFill, 1 }, {}, {} });
            cmd.FillBuffer(target.Handle(), 0, rhi::k_WholeSize, 2u);
        }

        // MaxScopesPerBlock is 2, so this one records nothing and is counted
        rhi::ScopedGpuTimer dropped(block, "dropped");
    }

    ASSERT_TRUE(cmd.End().IsSuccess());

    // not submitted yet: Collect finds nothing ready, and does not wait
    timers.Collect();
    EXPECT_FALSE(timers.Latest().has_value());

    submitAndWait(device->GetGeneralQueue(), cmd);
    timers.Collect();
    const std::optional<rhi::GpuTimingBlock> latest = timers.Latest();
    ASSERT_TRUE(latest.has_value());
    EXPECT_EQ(latest->Tag, 7u);
    EXPECT_EQ(latest->DroppedScopes, 1u);
    ASSERT_EQ(latest->Timings.size(), 2u);

    const rhi::GpuTiming& outerTiming = latest->Timings[0];
    const rhi::GpuTiming& innerTiming = latest->Timings[1];
    EXPECT_EQ(outerTiming.Hash, rhi::HashGpuTimerName("outer"));
    EXPECT_EQ(innerTiming.Hash, rhi::HashGpuTimerName("inner"));
    EXPECT_EQ(outerTiming.Depth, 0u);
    EXPECT_EQ(innerTiming.Depth, 1u);
    EXPECT_GT(innerTiming.Nanoseconds, 0.0);
    EXPECT_GE(outerTiming.Nanoseconds, innerTiming.Nanoseconds);
    EXPECT_EQ(timers.Name(outerTiming.Hash), "outer");
    EXPECT_EQ(timers.Name(rhi::HashGpuTimerName("never opened")), "");

    ASSERT_TRUE(timers.Find("inner").has_value());
    EXPECT_DOUBLE_EQ(*timers.Find("inner"), innerTiming.Nanoseconds);
    EXPECT_FALSE(timers.Find("dropped").has_value());

    // busy time is the depth 0 scopes only, and taking it clears it
    EXPECT_DOUBLE_EQ(timers.TakeBusyNanoseconds(), outerTiming.Nanoseconds);
    EXPECT_DOUBLE_EQ(timers.TakeBusyNanoseconds(), 0.0);
    EXPECT_EQ(timers.TakeCompleted().size(), 1u);
    EXPECT_TRUE(timers.TakeCompleted().empty());
}

TEST_F(GpuTimersTest, BlocksRunOutWhilePendingAndComeBackAfterCollect)
{
    const TestBuffer& target = createBuffer(VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    rhi::GpuTimers timers(*device, rhi::GpuTimersCreateInfo{ graphicsFamily, 1u, 4u });
    ASSERT_TRUE(timers.Active()) << timers.InactiveReason();

    rhi::CommandBuffer first(graphicsPool->GetCommandBuffer(0));
    rhi::CommandBuffer second(graphicsPool->GetCommandBuffer(1));
    ASSERT_TRUE(first.Begin().IsSuccess());

    {
        rhi::GpuTimerBlock block(timers, first, 1u);
        ASSERT_TRUE(block.Active());
        rhi::ScopedGpuTimer scope(block, "first");
        first.FillBuffer(target.Handle(), 0, rhi::k_WholeSize, 1u);
    }

    ASSERT_TRUE(first.End().IsSuccess());

    // the only block is pending until its results are collected
    ASSERT_TRUE(second.Begin().IsSuccess());

    {
        rhi::GpuTimerBlock block(timers, second, 2u);
        EXPECT_FALSE(block.Active());
        rhi::ScopedGpuTimer scope(block, "second");
    }

    ASSERT_TRUE(second.End().IsSuccess());
    EXPECT_EQ(timers.DroppedBlocks(), 1u);

    submitAndWait(device->GetGeneralQueue(), first);
    timers.Collect();
    ASSERT_TRUE(timers.Latest().has_value());
    EXPECT_EQ(timers.Latest()->Tag, 1u);

    // collected, so the block is free again
    ASSERT_TRUE(graphicsPool->Reset().IsSuccess());
    rhi::CommandBuffer again(graphicsPool->GetCommandBuffer(0));
    ASSERT_TRUE(again.Begin().IsSuccess());

    {
        rhi::GpuTimerBlock block(timers, again, 3u);
        EXPECT_TRUE(block.Active());
        rhi::ScopedGpuTimer scope(block, "again");
        again.FillBuffer(target.Handle(), 0, rhi::k_WholeSize, 3u);
    }

    ASSERT_TRUE(again.End().IsSuccess());
    submitAndWait(device->GetGeneralQueue(), again);
    timers.Collect();
    EXPECT_EQ(timers.Latest()->Tag, 3u);
}

// The transfer queue cannot reset a query pool in a command buffer, so this proves the host reset path too
TEST_F(GpuTimersTest, TransferQueueBusyTime)
{
    if (device->GetTransferQueueCount() == 0u)
    {
        GTEST_SKIP() << "no transfer queue";
    }

    const uint32_t transferFamily = device->GetQueueFamilyIndices().Transfer;
    transferPool = std::make_unique<rhi::CommandPool>(device->Handle(), rhi::CommandPool::Type::Transfer, transferFamily);
    ASSERT_TRUE(transferPool->AllocateCommandBuffers(1).IsSuccess());
    const TestBuffer& source = createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    const TestBuffer& destination = createBuffer(VK_BUFFER_USAGE_TRANSFER_DST_BIT);

    rhi::GpuTimers timers(*device, rhi::GpuTimersCreateInfo{ transferFamily, 2u, 4u });
    ASSERT_TRUE(timers.Active()) << timers.InactiveReason();

    rhi::CommandBuffer cmd(transferPool->GetCommandBuffer(0));
    ASSERT_TRUE(cmd.Begin().IsSuccess());

    {
        rhi::GpuTimerBlock block(timers, cmd, 0u);
        rhi::ScopedGpuTimer upload(block, "transfer batch");
        const rhi::BufferCopy region{ 0u, 0u, k_BufferBytes };
        cmd.CopyBuffer(source.Handle(), destination.Handle(), std::span{ &region, 1 });
    }

    ASSERT_TRUE(cmd.End().IsSuccess());
    submitAndWait(device->GetTransferQueue(0), cmd);
    timers.Collect();
    EXPECT_GT(timers.TakeBusyNanoseconds(), 0.0);
}
#else
TEST_F(GpuTimersTest, CompiledOutOfRelease)
{
    rhi::GpuTimers timers(*device, rhi::GpuTimersCreateInfo{ graphicsFamily, 2u, 2u });
    EXPECT_FALSE(timers.Active());

    rhi::CommandBuffer cmd(graphicsPool->GetCommandBuffer(0));
    ASSERT_TRUE(cmd.Begin().IsSuccess());

    {
        rhi::GpuTimerBlock block(timers, cmd, 0u);
        EXPECT_FALSE(block.Active());
        rhi::ScopedGpuTimer scope(block, "nothing");
    }

    ASSERT_TRUE(cmd.End().IsSuccess());
    timers.Collect();
    EXPECT_FALSE(timers.Latest().has_value());
}
#endif
