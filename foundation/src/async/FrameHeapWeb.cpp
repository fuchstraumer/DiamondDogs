#include "async/FrameHeap.hpp"
#include "async/FrameStatistics.hpp"
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <print>

namespace
{
    struct alignas(16) FrameHeader
    {
        foundation::FrameHeap* Owner{ nullptr };
    };

    static_assert(sizeof(FrameHeader) == 16u);
    static_assert(alignof(std::max_align_t) <= 16u);

    constinit foundation::FrameCounters processCounters;
    constinit thread_local foundation::FrameHeap* currentHeap{ nullptr };
    constinit thread_local foundation::FrameBackend fallbackBackend{ foundation::FrameBackend::CrtMalloc };

    FrameHeader* HeaderOfFrame(void* frame) noexcept;
    void* FrameOfBlock(void* block) noexcept;
#if FOUNDATION_ASYNC_FRAME_STATISTICS
    foundation::FrameHeap* OwnerOfFrame(void* frame) noexcept;
#endif
}

namespace foundation
{

#if FOUNDATION_ASYNC_FRAME_STATISTICS

void FrameCounters::RecordAllocation(std::size_t size) noexcept
{
    liveFrames.fetch_add(1, std::memory_order_relaxed);
    totalFrames.fetch_add(1, std::memory_order_relaxed);
    const uint64_t bytesAfter = liveBytes.fetch_add(size, std::memory_order_relaxed) + size;
    raisePeak(bytesAfter);
}

void FrameCounters::RecordFree(std::size_t size) noexcept
{
    liveFrames.fetch_sub(1, std::memory_order_relaxed);
    liveBytes.fetch_sub(size, std::memory_order_relaxed);
}

FrameStatisticsSnapshot FrameCounters::Snapshot() const noexcept
{
    FrameStatisticsSnapshot snapshot;
    snapshot.LiveFrames = liveFrames.load(std::memory_order_relaxed);
    snapshot.LiveBytes = liveBytes.load(std::memory_order_relaxed);
    snapshot.PeakLiveBytes = peakLiveBytes.load(std::memory_order_relaxed);
    snapshot.TotalFrames = totalFrames.load(std::memory_order_relaxed);
    return snapshot;
}

void FrameCounters::raisePeak(uint64_t candidate) noexcept
{
    uint64_t observed = peakLiveBytes.load(std::memory_order_relaxed);

    while (observed < candidate &&
           !peakLiveBytes.compare_exchange_weak(observed, candidate, std::memory_order_relaxed))
    {
    }
}

#else

void FrameCounters::RecordAllocation(std::size_t) noexcept
{
}

void FrameCounters::RecordFree(std::size_t) noexcept
{
}

FrameStatisticsSnapshot FrameCounters::Snapshot() const noexcept
{
    return FrameStatisticsSnapshot{};
}

#endif

FrameHeap::FrameHeap() noexcept = default;

FrameHeap::~FrameHeap()
{
    assert(!bound && "a FrameHeap is unbound before it is destroyed");

#ifndef NDEBUG
    const FrameStatisticsSnapshot snapshot = counters.Snapshot();

    if (snapshot.LiveFrames != 0)
    {
        std::println(stderr,
                     "[async] FrameHeap destroyed with {} live frames ({} bytes). Their headers point to a dead heap.",
                     snapshot.LiveFrames, snapshot.LiveBytes);
    }
#endif
}

void FrameHeap::BindToCurrentThread() noexcept
{
    assert(!bound);

    previousHeap = currentHeap;
    currentHeap = this;
    bound = true;
}

void FrameHeap::UnbindFromCurrentThread() noexcept
{
    assert(bound);
    assert(currentHeap == this && "frame heaps are unbound in reverse order of binding, on the binding thread");

    currentHeap = previousHeap;
    previousHeap = nullptr;
    bound = false;
}

FrameHeap* FrameHeap::MakeCurrent() noexcept
{
    assert(bound);

    FrameHeap* previous = currentHeap;
    currentHeap = this;
    return previous;
}

void FrameHeap::RestoreCurrent(FrameHeap* previous) noexcept
{
    currentHeap = previous;
}

FrameStatisticsSnapshot FrameHeap::Snapshot() const noexcept
{
    return counters.Snapshot();
}

FrameHeap* FrameHeap::Current() noexcept
{
    return currentHeap;
}

FrameHeap* FrameHeap::SuspendRouting() noexcept
{
    FrameHeap* suspended = currentHeap;
    currentHeap = nullptr;
    return suspended;
}

void FrameHeap::ResumeRouting(FrameHeap* suspended) noexcept
{
    assert(currentHeap == nullptr && "ResumeRouting follows SuspendRouting on the same thread");
    currentHeap = suspended;
}

FrameBackend FrameHeap::SetFallbackBackend(FrameBackend backend) noexcept
{
    const FrameBackend previous = fallbackBackend;
    fallbackBackend = backend;
    return previous;
}

void* FrameHeap::AllocateFrame(std::size_t size)
{
    FrameHeap* routedHeap = currentHeap;
    const std::size_t blockSize = size + sizeof(FrameHeader);
    void* block = std::malloc(blockSize);

    if (block == nullptr)
    {
        std::abort();
    }

    ::new (block) FrameHeader{ routedHeap };

#if FOUNDATION_ASYNC_FRAME_STATISTICS
    processCounters.RecordAllocation(size);

    if (routedHeap != nullptr)
    {
        routedHeap->counters.RecordAllocation(size);
    }
#endif

    return FrameOfBlock(block);
}

void FrameHeap::FreeFrame(void* frame, std::size_t size) noexcept
{
    FrameHeader* header = HeaderOfFrame(frame);

#if FOUNDATION_ASYNC_FRAME_STATISTICS
    FrameHeap* owner = OwnerOfFrame(frame);

    processCounters.RecordFree(size);

    if (owner != nullptr)
    {
        owner->counters.RecordFree(size);
    }
#endif

    std::free(header);
}

FrameStatisticsSnapshot FrameHeap::ProcessSnapshot() noexcept
{
    return processCounters.Snapshot();
}

} // namespace foundation

namespace
{
    FrameHeader* HeaderOfFrame(void* frame) noexcept
    {
        std::byte* bytes = static_cast<std::byte*>(frame);
        return std::launder(reinterpret_cast<FrameHeader*>(bytes - sizeof(FrameHeader)));
    }

    void* FrameOfBlock(void* block) noexcept
    {
        return static_cast<std::byte*>(block) + sizeof(FrameHeader);
    }

#if FOUNDATION_ASYNC_FRAME_STATISTICS
    foundation::FrameHeap* OwnerOfFrame(void* frame) noexcept
    {
        const FrameHeader* header = HeaderOfFrame(frame);
        return header->Owner;
    }
#endif
}
