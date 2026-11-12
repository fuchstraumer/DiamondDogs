#include <gtest/gtest.h>
#include "timing/HighResolutionWait.hpp"
#include <chrono>

TEST(HighResolutionWaitTest, ConstructionAndSupport)
{
    foundation::HighResolutionWait wait;

    // Windows 10 1803 and later, and modern Linux, have a high resolution timer
    EXPECT_TRUE(wait.HasHighResolutionSupport());
}

TEST(HighResolutionWaitTest, SleepForTwoMilliseconds)
{
    foundation::HighResolutionWait wait;
    const auto start = std::chrono::steady_clock::now();

    wait.SleepFor(std::chrono::milliseconds(2));

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);

    EXPECT_GE(elapsed.count(), 1);
    EXPECT_LT(elapsed.count(), 8);
}

TEST(HighResolutionWaitTest, SleepUntilTwoMilliseconds)
{
    foundation::HighResolutionWait wait;
    const auto start = std::chrono::steady_clock::now();

    wait.SleepUntil(start + std::chrono::milliseconds(2));

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);

    EXPECT_GE(elapsed.count(), 1);
    EXPECT_LT(elapsed.count(), 8);
}
