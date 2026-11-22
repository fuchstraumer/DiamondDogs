#include "timing/FrameLimiter.hpp"
#include <chrono>
#include <ratio>
#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace
{
    // a rate of 1 Hz or less is a caller error, not a frame rate
    constexpr double k_FallbackFramerate{ 60.0 };
}

namespace foundation
{

using Clock = FrameLimiter::Clock;

FrameLimiter::FrameLimiter(double target_fps) noexcept
{
    SetTargetFramerate(target_fps);
}

void FrameLimiter::SetTargetFramerate(double fps) noexcept
{
    if (fps <= 1.0)
    {
        fps = k_FallbackFramerate;
    }

    targetFps = fps;
    targetInterval = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double, std::ratio<1, 1>>(1.0 / targetFps));
    initialized = false;
}

double FrameLimiter::TargetFramerate() const noexcept
{
    return targetFps;
}

std::chrono::duration<double, std::milli> FrameLimiter::TargetFrameInterval() const noexcept
{
    return std::chrono::duration<double, std::milli>(1000.0 / targetFps);
}

void FrameLimiter::SetSpinMargin(std::chrono::microseconds margin) noexcept
{
    spinMargin = margin;
}

std::chrono::microseconds FrameLimiter::SpinMargin() const noexcept
{
    return spinMargin;
}

void FrameLimiter::Reset() noexcept
{
    initialized = false;
}

void FrameLimiter::WaitNextFrame() noexcept
{
    const Clock::time_point now = Clock::now();

    if (!initialized)
    {
        nextDeadline = now + targetInterval;
        initialized = true;
        return;
    }

    if (now > nextDeadline + targetInterval)
    {
        nextDeadline = now + targetInterval;
        return;
    }

    // sleep to within the spin margin, then spin: a sleep can overshoot, a spin cannot
    while (Clock::now() + spinMargin < nextDeadline)
    {
        const Clock::duration remaining = nextDeadline - Clock::now();

        if (remaining <= spinMargin)
        {
            break;
        }

        highResolutionWait.SleepFor(remaining - spinMargin);
    }

    while (Clock::now() < nextDeadline)
    {
#if defined(__x86_64__) || defined(_M_X64)
        _mm_pause();
#endif
    }

    nextDeadline += targetInterval;
}

} // namespace foundation
