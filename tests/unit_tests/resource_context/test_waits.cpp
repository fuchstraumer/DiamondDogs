#include "Device.hpp"
#include "ResourceContext.hpp"
#include "ResourceContextFixture.hpp"
#include "ResourceFlags.hpp"
#include "ResourceFuture.hpp"
#include "ResourceTypes.hpp"
#include "Semaphore.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <gtest/gtest.h>
#include <span>
#include <thread>
#include <vector>

namespace
{
    // long enough that a transfer that should not be held would have finished, short enough to keep the suite fast
    constexpr uint64_t k_HoldWindowNs = 100'000'000ull;
    // the longest a transfer that must not be held may take
    constexpr uint64_t k_MustFinishNs = 2'000'000'000ull;
    // the guard waits this long for a tracked future before it gives up
    constexpr uint64_t k_GuardWaitNs = 5'000'000'000ull;
    constexpr size_t k_Words = 256u;
    constexpr size_t k_Bytes = k_Words * sizeof(uint32_t);

    /** @brief Signals the timeline to its final value when the test ends, and then waits for the futures it tracks.
     *  A failed ASSERT returns from the test early, and a transfer that still waits on the timeline would hang the
     *  shutdown of the context. Declare the guard after the timeline and after the futures it tracks, so that it is
     *  destroyed before both. */
    class TimelineReleaseGuard
    {
    public:
        TimelineReleaseGuard(ResourceContext& _context, rhi::TimelineSemaphore& _timeline, uint64_t _final_value);
        ~TimelineReleaseGuard();
        TimelineReleaseGuard(const TimelineReleaseGuard&) = delete;
        TimelineReleaseGuard& operator=(const TimelineReleaseGuard&) = delete;

        void Track(const rhi::OperationFuture& future);

    private:
        ResourceContext& context;
        rhi::TimelineSemaphore& timeline;
        uint64_t finalValue;
        std::vector<const rhi::OperationFuture*> tracked;
    };

    [[nodiscard]] BufferCreateInfo DeviceLocalCopyBuffer(size_t size);
    [[nodiscard]] BufferCreateInfo HostVisibleBuffer(size_t size);
    void SleepForHoldWindow();
}

TEST_F(ResourceContextTest, AWaitHoldsTheTransfer)
{
    rhi::TimelineSemaphore timeline(rhiSystem->GetDevice()->Handle());
    rhi::OperationFuture copy;
    TimelineReleaseGuard guard(*context, timeline, 1u);
    guard.Track(copy);

    const std::vector<uint32_t> words = MakeWordPattern(k_Words, 51u);
    const RhiBufferResourceData data{ std::as_bytes(std::span{ words }), 0u };
    const GraphicsResource source = CreateBufferAndWait(DeviceLocalCopyBuffer(k_Bytes), std::span{ &data, 1u });
    const GraphicsResource destination = CreateBufferAndWait(DeviceLocalCopyBuffer(k_Bytes), {});

    const rhi::TransferWait wait{ timeline.Handle(), 1u };
    copy = context->CopyBufferContents(source, destination, std::span{ &wait, 1u });
    EXPECT_EQ(context->Wait(copy, k_HoldWindowNs), rhi::FutureState::Created);

    ASSERT_TRUE(timeline.SetValue(1u).IsSuccess());
    EXPECT_EQ(context->Wait(copy, k_TimeoutNs), rhi::FutureState::Complete);
    EXPECT_EQ(ReadBackWords(destination, k_Bytes), words);

    DestroyAndWait(source);
    DestroyAndWait(destination);
}

