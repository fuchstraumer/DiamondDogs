#include "async/FrameHeap.hpp"
#include "async/FrameStatistics.hpp"
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mimalloc.h>
#include <new>
#include <print>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace
{
    // stored before each frame. The size keeps the frame 16-byte aligned, which is what mimalloc guarantees
    struct alignas(16) FrameHeader
    {
        foundation::FrameHeap* Owner{ nullptr };
        foundation::FrameBackend Backend{ foundation::FrameBackend::Mimalloc };
    };

    static_assert(sizeof(FrameHeader) == 16u);
    // no align_val_t overload of operator new exists, so a frame is at most 16-byte aligned
    static_assert(alignof(std::max_align_t) <= 16u);

    constinit foundation::FrameCounters processCounters;
    constinit thread_local foundation::FrameHeap* currentHeap{ nullptr };
    constinit thread_local foundation::FrameBackend fallbackBackend{ foundation::FrameBackend::Mimalloc };

    FrameHeader* HeaderOfFrame(void* frame) noexcept;
    void* FrameOfBlock(void* block) noexcept;
#if FOUNDATION_ASYNC_FRAME_STATISTICS
    foundation::FrameHeap* OwnerOfFrame(void* frame) noexcept;
#endif
    void* AllocateFromBackend(foundation::FrameBackend backend, std::size_t block_size) noexcept;
    void FreeToBackend(foundation::FrameBackend backend, void* block, std::size_t block_size) noexcept;
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

FrameHeap::FrameHeap() noexcept : heap{ mi_heap_new() }
{
    assert(heap != nullptr && "mi_heap_new failed");
}

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

    // never mi_heap_destroy: it frees live blocks without running anything that owns them
    mi_heap_delete(heap);
}

void FrameHeap::BindToCurrentThread() noexcept
{
    assert(!bound);

    ownerTheap = mi_heap_theap(heap);
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
    ownerTheap = nullptr;
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
    FrameBackend backend = FrameBackend::Mimalloc;
    void* block = nullptr;

    if (routedHeap != nullptr)
    {
        block = mi_theap_malloc(routedHeap->ownerTheap, blockSize);
    }
    else
    {
        backend = fallbackBackend;
        block = AllocateFromBackend(backend, blockSize);
    }

    if (block == nullptr)
    {
        throw std::bad_alloc{};
    }

    ::new (block) FrameHeader{ routedHeap, backend };

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

    const FrameBackend backend = header->Backend;

    if (backend == FrameBackend::Mimalloc)
    {
        mi_free_size(header, size + sizeof(FrameHeader));
    }
    else
    {
        FreeToBackend(backend, header, size + sizeof(FrameHeader));
    }
}

FrameStatisticsSnapshot FrameHeap::ProcessSnapshot() noexcept
{
    return processCounters.Snapshot();
}

}

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

    void* AllocateFromBackend(foundation::FrameBackend backend, std::size_t block_size) noexcept
    {
        switch (backend)
        {
        case foundation::FrameBackend::Mimalloc:
            return mi_malloc(block_size);
#ifdef _WIN32
        case foundation::FrameBackend::WindowsHeap:
            return HeapAlloc(GetProcessHeap(), 0, block_size);
#endif
        case foundation::FrameBackend::CrtMalloc:
            return std::malloc(block_size);
        case foundation::FrameBackend::Invalid:
            break;
        }

        // an unknown backend, or WindowsHeap off Windows: the caller turns null into bad_alloc
        assert(false && "unknown frame backend");
        return nullptr;
    }

    void FreeToBackend(foundation::FrameBackend backend, void* block, std::size_t block_size) noexcept
    {
        switch (backend)
        {
        case foundation::FrameBackend::Mimalloc:
            mi_free_size(block, block_size);
            break;
#ifdef _WIN32
        case foundation::FrameBackend::WindowsHeap:
            HeapFree(GetProcessHeap(), 0, block);
            break;
#endif
        case foundation::FrameBackend::CrtMalloc:
            std::free(block);
            break;
        case foundation::FrameBackend::Invalid:
            std::unreachable();
        }
    }
}
