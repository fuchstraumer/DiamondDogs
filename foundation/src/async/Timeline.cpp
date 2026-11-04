#include "async/Timeline.hpp"
#include "async/Scheduler.hpp"
#include <atomic>
#include <cassert>
#include <coroutine>
#include <cstdint>

namespace
{
    constexpr uint64_t k_FailedBit = uint64_t{ 1 } << 63;
}

namespace foundation
{
    
Timeline::Timeline() noexcept : state{ 0u } 
{
}

void Timeline::Signal(uint64_t _value) noexcept
{
    const uint64_t current = state.load(std::memory_order_relaxed);
    assert((_value & k_FailedBit) == 0u && "a timeline value has 63 bits");
    assert(_value >= (current & ~k_FailedBit));

    // the signaler is the only writer, so keeping the flag of `current` cannot lose a Fail
    state.store(_value | (current & k_FailedBit), std::memory_order_release);
    state.notify_all();
}

void Timeline::Fail() noexcept
{
    state.fetch_or(k_FailedBit, std::memory_order_release);
    state.notify_all();
}

uint64_t Timeline::Value() const noexcept
{
    return state.load(std::memory_order_acquire) & ~k_FailedBit;
}

bool Timeline::Failed() const noexcept
{
    return (state.load(std::memory_order_acquire) & k_FailedBit) != 0u;
}

bool Timeline::Reached(uint64_t _value) const noexcept
{
    return Value() >= _value;
}

TimelineWait Timeline::Wait(uint64_t _value) const noexcept
{
    uint64_t observed = state.load(std::memory_order_acquire);

    while ((observed & ~k_FailedBit) < _value && (observed & k_FailedBit) == 0u)
    {
        state.wait(observed, std::memory_order_acquire);
        observed = state.load(std::memory_order_acquire);
    }

    return (observed & ~k_FailedBit) >= _value ? TimelineWait::Reached : TimelineWait::Failed;
}

TimelineAwaiter Timeline::Until(uint64_t _value) const noexcept
{
    return TimelineAwaiter{ this, _value, nullptr, nullptr };
}

bool TimelineAwaiter::await_ready() const noexcept
{
    return Source->Reached(TargetValue) || Source->Failed();
}

void TimelineAwaiter::await_suspend(std::coroutine_handle<> handle) noexcept
{
    // Owner is null when the awaiting coroutine is not a Task: its promise has no await_transform to bind us
    assert(Owner != nullptr);
    Owner->Park(*Source, TargetValue, handle, Waiter);
}

TimelineWait TimelineAwaiter::await_resume() const noexcept
{
    if (Source->Reached(TargetValue))
    {
        return TimelineWait::Reached;
    }

    assert(Source->Failed() && "a frame resumes only when its target is reached or its timeline failed");
    return TimelineWait::Failed;
}

void TimelineAwaiter::BindOwner(Scheduler* scheduler, BasePromise* awaiting) noexcept
{
    Owner = scheduler;
    Waiter = awaiting;
}

}
