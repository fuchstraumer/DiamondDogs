#include "async/Yield.hpp"
#include "async/Scheduler.hpp"
#include <cassert>
#include <coroutine>

namespace foundation
{

bool YieldAwaiter::await_ready() const noexcept
{
    return false;
}

void YieldAwaiter::await_suspend(std::coroutine_handle<> handle) noexcept
{
    // Owner is null when the awaiting coroutine is not a Task: its promise has no await_transform to bind us
    assert(Owner != nullptr);
    Owner->ParkUntilNextTick(handle, Waiter);
}

void YieldAwaiter::await_resume() const noexcept
{
}

void YieldAwaiter::BindOwner(Scheduler* scheduler, BasePromise* awaiting) noexcept
{
    Owner = scheduler;
    Waiter = awaiting;
}

YieldAwaiter Yield() noexcept
{
    return YieldAwaiter{};
}

}
