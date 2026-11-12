#pragma once
#ifndef FOUNDATION_TIMING_HIGH_RESOLUTION_WAIT_HPP
#define FOUNDATION_TIMING_HIGH_RESOLUTION_WAIT_HPP
#include <chrono>
#include <memory>

namespace foundation
{

struct HighResolutionWaitImpl;

/** Sleeps the calling thread with sub-millisecond precision. It does not read the time: use MonotonicNanoseconds or
 *  Stopwatch for that. One instance per sleeping thread, because a second sleeper resets the first one's deadline. */
class HighResolutionWait
{
public:

    HighResolutionWait();
    ~HighResolutionWait();

    HighResolutionWait(const HighResolutionWait&) = delete;
    HighResolutionWait& operator=(const HighResolutionWait&) = delete;

    HighResolutionWait(HighResolutionWait&& other) noexcept;
    HighResolutionWait& operator=(HighResolutionWait&& other) noexcept;

    void SleepFor(const std::chrono::steady_clock::duration& duration) noexcept;
    void SleepUntil(const std::chrono::steady_clock::time_point& deadline) noexcept;
    [[nodiscard]] bool HasHighResolutionSupport() const noexcept;

private:

    std::unique_ptr<HighResolutionWaitImpl> impl;

};

} // namespace foundation

#endif // !FOUNDATION_TIMING_HIGH_RESOLUTION_WAIT_HPP