TEST_F(ResourceContextTest, EarlierTransfersAreNotHeldByAWaitingOne)
{
    rhi::TimelineSemaphore timeline(rhiSystem->GetDevice()->Handle());
    rhi::OperationFuture upload;
    rhi::OperationFuture heldCopy;
    TimelineReleaseGuard guard(*context, timeline, 1u);
    guard.Track(upload);
    guard.Track(heldCopy);

    const GraphicsResource uploaded = CreateBufferAndWait(DeviceLocalCopyBuffer(k_Bytes), {});
    const GraphicsResource source = CreateBufferAndWait(DeviceLocalCopyBuffer(k_Bytes), {});
    const GraphicsResource destination = CreateBufferAndWait(DeviceLocalCopyBuffer(k_Bytes), {});
    const std::vector<uint32_t> words = MakeWordPattern(k_Words, 52u);
    const RhiBufferResourceData data{ std::as_bytes(std::span{ words }), 0u };

    // the upload is recorded, and not yet submitted, when the waiting copy arrives. the transfer thread submits it first
    const rhi::TransferWait wait{ timeline.Handle(), 1u };
    upload = context->SetBufferData(uploaded, std::span{ &data, 1u }, {});
    heldCopy = context->CopyBufferContents(source, destination, std::span{ &wait, 1u });

    EXPECT_EQ(context->Wait(upload, k_MustFinishNs), rhi::FutureState::Complete);
    EXPECT_EQ(heldCopy.State(), rhi::FutureState::Created);

    ASSERT_TRUE(timeline.SetValue(1u).IsSuccess());
    EXPECT_EQ(context->Wait(heldCopy, k_TimeoutNs), rhi::FutureState::Complete);

    DestroyAndWait(uploaded);
    DestroyAndWait(source);
    DestroyAndWait(destination);
}

TEST_F(ResourceContextTest, LaterTransfersWaitBehindAWaitingBatch)
{
    rhi::TimelineSemaphore timeline(rhiSystem->GetDevice()->Handle());
    rhi::OperationFuture heldCopy;
    rhi::OperationFuture laterUpload;
    TimelineReleaseGuard guard(*context, timeline, 1u);
    guard.Track(heldCopy);
    guard.Track(laterUpload);

    const GraphicsResource source = CreateBufferAndWait(DeviceLocalCopyBuffer(k_Bytes), {});
    const GraphicsResource destination = CreateBufferAndWait(DeviceLocalCopyBuffer(k_Bytes), {});
    const GraphicsResource later = CreateBufferAndWait(DeviceLocalCopyBuffer(k_Bytes), {});
    const std::vector<uint32_t> words = MakeWordPattern(k_Words, 53u);
    const RhiBufferResourceData data{ std::as_bytes(std::span{ words }), 0u };

    const rhi::TransferWait wait{ timeline.Handle(), 1u };
    heldCopy = context->CopyBufferContents(source, destination, std::span{ &wait, 1u });
    laterUpload = context->SetBufferData(later, std::span{ &data, 1u }, {});

    // this documents a limit and not a feature. a semaphore signal on a queue waits for all commands submitted before
    // it, so the batch of the later upload cannot signal its ticket before the blocked batch does. the tickets stay in
    // order, which the completion code needs, and the later upload completes only after the wait is released
    EXPECT_EQ(context->Wait(laterUpload, k_HoldWindowNs), rhi::FutureState::Created);
    EXPECT_EQ(heldCopy.State(), rhi::FutureState::Created);

    ASSERT_TRUE(timeline.SetValue(1u).IsSuccess());
    EXPECT_EQ(context->Wait(heldCopy, k_TimeoutNs), rhi::FutureState::Complete);
    EXPECT_EQ(context->Wait(laterUpload, k_TimeoutNs), rhi::FutureState::Complete);
    EXPECT_EQ(ReadBackWords(later, k_Bytes), words);

    DestroyAndWait(source);
    DestroyAndWait(destination);
    DestroyAndWait(later);
}

