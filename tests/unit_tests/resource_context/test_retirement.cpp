#include "Device.hpp"
#include "ResourceContext.hpp"
#include "ResourceContextFixture.hpp"
#include "ResourceFlags.hpp"
#include "ResourceMessageReply.hpp"
#include "ResourceTypes.hpp"
#include "Semaphore.hpp"
#include <cstdint>
#include <gtest/gtest.h>
#include <iostream>
#include <memory>
#include <span>
#include <vector>

namespace
{
    // long enough that a retirement that should not happen would have happened, short enough to keep the suite fast
    constexpr uint64_t k_ShortWaitNs = 100'000'000ull;

    BufferCreateInfo DeviceLocalStorageBuffer(size_t size)
    {
        BufferCreateInfo info{};
        info.Domain = ResourceDomain::GPUOnly;
        info.Usage = BufferUsageBits::ComputeShaderUAV | BufferUsageBits::CopySource | BufferUsageBits::CopyDestination;
        info.Size = size;
        return info;
    }
}

TEST_F(ResourceContextTest, DestroyWaitsForTheConsumerTimeline)
{
    rhi::TimelineSemaphore timeline(rhiSystem->GetDevice()->Handle());
    ASSERT_EQ(context->RegisterConsumerTimeline(timeline.Handle())->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Completed);

    // the owner is recording the frame that signals 5
    const ConsumerTimelineValue pending{ timeline.Handle(), 5u };
    context->Tick(std::span{ &pending, 1u });

    const GraphicsResource buffer = CreateBufferAndWait(DeviceLocalStorageBuffer(256u), {});
    std::shared_ptr<MessageReply> destroy = context->DestroyResource(buffer);
    EXPECT_EQ(destroy->WaitForCompletion(k_ShortWaitNs), MessageReply::Status::Timeout);

    // a retiring resource is refused at once. this reply also orders the counter read after the destroy
    EXPECT_EQ(context->FillBuffer(buffer, 0u, 0u, 256u)->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Failed);
    EXPECT_EQ(context->Counters().RetirementsPending, 1);

    // a tick before the GPU passes the value frees nothing
    context->Tick(std::span{ &pending, 1u });
    EXPECT_EQ(destroy->WaitForCompletion(k_ShortWaitNs), MessageReply::Status::Timeout);

    ASSERT_TRUE(timeline.SetValue(5u).IsSuccess());
    context->Tick(std::span{ &pending, 1u });
    EXPECT_EQ(destroy->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Completed);
    EXPECT_EQ(context->Counters().RetirementsPending, 0);

    EXPECT_EQ(context->UnregisterConsumerTimeline(timeline.Handle())->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Completed);
}

TEST_F(ResourceContextTest, UnregisteringATimelineReleasesItsWaits)
{
    rhi::TimelineSemaphore timeline(rhiSystem->GetDevice()->Handle());
    ASSERT_EQ(context->RegisterConsumerTimeline(timeline.Handle())->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Completed);
    const ConsumerTimelineValue pending{ timeline.Handle(), 3u };
    context->Tick(std::span{ &pending, 1u });

    const GraphicsResource buffer = CreateBufferAndWait(DeviceLocalStorageBuffer(256u), {});
    std::shared_ptr<MessageReply> destroy = context->DestroyResource(buffer);
    EXPECT_EQ(destroy->WaitForCompletion(k_ShortWaitNs), MessageReply::Status::Timeout);

    // the owner waited for its frames, then unregisters: nothing waits on this timeline after that
    ASSERT_TRUE(timeline.SetValue(3u).IsSuccess());
    EXPECT_EQ(context->UnregisterConsumerTimeline(timeline.Handle())->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Completed);
    EXPECT_EQ(destroy->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Completed);
}

TEST_F(ResourceContextTest, RegistrationRefusesDuplicatesAndOverflow)
{
    std::vector<std::unique_ptr<rhi::TimelineSemaphore>> timelines;
    for (int32_t i = 0; i < k_MaxConsumerTimelines + 1; ++i)
    {
        timelines.push_back(std::make_unique<rhi::TimelineSemaphore>(rhiSystem->GetDevice()->Handle()));
    }

    for (int32_t i = 0; i < k_MaxConsumerTimelines; ++i)
    {
        EXPECT_EQ(context->RegisterConsumerTimeline(timelines[i]->Handle())->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Completed);
    }

    EXPECT_EQ(context->RegisterConsumerTimeline(timelines[0]->Handle())->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Failed);
    EXPECT_EQ(context->RegisterConsumerTimeline(timelines.back()->Handle())->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Failed);

    for (int32_t i = 0; i < k_MaxConsumerTimelines; ++i)
    {
        EXPECT_EQ(context->UnregisterConsumerTimeline(timelines[i]->Handle())->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Completed);
    }

    EXPECT_EQ(context->UnregisterConsumerTimeline(timelines[0]->Handle())->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Failed);
}

