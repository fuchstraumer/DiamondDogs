#pragma once
#ifndef FOUNDATION_THREADING_FRAME_LIMITER_HPP
#define FOUNDATION_THREADING_FRAME_LIMITER_HPP

#include <chrono>
#include "threading/PrecisionTimer.hpp"

namespace foundation
{

class FrameLimiter
{
public:
    using Clock = std::chrono::steady_clock;

    explicit FrameLimiter(double target_fps = 60.0) noexcept;

    void SetTargetFramerate(double fps) noexcept;
    [[nodiscard]] double TargetFramerate() const noexcept;
    [[nodiscard]] std::chrono::duration<double, std::milli> TargetFrameInterval() const noexcept;
    void SetSpinMargin(std::chrono::microseconds margin) noexcept;
    [[nodiscard]] std::chrono::microseconds SpinMargin() const noexcept;
    void Reset() noexcept;
    void WaitNextFrame() noexcept;

private:
    std::chrono::steady_clock::duration targetInterval{};
    std::chrono::steady_clock::time_point nextDeadline{};
    double targetFps{ 60.0 };
    bool initialized{ false };
    PrecisionTimer precisionTimer{};
    std::chrono::microseconds spinMargin{ 300 };
};

} // namespace foundation

#endif // FOUNDATION_THREADING_FRAME_LIMITER_HPP
