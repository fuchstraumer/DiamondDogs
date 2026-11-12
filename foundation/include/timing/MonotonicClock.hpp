#pragma once
#ifndef FOUNDATION_TIMING_MONOTONIC_CLOCK_HPP
#define FOUNDATION_TIMING_MONOTONIC_CLOCK_HPP
#include <chrono>
#include <cstdint>

namespace foundation
{

/** @brief Returns the monotonic clock in nanoseconds. The zero point is arbitrary, so only a difference of two readings
 *  means anything. On Windows this is QueryPerformanceCounter, through std::chrono::steady_clock. */
[[nodiscard]] inline int64_t MonotonicNanoseconds() noexcept
{
    const std::chrono::steady_clock::duration sinceEpoch = std::chrono::steady_clock::now().time_since_epoch();

    return std::chrono::duration_cast<std::chrono::nanoseconds>(sinceEpoch).count();
}

/** Measures the time since it was constructed, or since the last Restart. */
class Stopwatch
{
public:

    Stopwatch() noexcept : startNanoseconds{ MonotonicNanoseconds() } {}

    void Restart() noexcept
    {
        startNanoseconds = MonotonicNanoseconds();
    }

    [[nodiscard]] int64_t ElapsedNanoseconds() const noexcept
    {
        return MonotonicNanoseconds() - startNanoseconds;
    }

    [[nodiscard]] double ElapsedMilliseconds() const noexcept
    {
        return static_cast<double>(ElapsedNanoseconds()) / 1'000'000.0;
    }

private:

    int64_t startNanoseconds;

};

} // namespace foundation

#endif // !FOUNDATION_TIMING_MONOTONIC_CLOCK_HPP
