#include "async/Timeline.hpp"
#include "async/Scheduler.hpp"
#include <atomic>
#include <cassert>
#include <cstdint>

namespace foundation
{
    
Timeline::Timeline() noexcept : value{ 0u } 
{
}

void Timeline::Signal(uint64_t _value) noexcept
{
    assert(_value >= value.load(std::memory_order_relaxed));
    value.store(_value, std::memory_order_release);
    value.notify_all();
}

uint64_t Timeline::Value() const noexcept
{
    return value.load(std::memory_order_acquire);
}

bool Timeline::Reached(uint64_t _value) const noexcept
{
    return Value() >= _value;
}

void Timeline::Wait(uint64_t _value) const noexcept
{
    uint64_t observed = value.load(std::memory_order_acquire);
    while (observed < _value)
    {
        value.wait(observed, std::memory_order_acquire);
        observed = value.load(std::memory_order_acquire);
    }
}

TimelineAwaiter Timeline::Until(uint64_t _value) const noexcept
{
    return TimelineAwaiter{ this, _value, nullptr, nullptr };
}

bool TimelineAwaiter::await_ready() const noexcept
{
    return Source->Reached(TargetValue);
}

void TimelineAwaiter::await_suspend(std::coroutine_handle<> handle) noexcept
{
    // Owner is null when the awaiting coroutine is not a Task: its promise has no await_transform to bind us
    assert(Owner != nullptr);
    Owner->Park(*Source, TargetValue, handle, Waiter);
}

void TimelineAwaiter::await_resume() const noexcept
{
}

void TimelineAwaiter::BindOwner(Scheduler* scheduler, BasePromise* awaiting) noexcept
{
    Owner = scheduler;
    Waiter = awaiting;
}

}
