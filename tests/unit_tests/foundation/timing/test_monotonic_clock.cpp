#include <gtest/gtest.h>
#include "timing/HighResolutionWait.hpp"
#include "timing/MonotonicClock.hpp"
#include <chrono>
#include <cstdint>

TEST(MonotonicClockTest, NeverGoesBackwards)
{
    int64_t previous = foundation::MonotonicNanoseconds();

    for (int32_t i = 0; i < 10000; ++i)
    {
        const int64_t now = foundation::MonotonicNanoseconds();

        ASSERT_GE(now, previous);
        previous = now;
    }
}

TEST(MonotonicClockTest, MatchesSteadyClock)
{
    const auto before = std::chrono::steady_clock::now();
    const int64_t reading = foundation::MonotonicNanoseconds();
    const auto after = std::chrono::steady_clock::now();
    const int64_t beforeNanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(before.time_since_epoch()).count();
    const int64_t afterNanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(after.time_since_epoch()).count();

    EXPECT_GE(reading, beforeNanoseconds);
    EXPECT_LE(reading, afterNanoseconds);
}

TEST(StopwatchTest, MeasuresASleep)
{
    foundation::HighResolutionWait wait;
    const foundation::Stopwatch stopwatch;

    wait.SleepFor(std::chrono::milliseconds(2));

    EXPECT_GE(stopwatch.ElapsedNanoseconds(), 1'000'000);
    EXPECT_LT(stopwatch.ElapsedMilliseconds(), 8.0);
}

TEST(StopwatchTest, RestartStartsOver)
{
    foundation::HighResolutionWait wait;
    foundation::Stopwatch stopwatch;

    wait.SleepFor(std::chrono::milliseconds(4));
    stopwatch.Restart();

    EXPECT_LT(stopwatch.ElapsedMilliseconds(), 3.0);
}
