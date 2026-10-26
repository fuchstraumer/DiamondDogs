#include <gtest/gtest.h>
#include "threading/PrecisionTimer.hpp"
#include <chrono>

TEST(PrecisionTimerTest, ConstructionAndSupport)
{
    foundation::PrecisionTimer timer;
    // On Windows 10 1803+ and modern Linux, high resolution support is expected to be available
    EXPECT_TRUE(timer.HasHighResolutionSupport());
}

TEST(PrecisionTimerTest, SleepForTwoMilliseconds)
{
    foundation::PrecisionTimer timer;
    const auto start = std::chrono::steady_clock::now();
    timer.SleepFor(std::chrono::milliseconds(2));
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    EXPECT_GE(elapsed.count(), 1);
    EXPECT_LT(elapsed.count(), 8);
}

TEST(PrecisionTimerTest, SleepUntilTwoMilliseconds)
{
    foundation::PrecisionTimer timer;
    const auto start = std::chrono::steady_clock::now();
    timer.SleepUntil(start + std::chrono::milliseconds(2));
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    EXPECT_GE(elapsed.count(), 1);
    EXPECT_LT(elapsed.count(), 8);
}
