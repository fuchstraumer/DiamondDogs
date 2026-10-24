#pragma once
#ifndef FOUNDATION_THREADING_TIMELINE_HPP
#define FOUNDATION_THREADING_TIMELINE_HPP
#include <atomic>
#include <coroutine>
#include <cstdint>

namespace foundation
{
    class Timeline;
    class Scheduler;

    struct TimelineAwaiter
    {
        [[nodiscard]] bool await_ready() const noexcept;
        void await_suspend(std::coroutine_handle<> handle) noexcept;
        void await_resume() const noexcept;
        Timeline* Source{ nullptr };
        uint64_t TargetValue{ 0u };
        Scheduler* Owner{ nullptr };
    };

    /**@brief Core synchronization primitive for coordinating and managing progress of asynchronous tasks
     * across both the GPU and the CPU. TimelineAwaiter sits over this to make it an awaitable type. */
    class Timeline
    {
    public:
        Timeline() noexcept;
        ~Timeline() noexcept = default;
        // Can't be copied, as it's an atomic primitive and copying just isn't safe or meaningful.
        Timeline(const Timeline&) = delete;
        Timeline& operator=(const Timeline&) = delete;
        // Can't be moved, as parked waiters hold it's address and moving would invalidate those
        Timeline(Timeline&& other) = delete;
        Timeline& operator=(Timeline&& other) = delete;

        // release semantics used for this store. One signaler per timeline: never decreases.
        void Signal(uint64_t value) noexcept;
        // acquire semantics used for this load
        [[nodiscard]] uint64_t Value() const noexcept;
        // check if the timeline has reached or surpassed the specified value
        [[nodiscard]] bool Reached(uint64_t value) const noexcept;
        // block the calling thread until the timeline reaches the specified value: only use this on workers, *never* reactors
        void Wait(uint64_t value) const noexcept;
        // suspend execution until the timeline reaches the specified value
        [[nodiscard]] TimelineAwaiter Until(uint64_t value) const noexcept;
    private:
        std::atomic<uint64_t> value{ 0u };
    };

}

#endif // !FOUNDATION_THREADING_TIMELINE_HPP