TEST_F(ResourceContextTest, WaitErrors)
{
    rhi::TimelineSemaphore timeline(rhiSystem->GetDevice()->Handle());
    const GraphicsResource buffer = CreateBufferAndWait(DeviceLocalCopyBuffer(k_Bytes), {});
    const GraphicsResource other = CreateBufferAndWait(DeviceLocalCopyBuffer(k_Bytes), {});
    const GraphicsResource hostVisible = CreateBufferAndWait(HostVisibleBuffer(k_Bytes), {});
    const std::vector<uint32_t> words = MakeWordPattern(k_Words, 54u);
    const RhiBufferResourceData data{ std::as_bytes(std::span{ words }), 0u };

    const rhi::TransferWait validWait{ timeline.Handle(), 1u };
    const rhi::TransferWait nullSemaphore{ rhi::TimelineSemaphoreHandle{}, 1u };
    const rhi::TransferWait zeroValue{ timeline.Handle(), 0u };

    struct Case
    {
        const char* Name;
        rhi::OperationFuture Future;
        rhi::ResourceError Expected;
    };

    std::vector<Case> cases;
    cases.push_back({ "fill with a null semaphore", context->FillBuffer(buffer, 0u, 0u, 64u, std::span{ &nullSemaphore, 1u }), rhi::ResourceError::InvalidWait });
    cases.push_back({ "fill with a value of 0", context->FillBuffer(buffer, 0u, 0u, 64u, std::span{ &zeroValue, 1u }), rhi::ResourceError::InvalidWait });
    cases.push_back({ "copy with a null semaphore", context->CopyBufferContents(buffer, other, std::span{ &nullSemaphore, 1u }), rhi::ResourceError::InvalidWait });
    cases.push_back({ "upload with a value of 0", context->SetBufferData(buffer, std::span{ &data, 1u }, std::span{ &zeroValue, 1u }), rhi::ResourceError::InvalidWait });
    cases.push_back({ "host-visible write with a wait", context->SetBufferData(hostVisible, std::span{ &data, 1u }, std::span{ &validWait, 1u }), rhi::ResourceError::WaitOnHostWrite });

    for (const Case& entry : cases)
    {
        EXPECT_EQ(context->Wait(entry.Future, k_TimeoutNs), rhi::FutureState::Failed) << entry.Name;
        EXPECT_EQ(entry.Future.Error(), entry.Expected)
            << entry.Name << ": got " << rhi::ToString(entry.Future.Error()) << ", expected " << rhi::ToString(entry.Expected);
    }

    cases.clear();

    DestroyAndWait(buffer);
    DestroyAndWait(other);
    DestroyAndWait(hostVisible);
}

TEST_F(ResourceContextTest, TooManyWaitsFailOnTheCallingThreadWithNoMessage)
{
    rhi::TimelineSemaphore timeline(rhiSystem->GetDevice()->Handle());
    const GraphicsResource buffer = CreateBufferAndWait(DeviceLocalCopyBuffer(k_Bytes), {});
    const rhi::TransferWait wait{ timeline.Handle(), 1u };
    const std::array<rhi::TransferWait, rhi::k_MaxTransferWaits + 1> tooMany{ wait, wait, wait, wait, wait };

    // nothing is in flight, so the counters stay still until the calls below
    SleepForHoldWindow();
    const ResourceContextCounters before = context->Counters();

    const rhi::OperationFuture refused = context->FillBuffer(buffer, 0u, 0u, 64u, tooMany);

    // no wait here: the future is Failed when the call returns, so the worker did not fail it
    EXPECT_EQ(refused.State(), rhi::FutureState::Failed);
    EXPECT_EQ(refused.Error(), rhi::ResourceError::TooManyWaits);

    // two later calls that make no transfer. the refused call must not have sent a message of its own
    EXPECT_EQ(context->Wait(context->RegisterConsumerTimeline(timeline.Handle()), k_TimeoutNs), rhi::FutureState::Complete);
    EXPECT_EQ(context->Wait(context->UnregisterConsumerTimeline(timeline.Handle()), k_TimeoutNs), rhi::FutureState::Complete);

    int64_t processed = context->Counters().MessagesProcessed - before.MessagesProcessed;
    for (int32_t attempt = 0; attempt < 100 && processed < 2; ++attempt)
    {
        SleepForHoldWindow();
        processed = context->Counters().MessagesProcessed - before.MessagesProcessed;
    }

    SleepForHoldWindow();
    const ResourceContextCounters after = context->Counters();
    EXPECT_EQ(after.MessagesProcessed - before.MessagesProcessed, 2);
    EXPECT_EQ(after.TransfersRecorded, before.TransfersRecorded);

    DestroyAndWait(buffer);
}

