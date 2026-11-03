#pragma once
#ifndef FOUNDATION_THREADING_PRECISION_TIMER_HPP
#define FOUNDATION_THREADING_PRECISION_TIMER_HPP
#include <chrono>
#include <memory>

namespace foundation
{

struct PlatformTimerImpl;

class PrecisionTimer
{
public:
    PrecisionTimer();
    ~PrecisionTimer();

    PrecisionTimer(const PrecisionTimer&) = delete;
    PrecisionTimer& operator=(const PrecisionTimer&) = delete;

    PrecisionTimer(PrecisionTimer&& other) noexcept;
    PrecisionTimer& operator=(PrecisionTimer&& other) noexcept;

    void SleepFor(const std::chrono::steady_clock::duration& duration) noexcept;
    void SleepUntil(const std::chrono::steady_clock::time_point& deadline) noexcept;
    [[nodiscard]] bool HasHighResolutionSupport() const noexcept;

private:
    std::unique_ptr<PlatformTimerImpl> impl;
};

} // namespace foundation

#endif // FOUNDATION_THREADING_PRECISION_TIMER_HPP
