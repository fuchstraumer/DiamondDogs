#pragma once
#ifndef FOUNDATION_ASYNC_FRAME_HEAP_HPP
#define FOUNDATION_ASYNC_FRAME_HEAP_HPP
#include "async/FrameStatistics.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>

// mimalloc types, forward declared so that no consumer of this header includes <mimalloc.h>
struct mi_heap_s;
struct mi_theap_s;

namespace foundation
{

/**@brief Which allocator serves frames that no FrameHeap routes. Only benchmarks pick anything but Mimalloc. */
enum class FrameBackend : uint8_t
{
    Invalid = 0,
    Mimalloc = 1,
    WindowsHeap = 2, // HeapAlloc on the process heap, which mimalloc cannot redirect
    CrtMalloc = 3,
};

/**@brief Counters for coroutine frames. A frame can be freed on a different thread than the one that allocated it, so
 * the counters are atomics. They use relaxed ordering because they are statistics, and no other code orders its work
 * by them. When FOUNDATION_ASYNC_FRAME_STATISTICS is 0, the class has no members and Snapshot returns zeros. */
class FrameCounters
{
public:
    void RecordAllocation(std::size_t size) noexcept;
    void RecordFree(std::size_t size) noexcept;
    [[nodiscard]] FrameStatisticsSnapshot Snapshot() const noexcept;

private:
#if FOUNDATION_ASYNC_FRAME_STATISTICS
    void raisePeak(uint64_t candidate) noexcept;

    std::atomic<int64_t> liveFrames{ 0 };
    std::atomic<uint64_t> liveBytes{ 0u };
    std::atomic<uint64_t> peakLiveBytes{ 0u };
    std::atomic<int64_t> totalFrames{ 0 };
#endif
};

/**@brief A mimalloc heap that stores the coroutine frames of one Scheduler, and the counters of those frames.
 * BasePromise::operator new allocates from the heap that is bound to the calling thread. When no heap is bound, the
 * frame comes from mimalloc's default heap. Every frame has a 16-byte header, which stores the FrameHeap that counted
 * it, so that operator delete can update the right counters. Frames can be freed from any thread. A FrameHeap must
 * outlive its frames, because the header of a frame points to it. */
class FrameHeap
{
public:
    FrameHeap() noexcept;
    /** @brief Moves the live blocks of the heap to mimalloc's main heap (mi_heap_delete). In Debug, prints a line to
     * stderr when frames are still alive. */
    ~FrameHeap();
    FrameHeap(const FrameHeap&) = delete;
    FrameHeap& operator=(const FrameHeap&) = delete;
    FrameHeap(FrameHeap&&) = delete;
    FrameHeap& operator=(FrameHeap&&) = delete;

    /** @brief Makes this heap the current heap of the calling thread, which becomes its owner thread. Remembers the
     * heap that was current before. */
    void BindToCurrentThread() noexcept;
    /** @brief Restores the heap that was current before BindToCurrentThread. Asserts when this heap is not the
     * current heap of the calling thread, which means heaps were unbound out of order or on the wrong thread. */
    void UnbindFromCurrentThread() noexcept;
    /** @brief Makes this bound heap the current heap of the calling thread for a while, and returns the heap that was
     * current. Pair it with RestoreCurrent on the same thread. Scheduler::Tick uses this so that the frames created
     * during a Tick come from the heap of the Scheduler that ticks. */
    [[nodiscard]] FrameHeap* MakeCurrent() noexcept;
    /** @brief Makes `previous`, which MakeCurrent returned, the current heap of the calling thread again. */
    static void RestoreCurrent(FrameHeap* previous) noexcept;
    [[nodiscard]] FrameStatisticsSnapshot Snapshot() const noexcept;

    /** @brief Returns the heap that is bound to the calling thread, or null. */
    [[nodiscard]] static FrameHeap* Current() noexcept;
    /** @brief Unbinds the current heap of the calling thread without destroying it, so that frames come from the
     * default heap. Returns the heap that was current. A benchmark uses this to measure the default heap. Pair it
     * with ResumeRouting on the same thread. */
    [[nodiscard]] static FrameHeap* SuspendRouting() noexcept;
    static void ResumeRouting(FrameHeap* suspended) noexcept;
    /** @brief Picks the allocator for frames that no heap routes, on the calling thread. Returns the previous choice.
     * Only the unrouted path reads it, so routed frames pay nothing. Every frame stores its backend in its header, so
     * a frame is freed by the allocator that made it. For benchmarks. */
    static FrameBackend SetFallbackBackend(FrameBackend backend) noexcept;

    /** @brief Returns the address of a new frame. Called by BasePromise::operator new. */
    [[nodiscard]] static void* AllocateFrame(std::size_t size);
    /** @brief Frees a frame, on any thread. Called by BasePromise::operator delete. */
    static void FreeFrame(void* frame, std::size_t size) noexcept;
    /** @brief Returns the counters of every frame in the process, whichever heap allocated it. */
    [[nodiscard]] static FrameStatisticsSnapshot ProcessSnapshot() noexcept;

private:
    mi_heap_s* heap{ nullptr };
    mi_theap_s* ownerTheap{ nullptr }; // the owner thread's handle to `heap`, so that allocation skips a lookup
    FrameHeap* previousHeap{ nullptr };
    bool bound{ false };
    FrameCounters counters;
};

}

#endif // !FOUNDATION_ASYNC_FRAME_HEAP_HPP
