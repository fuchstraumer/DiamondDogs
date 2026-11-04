#pragma once
#ifndef FOUNDATION_ASYNC_TIMELINE_HPP
#define FOUNDATION_ASYNC_TIMELINE_HPP
#include <atomic>
#include <coroutine>
#include <cstdint>

namespace foundation
{

class Timeline;
class Scheduler;
struct BasePromise;

/**@brief What a wait on a Timeline returns. Reached means the value was reached. Failed means the signaler called
 * Fail first. A timeline that reached the value and then failed still reports Reached. */
enum class TimelineWait : uint8_t
{
    Invalid = 0,
    Reached = 1,
    Failed = 2,
};

struct TimelineAwaiter
{
    [[nodiscard]] bool await_ready() const noexcept;
    void await_suspend(std::coroutine_handle<> handle) noexcept;
    [[nodiscard]] TimelineWait await_resume() const noexcept;
    void BindOwner(Scheduler* scheduler, BasePromise* awaiting) noexcept;
    const Timeline* Source{ nullptr };
    uint64_t TargetValue{ 0u };
    Scheduler* Owner{ nullptr };
    BasePromise* Waiter{ nullptr };
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
    // marks the timeline as failed, for good: a parked frame resumes and a blocked waiter wakes. Only the signaler calls
    // this, like Signal. Release semantics. Values already reached stay reached
    void Fail() noexcept;
    // acquire semantics used for this load
    [[nodiscard]] bool Failed() const noexcept;
    // block the calling thread until the timeline reaches the specified value or fails: only use this on workers,
    // *never* reactors
    [[nodiscard]] TimelineWait Wait(uint64_t value) const noexcept;
    // suspend execution until the timeline reaches the specified value or fails
    [[nodiscard]] TimelineAwaiter Until(uint64_t value) const noexcept;
private:
    // the top bit is the failed flag and the other 63 bits are the value, so that one atomic wakes a blocked waiter on
    // either change
    std::atomic<uint64_t> state{ 0u };
};

}

#endif // !FOUNDATION_ASYNC_TIMELINE_HPP
