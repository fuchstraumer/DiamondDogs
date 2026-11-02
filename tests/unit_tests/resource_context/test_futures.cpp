#include "ImageDataFormats.hpp"
#include "ResourceContext.hpp"
#include "ResourceContextFixture.hpp"
#include "ResourceFlags.hpp"
#include "ResourceFuture.hpp"
#include "ResourceTypes.hpp"
#include <chrono>
#include <cstdint>
#include <gtest/gtest.h>
#include <iostream>
#include <span>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    // slot 0 is the shared full-table slot, so a table of 4 holds 3 live futures
    constexpr int32_t k_SmallTable = 4;
    constexpr int32_t k_SmallTableLive = k_SmallTable - 1;

    BufferCreateInfo DeviceLocalBuffer(size_t size)
    {
        BufferCreateInfo info{};
        info.Domain = ResourceDomain::GPUOnly;
        info.Usage = BufferUsageBits::ComputeShaderUAV | BufferUsageBits::CopySource | BufferUsageBits::CopyDestination;
        info.Size = size;
        return info;
    }

    BufferCreateInfo HostVisibleBuffer(size_t size)
    {
        BufferCreateInfo info{};
        info.Domain = ResourceDomain::CPUToGPU;
        info.Usage = BufferUsageBits::ComputeShaderUBO;
        info.Size = size;
        return info;
    }

    ImageCreateInfo SmallTexture()
    {
        ImageCreateInfo info{};
        info.Domain = ResourceDomain::GPUOnly;
        info.Usage = ImageUsageBits::SampledImage | ImageUsageBits::SrvComputeShader;
        info.Type = ImageType::Type2D;
        info.Format = ImageFormat{ ImageComponentFormats::RGBA8, ImageDataType::UNorm };
        info.Width = 4u;
        info.Height = 4u;
        info.Depth = 1u;
        return info;
    }

    /** Yields until the context's live future count is `expected`. Returns false after `timeout`. */
    [[nodiscard]] bool WaitForLiveFutures(const ResourceContext& context, int64_t expected, std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (context.Counters().FuturesLive != expected)
        {
            if (std::chrono::steady_clock::now() > deadline)
            {
                return false;
            }

            std::this_thread::yield();
        }

        return true;
    }
}

TEST_F(ResourceContextTest, DefaultAndMovedFromFuturesAreInvalid)
{
    const rhi::OperationFuture empty;
    EXPECT_EQ(empty.State(), rhi::FutureState::Invalid);
    EXPECT_EQ(empty.Error(), rhi::ResourceError::Invalid);
    EXPECT_EQ(empty.Completion().Value, 0u);
    EXPECT_FALSE(empty.IsDone());

    rhi::ResourceFuture<GraphicsResource> first = context->CreateSampler(SamplerCreateInfo{});
    ASSERT_EQ(context->Wait(first, k_TimeoutNs), rhi::FutureState::Complete);
    const int64_t liveBefore = context->Counters().FuturesLive;

    // a move hands the slot over, and does not take a second one
    rhi::ResourceFuture<GraphicsResource> second = std::move(first);
    EXPECT_EQ(first.State(), rhi::FutureState::Invalid);
    EXPECT_EQ(second.State(), rhi::FutureState::Complete);
    EXPECT_EQ(context->Counters().FuturesLive, liveBefore);

    const GraphicsResource sampler = second.Get();
    second = rhi::ResourceFuture<GraphicsResource>{};
    EXPECT_EQ(context->Counters().FuturesLive, liveBefore - 1);
    DestroyAndWait(sampler);
}