TEST_F(ResourceContextTest, ManyTransfersCompleteFromFewCounterReads)
{
    constexpr int32_t k_FillCount = 256;
    constexpr size_t k_BufferBytes = 4096u;
    const GraphicsResource buffer = CreateBufferAndWait(DeviceLocalStorageBuffer(k_BufferBytes), {});
    const ResourceContextCounters before = context->Counters();

    std::vector<std::shared_ptr<ResourceTransferReply>> replies;
    replies.reserve(k_FillCount);
    for (int32_t i = 0; i < k_FillCount; ++i)
    {
        replies.push_back(context->FillBuffer(buffer, static_cast<uint32_t>(i), 0u, k_BufferBytes));
    }

    for (const std::shared_ptr<ResourceTransferReply>& reply : replies)
    {
        ASSERT_EQ(reply->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Completed);
    }

    const ResourceContextCounters after = context->Counters();
    const int64_t transfers = after.TransfersRecorded - before.TransfersRecorded;
    const int64_t batches = after.TransferBatchesSubmitted - before.TransferBatchesSubmitted;
    const int64_t reads = after.TransferCompletionReads - before.TransferCompletionReads;
    std::cout << "fills: " << transfers << ", batches: " << batches << ", completion reads: " << reads << "\n";

    // completion is one counter read per transfer loop, never one per reply
    EXPECT_EQ(transfers, k_FillCount);
    EXPECT_LT(reads, transfers);

    // the ordering barrier between transfers means the last fill wins in every word
    const std::vector<uint32_t> words = ReadBackWords(buffer, k_BufferBytes);
    for (const uint32_t word : words)
    {
        ASSERT_EQ(word, static_cast<uint32_t>(k_FillCount - 1));
    }
}

TEST_F(ResourceContextTest, DestroyWaitsForQueuedUploads)
{
    // several large uploads, then destroys pushed right behind them. a destroy that freed a buffer while its upload was
    // queued or in flight would show as a validation error, which fails the run
    constexpr size_t k_UploadBytes = 4u * 1024u * 1024u;
    const std::vector<uint32_t> words = MakeWordPattern(static_cast<uint32_t>(k_UploadBytes / sizeof(uint32_t)), 0x5EEDu);
    const RhiBufferResourceData data{ std::as_bytes(std::span{ words }), 0u };

    std::vector<std::shared_ptr<MessageReply>> destroys;
    for (int32_t i = 0; i < 8; ++i)
    {
        const GraphicsResource buffer = CreateBufferAndWait(DeviceLocalStorageBuffer(k_UploadBytes), {});
        std::shared_ptr<ResourceTransferReply> upload = context->SetBufferData(buffer, std::span{ &data, 1u });
        destroys.push_back(context->DestroyResource(buffer));

        // the destroy completes only after the upload's ticket passed
        ASSERT_EQ(destroys.back()->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Completed);
        EXPECT_EQ(upload->GetStatus(), MessageReply::Status::Completed);
    }
}

TEST_F(ResourceContextTest, ShutdownFreesQueuedRetirements)
{
    rhi::TimelineSemaphore timeline(rhiSystem->GetDevice()->Handle());
    ASSERT_EQ(context->RegisterConsumerTimeline(timeline.Handle())->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Completed);
    const ConsumerTimelineValue pending{ timeline.Handle(), 100u };
    context->Tick(std::span{ &pending, 1u });

    const std::vector<uint32_t> words = MakeWordPattern(1024u, 0xD1Eu);
    const RhiBufferResourceData data{ std::as_bytes(std::span{ words }), 0u };

    std::vector<GraphicsResource> buffers;
    buffers.push_back(CreateBufferAndWait(DeviceLocalStorageBuffer(4096u), {}));
    buffers.push_back(CreateBufferAndWait(DeviceLocalStorageBuffer(4096u), std::span{ &data, 1u }));
    buffers.push_back(CreateBufferAndWait(DeviceLocalStorageBuffer(4096u), {}));

    std::vector<std::shared_ptr<MessageReply>> destroys;
    for (const GraphicsResource& buffer : buffers)
    {
        destroys.push_back(context->DestroyResource(buffer));
    }

    // orders the counter read after the destroys
    EXPECT_EQ(context->FillBuffer(buffers[0], 0u, 0u, 4096u)->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Failed);
    EXPECT_EQ(context->Counters().RetirementsPending, 3);

    // the owner waits for its frames before it destroys the context. the context must free the queue, not leak it
    ASSERT_TRUE(timeline.SetValue(100u).IsSuccess());
    context.reset();

    for (const std::shared_ptr<MessageReply>& destroy : destroys)
    {
        EXPECT_EQ(destroy->GetStatus(), MessageReply::Status::Completed);
    }
}
