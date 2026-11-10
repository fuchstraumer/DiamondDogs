#pragma once
#ifndef FOUNDATION_ASYNC_YIELD_HPP
#define FOUNDATION_ASYNC_YIELD_HPP
#include <coroutine>

namespace foundation
{

class Scheduler;
struct BasePromise;

/**@brief Gives the scheduler of the awaiting Task the frame back, until the next Tick. The Task's await_transform binds
 * the scheduler, so the call site names no scheduler. */
struct YieldAwaiter
{
    [[nodiscard]] bool await_ready() const noexcept;
    void await_suspend(std::coroutine_handle<> handle) noexcept;
    void await_resume() const noexcept;
    void BindOwner(Scheduler* scheduler, BasePromise* awaiting) noexcept;
    Scheduler* Owner{ nullptr };
    BasePromise* Waiter{ nullptr };
};

/**@brief Use as `co_await foundation::Yield();`. The frame resumes on the next Tick of its scheduler, not in this one.
 * A long computation calls this to stop holding the owner thread. Awaiting it in a coroutine that no Scheduler runs
 * asserts. */
[[nodiscard]] YieldAwaiter Yield() noexcept;

}

#endif // !FOUNDATION_ASYNC_YIELD_HPP