TEST_F(ResourceContextTest, FullTableFailsAtOnceAndReusesReleasedSlots)
{
    RecreateContext(k_SmallTable);

    std::vector<rhi::ResourceFuture<GraphicsResource>> held;
    held.reserve(k_SmallTableLive);

    for (int32_t index = 0; index < k_SmallTableLive; ++index)
    {
        held.push_back(context->CreateSampler(SamplerCreateInfo{}));
        ASSERT_EQ(context->Wait(held.back(), k_TimeoutNs), rhi::FutureState::Complete);
    }

    EXPECT_EQ(context->Counters().FuturesLive, k_SmallTableLive);

    // a full table fails the call before Wait, and sends no message
    rhi::ResourceFuture<GraphicsResource> overflow = context->CreateSampler(SamplerCreateInfo{});
    EXPECT_EQ(overflow.State(), rhi::FutureState::Failed);
    EXPECT_EQ(overflow.Error(), rhi::ResourceError::FutureTableFull);
    EXPECT_TRUE(overflow.IsDone());
    EXPECT_EQ(overflow.Completion().Value, 0u);
    EXPECT_EQ(context->Wait(overflow, 0u), rhi::FutureState::Failed);
    EXPECT_EQ(context->Counters().FutureTableFull, 1);
    EXPECT_EQ(context->Counters().FuturesLive, k_SmallTableLive);

    // the full-table slot is shared, so a second overflow gets the same answer
    rhi::OperationFuture secondOverflow = context->FillBuffer(GraphicsResource::Null(), 0u, 0u, 4u);
    EXPECT_EQ(secondOverflow.Error(), rhi::ResourceError::FutureTableFull);
    EXPECT_EQ(context->Counters().FutureTableFull, 2);

    const GraphicsResource released = held.back().Get();
    held.pop_back();
    EXPECT_EQ(context->Counters().FuturesLive, k_SmallTableLive - 1);

    // the released slot is the only free one, so this call must get it
    rhi::ResourceFuture<GraphicsResource> reused = context->CreateSampler(SamplerCreateInfo{});
    ASSERT_EQ(context->Wait(reused, k_TimeoutNs), rhi::FutureState::Complete);
    EXPECT_EQ(reused.Error(), rhi::ResourceError::Success);
    EXPECT_NE(reused.Get().VkSamplerHandle, 0u);
    EXPECT_EQ(context->Counters().FutureTableFull, 2);

    // the worker reads one queue in order, so it is past both overflow calls now. a message sent for either would
    // have written the shared slot: the sampler to Complete, the fill of a null buffer to StaleResource
    EXPECT_EQ(overflow.State(), rhi::FutureState::Failed);
    EXPECT_EQ(overflow.Error(), rhi::ResourceError::FutureTableFull);
    EXPECT_EQ(secondOverflow.Error(), rhi::ResourceError::FutureTableFull);

    // releasing a future of the full-table slot frees nothing
    overflow = rhi::ResourceFuture<GraphicsResource>{};
    secondOverflow = rhi::OperationFuture{};
    EXPECT_EQ(context->Counters().FuturesLive, k_SmallTableLive);

    std::vector<GraphicsResource> samplers{ released, reused.Get() };
    for (const rhi::ResourceFuture<GraphicsResource>& future : held)
    {
        samplers.push_back(future.Get());
    }

    held.clear();
    reused = rhi::ResourceFuture<GraphicsResource>{};

    for (const GraphicsResource& sampler : samplers)
    {
        DestroyAndWait(sampler);
    }

    EXPECT_EQ(context->Counters().FuturesLive, 0);
}

TEST_F(ResourceContextTest, FutureDroppedDuringTransferFreesItsSlot)
{
    // the owner releases each future while its upload may still be on the GPU. the transfer thread then frees the
    // slot when it publishes. the table is small, so a slot that never came back fails a later call with
    // FutureTableFull. it is 8, not 4, because ReadBackWords holds 4 futures at once
    RecreateContext(8);
    constexpr uint32_t k_Words = 16384u;
    constexpr int32_t k_Iterations = 64;

    const GraphicsResource buffer = CreateBufferAndWait(DeviceLocalBuffer(k_Words * sizeof(uint32_t)), {});
    ASSERT_TRUE(static_cast<bool>(buffer));

    std::vector<uint32_t> words;
    int32_t droppedInFlight = 0;

    for (int32_t iteration = 0; iteration < k_Iterations; ++iteration)
    {
        words = MakeWordPattern(k_Words, static_cast<uint32_t>(iteration));
        const RhiBufferResourceData data{ std::as_bytes(std::span{ words }), 0u };

        {
            rhi::OperationFuture upload = context->SetBufferData(buffer, std::span{ &data, 1u });
            ASSERT_NE(upload.Error(), rhi::ResourceError::FutureTableFull) << "iteration " << iteration;

            if (!upload.IsDone())
            {
                ++droppedInFlight;
            }
        }

        ASSERT_TRUE(WaitForLiveFutures(*context, 0, std::chrono::seconds(5))) << "iteration " << iteration;
    }

    // the context copies the data at the call, so the last pattern is what the buffer holds
    EXPECT_EQ(ReadBackWords(buffer, k_Words * sizeof(uint32_t)), words);
    EXPECT_GT(droppedInFlight, 0) << "no future was still in flight when dropped, so the handoff was not tested";
    std::cout << "[          ] " << droppedInFlight << " of " << k_Iterations << " futures were dropped in flight\n";

    DestroyAndWait(buffer);
    EXPECT_EQ(context->Counters().FutureTableFull, 0);
}