TEST_F(ResourceContextTest, TwoWaitsOnOneTimelineUseTheLargerValue)
{
    rhi::TimelineSemaphore timeline(rhiSystem->GetDevice()->Handle());
    rhi::OperationFuture copy;
    TimelineReleaseGuard guard(*context, timeline, 2u);
    guard.Track(copy);

    const std::vector<uint32_t> words = MakeWordPattern(k_Words, 55u);
    const RhiBufferResourceData data{ std::as_bytes(std::span{ words }), 0u };
    const GraphicsResource source = CreateBufferAndWait(DeviceLocalCopyBuffer(k_Bytes), std::span{ &data, 1u });
    const GraphicsResource destination = CreateBufferAndWait(DeviceLocalCopyBuffer(k_Bytes), {});

    const std::array<rhi::TransferWait, 2> waits{ rhi::TransferWait{ timeline.Handle(), 1u }, rhi::TransferWait{ timeline.Handle(), 2u } };
    copy = context->CopyBufferContents(source, destination, waits);

    // value 1 alone is not enough
    ASSERT_TRUE(timeline.SetValue(1u).IsSuccess());
    EXPECT_EQ(context->Wait(copy, k_HoldWindowNs), rhi::FutureState::Created);

    ASSERT_TRUE(timeline.SetValue(2u).IsSuccess());
    EXPECT_EQ(context->Wait(copy, k_TimeoutNs), rhi::FutureState::Complete);
    EXPECT_EQ(ReadBackWords(destination, k_Bytes), words);

    DestroyAndWait(source);
    DestroyAndWait(destination);
}

namespace
{
    TimelineReleaseGuard::TimelineReleaseGuard(ResourceContext& _context, rhi::TimelineSemaphore& _timeline, uint64_t _final_value) :
        context{ _context },
        timeline{ _timeline },
        finalValue{ _final_value }
    {}

    TimelineReleaseGuard::~TimelineReleaseGuard()
    {
        uint64_t current = 0u;
        if (timeline.GetValue(current).IsSuccess() && current < finalValue)
        {
            static_cast<void>(timeline.SetValue(finalValue));
        }

        // the semaphore is destroyed after this guard, so no transfer may still wait on it
        for (const rhi::OperationFuture* future : tracked)
        {
            static_cast<void>(context.Wait(*future, k_GuardWaitNs));
        }
    }

    void TimelineReleaseGuard::Track(const rhi::OperationFuture& future)
    {
        tracked.push_back(&future);
    }

    BufferCreateInfo DeviceLocalCopyBuffer(size_t size)
    {
        BufferCreateInfo info{};
        info.Domain = ResourceDomain::GPUOnly;
        info.Usage = BufferUsageBits::CopySource | BufferUsageBits::CopyDestination;
        info.Size = size;
        return info;
    }

    BufferCreateInfo HostVisibleBuffer(size_t size)
    {
        BufferCreateInfo info{};
        info.Domain = ResourceDomain::CPUToGPU;
        info.Usage = BufferUsageBits::CopySource | BufferUsageBits::CopyDestination;
        info.Size = size;
        return info;
    }

    void SleepForHoldWindow()
    {
        std::this_thread::sleep_for(std::chrono::nanoseconds(k_HoldWindowNs));
    }
}
