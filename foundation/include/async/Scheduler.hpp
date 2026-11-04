#pragma once
#ifndef FOUNDATION_ASYNC_SCHEDULER_HPP
#define FOUNDATION_ASYNC_SCHEDULER_HPP
#include "async/FrameHeap.hpp"
#include "async/FrameNames.hpp"
#include "async/FrameStatistics.hpp"
#include "async/Task.hpp"
#include <coroutine>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace foundation
{

struct BasePromise;
class Timeline;
class TimelineSource;

/**@brief Runs coroutines on one owner thread, which is the thread that constructs the Scheduler. Spawned roots start
 * on the next Tick, and frames parked on a Timeline resume on the first Tick that sees their target reached. Every
 * call must come from the owner thread. A Timeline may be signaled from any thread.
 *
 * The Scheduler owns a FrameHeap and binds it to the owner thread. Every coroutine frame that the owner thread creates
 * while the Scheduler is alive is allocated from that heap. If two Schedulers live on one thread, the one that was
 * constructed last owns the routing, and Schedulers must be destroyed in the reverse order of construction. */
class Scheduler
{
public:
    Scheduler() noexcept;
    /** @brief Reports every root and parked frame still alive (in Debug), then destroys the roots. */
    ~Scheduler();
    // awaiters and promises hold the address of their scheduler
    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;
    Scheduler(Scheduler&&) = delete;
    Scheduler& operator=(Scheduler&&) = delete;

    /** @brief Takes ownership of a root task. It starts on the next Tick, and its frame is destroyed when it finishes. */
    void Spawn(Task<void>&& root);
    /** @brief Called by TimelineAwaiter::await_suspend. `waiter` is the promise of the parked frame. */
    void Park(const Timeline& source, uint64_t target_value, std::coroutine_handle<> handle, BasePromise* waiter);
    /** @brief Called by YieldAwaiter::await_suspend. The frame resumes on the next Tick. `waiter` is the promise of
     * the suspended frame, or null. */
    void ParkUntilNextTick(std::coroutine_handle<> handle, BasePromise* waiter);
    /** @brief Resumes a suspended frame later in the same Tick, after the frame that is running now suspends or
     * finishes. A frame that is posted outside a Tick resumes in the next Tick. The handle must be a suspended frame
     * that this Scheduler runs, and it must not be posted twice. A frame that posts itself again each time it resumes
     * never ends the Tick: use Yield to give the frame back instead. */
    void Post(std::coroutine_handle<> handle);
    /** @brief Same as Post. `waiter` is the promise of the posted frame, so that DumpState can walk its backtrace. */
    void Post(std::coroutine_handle<> handle, BasePromise* waiter);
    /** @brief Registers a source that Tick polls. The source must outlive its registration. */
    void AddSource(TimelineSource& source);
    /** @brief Unregisters a source. The source must be registered. */
    void RemoveSource(TimelineSource& source);
    /** @brief Does these steps in order:
     * - polls every source
     * - resumes parked frames whose target is reached or whose timeline failed
     * - starts spawned roots
     * - runs the ready list: first the frames that yielded in the previous Tick, then the posted frames, until no
     *   posted frame is left, because a resumed frame can post again
     * - destroys finished roots.
     * The sources are polled first, so a target that a source reaches in this Tick resumes its frame in this Tick. A
     * frame that parks during this Tick, or whose target is reached by another frame during it, resumes on a later
     * Tick. A frame that yields during this Tick resumes on the next Tick. */
    void Tick();

    /** @brief Returns true when no root is alive. */
    [[nodiscard]] bool Idle() const noexcept;
    [[nodiscard]] int32_t RootCount() const noexcept;
    [[nodiscard]] int32_t ParkedCount() const noexcept;
    [[nodiscard]] int64_t TickCount() const noexcept;
    /** @brief Returns the counters of the coroutine frames in the heap of this Scheduler. */
    [[nodiscard]] FrameStatisticsSnapshot FrameStatistics() const noexcept;
    /** @brief Returns one line for each parked frame (its target, the current value, the ticks parked), and one line
     * for each ready or yielded frame (the ticks since it was posted or yielded). Each line is followed by the frame
     * descriptions of its async backtrace (function names where DbgHelp is available, else addresses), from the
     * suspended frame up to its root. */
    [[nodiscard]] std::string DumpState() const;

private:
    struct ParkedFrame
    {
        const Timeline* Source{ nullptr };
        uint64_t TargetValue{ 0u };
        std::coroutine_handle<> Handle{ nullptr };
        BasePromise* Waiter{ nullptr };
        int64_t ParkedAtTick{ 0 };
    };

    struct ReadyFrame
    {
        std::coroutine_handle<> Handle{ nullptr };
        BasePromise* Waiter{ nullptr };
        int64_t QueuedAtTick{ 0 };
    };

    void checkOwnerThread() const noexcept;
    void pollSources();
    void startSpawnedRoots();
    void resumeReachedFrames();
    void runReadyList();
    void destroyFinishedRoots();
    void appendReadyFrames(std::string& output, const char* label, const std::vector<ReadyFrame>& frames) const;
    void appendBacktrace(std::string& output, const BasePromise* waiter, std::coroutine_handle<> handle) const;

    // first member, so that it is destroyed last: the roots are destroyed in the destructor body
    FrameHeap frameHeap;
    std::vector<std::coroutine_handle<>> roots;
    std::vector<std::coroutine_handle<>> spawned;
    std::vector<std::coroutine_handle<>> starting;
    std::vector<ParkedFrame> parked;
    std::vector<ParkedFrame> resuming;
    std::vector<ReadyFrame> ready;
    std::vector<ReadyFrame> draining;
    std::vector<ReadyFrame> yielded;
    std::vector<ReadyFrame> yieldedPreviously;
    std::vector<TimelineSource*> sources;
    mutable FrameSymbolizer symbolizer; // DumpState is const but a symbolizer starts its DbgHelp session on first use
    std::thread::id ownerThread{};
    int64_t tickCount{ 0 };
};

}

#endif // !FOUNDATION_ASYNC_SCHEDULER_HPP
