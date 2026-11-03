#include "threading/PrecisionTimer.hpp"
#include <algorithm>
#include <chrono>
#include <memory>
#include <thread>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NO_MINMAX
#define NO_MINMAX
#endif

#include <Windows.h>
#include <timeapi.h>
#include <synchapi.h>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace foundation
{

struct PlatformTimerImpl
{
    HANDLE TimerHandle{ nullptr };
    bool TimePeriodActive{ false };
    bool HighResolutionActive{ false };
};

PrecisionTimer::PrecisionTimer()
    : impl(std::make_unique<PlatformTimerImpl>())
{
    timeBeginPeriod(1);
    impl->TimePeriodActive = true;

    impl->TimerHandle = CreateWaitableTimerExW(
        nullptr,
        nullptr,
        CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
        TIMER_ALL_ACCESS
    );

    if (impl->TimerHandle != nullptr)
    {
        impl->HighResolutionActive = true;
    }
    else
    {
        impl->TimerHandle = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
        if (impl->TimerHandle == nullptr)
        {
            impl->TimerHandle = CreateWaitableTimerW(nullptr, FALSE, nullptr);
        }
    }
}

PrecisionTimer::~PrecisionTimer()
{
    if (impl)
    {

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
}

PrecisionTimer::PrecisionTimer(PrecisionTimer&& other) noexcept = default;
PrecisionTimer& PrecisionTimer::operator=(PrecisionTimer&& other) noexcept = default;

bool PrecisionTimer::HasHighResolutionSupport() const noexcept
{
    return impl && impl->HighResolutionActive;
}

void PrecisionTimer::SleepFor(const std::chrono::steady_clock::duration& duration) noexcept
{
    if (duration <= std::chrono::steady_clock::duration::zero())
    {
        return;
    }

    if (impl && impl->TimerHandle != nullptr)
    {
        const auto ticks100ns = std::chrono::duration_cast<std::chrono::duration<int64_t, std::ratio<1, 10000000>>>(duration).count();
        LARGE_INTEGER dueTime;
        dueTime.QuadPart = -std::max<int64_t>(1LL, ticks100ns);
        SetWaitableTimer(impl->TimerHandle, &dueTime, 0, nullptr, nullptr, FALSE);
        WaitForSingleObject(impl->TimerHandle, INFINITE);
        return;
    }

    std::this_thread::sleep_for(duration);
}

void PrecisionTimer::SleepUntil(const std::chrono::steady_clock::time_point& deadline) noexcept
{
    const auto now = std::chrono::steady_clock::now();
    if (deadline > now)
    {
        SleepFor(deadline - now);
    }
}

} // namespace foundation
