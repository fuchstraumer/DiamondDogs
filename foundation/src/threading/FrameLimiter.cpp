#include "threading/FrameLimiter.hpp"
#include <chrono>
#include <ratio>
#include <thread>
#include <immintrin.h>

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
        fps = 60.0;
    }

    targetFps = fps;

    targetInterval = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double, std::ratio<1, 1>>(1.0 / targetFps)
    );
    
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

void FrameLimiter::Reset() noexcept
{
    initialized = false;
}

void FrameLimiter::WaitNextFrame() noexcept
{
    const auto now = Clock::now();
    
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

    constexpr auto k_SpinMargin = std::chrono::microseconds(1500);
    while (Clock::now() + k_SpinMargin < nextDeadline)
    {
        const auto remaining = nextDeadline - Clock::now();
        if (remaining > k_SpinMargin)
        {
            const auto sleepTime = std::chrono::duration_cast<std::chrono::milliseconds>(remaining - k_SpinMargin);
            if (sleepTime.count() > 0)
            {
                std::this_thread::sleep_for(sleepTime);
            }
            else
            {
                break;
            }
        }
    }

    while (Clock::now() < nextDeadline)
    {
        _mm_pause();
    }

    nextDeadline += targetInterval;
}

} // namespace foundation