TEST_F(ResourceContextTest, CompletionCarriesTheTransferTicket)
{
    const std::vector<uint32_t> words = MakeWordPattern(256u, 3u);
    const RhiBufferResourceData data{ std::as_bytes(std::span{ words }), 0u };

    rhi::ResourceFuture<GraphicsResource> deviceLocal = context->CreateBuffer(DeviceLocalBuffer(data.Data.size()), std::span{ &data, 1u });
    ASSERT_EQ(context->Wait(deviceLocal, k_TimeoutNs), rhi::FutureState::Complete);
    const rhi::GpuWait upload = deviceLocal.Completion();
    EXPECT_NE(upload.Semaphore.Get(), 0u);
    EXPECT_GT(upload.Value, 0u);
    EXPECT_TRUE(upload.Submitted);

    rhi::ResourceFuture<GraphicsResource> hostVisible = context->CreateBuffer(HostVisibleBuffer(data.Data.size()), std::span{ &data, 1u });
    ASSERT_EQ(context->Wait(hostVisible, k_TimeoutNs), rhi::FutureState::Complete);
    EXPECT_EQ(hostVisible.Completion().Value, 0u);

    DestroyAndWait(deviceLocal.Get());
    DestroyAndWait(hostVisible.Get());
}

TEST_F(ResourceContextTest, EveryFailurePathReportsItsError)
{
    const GraphicsResource deviceLocal = CreateBufferAndWait(DeviceLocalBuffer(256u), {});
    const GraphicsResource small = CreateBufferAndWait(DeviceLocalBuffer(64u), {});

    BufferCreateInfo noTransferInfo{};
    noTransferInfo.Domain = ResourceDomain::GPUOnly;
    noTransferInfo.Usage = BufferUsageBits::ComputeShaderUAV;
    noTransferInfo.Size = 256u;
    const GraphicsResource noTransfer = CreateBufferAndWait(noTransferInfo, {});

    rhi::ResourceFuture<GraphicsResource> imageFuture = context->CreateImage(SmallTexture(), {});
    ASSERT_EQ(context->Wait(imageFuture, k_TimeoutNs), rhi::FutureState::Complete);
    const GraphicsResource image = imageFuture.Get();

    rhi::ResourceFuture<GraphicsResource> samplerFuture = context->CreateSampler(SamplerCreateInfo{});
    ASSERT_EQ(context->Wait(samplerFuture, k_TimeoutNs), rhi::FutureState::Complete);
    const GraphicsResource sampler = samplerFuture.Get();

    const GraphicsResource destroyed = CreateBufferAndWait(DeviceLocalBuffer(64u), {});
    DestroyAndWait(destroyed);

    BufferCreateInfo zeroSize = DeviceLocalBuffer(0u);
    BufferCreateInfo addressUsage = DeviceLocalBuffer(64u);
    addressUsage.Usage |= BufferUsageBits::DeviceAddressBuffer;

    struct Case
    {
        const char* Name;
        rhi::OperationFuture Future;
        rhi::ResourceError Expected;
    };

    std::vector<Case> cases;
    cases.push_back({ "zero-size buffer", context->CreateBuffer(zeroSize, {}), rhi::ResourceError::InvalidCreateInfo });
    cases.push_back({ "device address without the feature", context->CreateBuffer(addressUsage, {}), rhi::ResourceError::UnsupportedUsage });
    cases.push_back({ "fill of a destroyed buffer", context->FillBuffer(destroyed, 0u, 0u, 64u), rhi::ResourceError::StaleResource });
    cases.push_back({ "map of an image", context->MapBuffer(image, 64u, 0u), rhi::ResourceError::WrongResourceKind });
    cases.push_back({ "fill without CopyDestination", context->FillBuffer(noTransfer, 0u, 0u, 64u), rhi::ResourceError::MissingTransferUsage });
    cases.push_back({ "fill at offset 2", context->FillBuffer(deviceLocal, 0u, 2u, 64u), rhi::ResourceError::InvalidFillRange });
    cases.push_back({ "copy into a smaller buffer", context->CopyBufferContents(deviceLocal, small), rhi::ResourceError::CopySizeMismatch });
    cases.push_back({ "map of device-local memory", context->MapBuffer(deviceLocal, 64u, 0u), rhi::ResourceError::NotHostVisible });
    cases.push_back({ "copy of a sampler", context->CopyBuffer(sampler, false), rhi::ResourceError::UnsupportedCopy });
    cases.push_back({ "register a null timeline", context->RegisterConsumerTimeline(rhi::TimelineSemaphoreHandle{}), rhi::ResourceError::InvalidTimeline });

    for (const Case& entry : cases)
    {
        EXPECT_EQ(context->Wait(entry.Future, k_TimeoutNs), rhi::FutureState::Failed) << entry.Name;
        EXPECT_EQ(entry.Future.Error(), entry.Expected)
            << entry.Name << ": got " << rhi::ToString(entry.Future.Error()) << ", expected " << rhi::ToString(entry.Expected);
    }

    cases.clear();
    imageFuture = rhi::ResourceFuture<GraphicsResource>{};
    samplerFuture = rhi::ResourceFuture<GraphicsResource>{};

    for (const GraphicsResource& resource : { deviceLocal, small, noTransfer, image, sampler })
    {
        DestroyAndWait(resource);
    }
}
