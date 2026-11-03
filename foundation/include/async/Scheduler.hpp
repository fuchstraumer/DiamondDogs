#pragma once
#ifndef FOUNDATION_ASYNC_SCHEDULER_HPP
#define FOUNDATION_ASYNC_SCHEDULER_HPP
#include "async/FrameNames.hpp"
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

/**@brief Runs coroutines on one owner thread. Spawned roots start on the next Tick, and frames parked on a Timeline
 * resume on the first Tick that sees their target reached. Every call must come from the owner thread, which is the
 * thread of the first call. A Timeline may be signaled from any thread. */
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
    /** @brief Registers a source that Tick polls. The source must outlive its registration. */
    void AddSource(TimelineSource& source);
    /** @brief Unregisters a source. The source must be registered. */
    void RemoveSource(TimelineSource& source);
    /** @brief Polls every source, resumes parked frames whose target is reached, starts spawned roots, and destroys
     * finished roots. The sources are polled first, so a target that a source reaches in this Tick resumes its frame in
     * this Tick. A frame that parks during this Tick, or whose target is reached by another frame during it, resumes on
     * a later Tick. */
    void Tick();

    /** @brief Returns true when no root is alive. */
    [[nodiscard]] bool Idle() const noexcept;
    [[nodiscard]] int32_t RootCount() const noexcept;
    [[nodiscard]] int32_t ParkedCount() const noexcept;
    [[nodiscard]] int64_t TickCount() const noexcept;
    /** @brief Returns one line for each parked frame (its target, the current value, the ticks parked), followed by
     * the frame descriptions of its async backtrace (function names where DbgHelp is available, else addresses), from
     * the parked frame up to its root. */
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

    void checkOwnerThread() noexcept;
    void pollSources();
    void startSpawnedRoots();
    void resumeReachedFrames();
    void destroyFinishedRoots();

    std::vector<std::coroutine_handle<>> roots;
    std::vector<std::coroutine_handle<>> spawned;
    std::vector<std::coroutine_handle<>> starting;
    std::vector<ParkedFrame> parked;
    std::vector<ParkedFrame> resuming;
    std::vector<TimelineSource*> sources;
    mutable FrameSymbolizer symbolizer; // DumpState is const but a symbolizer starts its DbgHelp session on first use
    std::thread::id ownerThread{};
    int64_t tickCount{ 0 };
};

}

#endif // !FOUNDATION_ASYNC_SCHEDULER_HPP
