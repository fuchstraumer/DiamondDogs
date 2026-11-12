#include "timing/HighResolutionWait.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <ratio>
#include <thread>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>
#include <timeapi.h>
#include <synchapi.h>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace foundation
{

struct HighResolutionWaitImpl
{
    HANDLE TimerHandle{ nullptr };
    bool TimePeriodActive{ false };
    bool HighResolutionActive{ false };
};

HighResolutionWait::HighResolutionWait() : impl(std::make_unique<HighResolutionWaitImpl>())
{
    timeBeginPeriod(1);
    impl->TimePeriodActive = true;
    impl->TimerHandle = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);

    if (impl->TimerHandle != nullptr)
    {
        impl->HighResolutionActive = true;
        return;
    }

    // before Windows 10 1803 there is no high resolution timer: fall back to an ordinary one
    impl->TimerHandle = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);

    if (impl->TimerHandle == nullptr)
    {
        impl->TimerHandle = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    }
}

HighResolutionWait::~HighResolutionWait()
{
    if (!impl)
    {
        return;
    }

    if (impl->TimerHandle != nullptr)
    {
        CloseHandle(impl->TimerHandle);
        impl->TimerHandle = nullptr;
    }

    if (impl->TimePeriodActive)
    {
        timeEndPeriod(1);
        impl->TimePeriodActive = false;
    }
}

HighResolutionWait::HighResolutionWait(HighResolutionWait&& other) noexcept = default;
HighResolutionWait& HighResolutionWait::operator=(HighResolutionWait&& other) noexcept = default;

bool HighResolutionWait::HasHighResolutionSupport() const noexcept
{
    return impl && impl->HighResolutionActive;
}

void HighResolutionWait::SleepFor(const std::chrono::steady_clock::duration& duration) noexcept
{
    if (duration <= std::chrono::steady_clock::duration::zero())
    {
        return;
    }

    if (!impl || impl->TimerHandle == nullptr)
    {
        std::this_thread::sleep_for(duration);
        return;
    }

    // a negative due time is relative, in 100 ns units
    const int64_t ticks100ns = std::chrono::duration_cast<std::chrono::duration<int64_t, std::ratio<1, 10000000>>>(duration).count();
    LARGE_INTEGER dueTime{};

    dueTime.QuadPart = -std::max<int64_t>(1LL, ticks100ns);
    SetWaitableTimer(impl->TimerHandle, &dueTime, 0, nullptr, nullptr, FALSE);
    WaitForSingleObject(impl->TimerHandle, INFINITE);
}

void HighResolutionWait::SleepUntil(const std::chrono::steady_clock::time_point& deadline) noexcept
{
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

    if (deadline > now)
    {
        SleepFor(deadline - now);
    }
}

} // namespace foundation
