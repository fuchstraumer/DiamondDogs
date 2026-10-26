#include <gtest/gtest.h>
#include "threading/FrameLimiter.hpp"
#include <chrono>

TEST(FrameLimiterTest, DefaultFramerate)
{
    foundation::FrameLimiter limiter;
    EXPECT_DOUBLE_EQ(limiter.TargetFramerate(), 60.0);
}

TEST(FrameLimiterTest, InvalidFramerateFallback)
{
    foundation::FrameLimiter limiterZero(0.0);
    EXPECT_DOUBLE_EQ(limiterZero.TargetFramerate(), 60.0);

    foundation::FrameLimiter limiterNegative(-10.0);
    EXPECT_DOUBLE_EQ(limiterNegative.TargetFramerate(), 60.0);

    foundation::FrameLimiter limiterOne(1.0);
    EXPECT_DOUBLE_EQ(limiterOne.TargetFramerate(), 60.0);

    foundation::FrameLimiter limiter(144.0);
    EXPECT_DOUBLE_EQ(limiter.TargetFramerate(), 144.0);
    limiter.SetTargetFramerate(0.5);
    EXPECT_DOUBLE_EQ(limiter.TargetFramerate(), 60.0);
    limiter.SetTargetFramerate(-60.0);
    EXPECT_DOUBLE_EQ(limiter.TargetFramerate(), 60.0);
}

TEST(FrameLimiterTest, TargetIntervalCalculation)
{
    foundation::FrameLimiter limiter(120.0);
    EXPECT_DOUBLE_EQ(limiter.TargetFramerate(), 120.0);
    EXPECT_NEAR(limiter.TargetFrameInterval().count(), 1000.0 / 120.0, 1e-6);

    limiter.SetTargetFramerate(30.0);
    EXPECT_DOUBLE_EQ(limiter.TargetFramerate(), 30.0);
    EXPECT_NEAR(limiter.TargetFrameInterval().count(), 1000.0 / 30.0, 1e-6);

    limiter.SetTargetFramerate(60.0);
    EXPECT_DOUBLE_EQ(limiter.TargetFramerate(), 60.0);
    EXPECT_NEAR(limiter.TargetFrameInterval().count(), 1000.0 / 60.0, 1e-6);
}

TEST(FrameLimiterTest, FramePacingAt200Hz)
{
    foundation::FrameLimiter limiter(200.0);
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 3; ++i)
    {
        limiter.WaitNextFrame();
    }
    const auto end = std::chrono::steady_clock::now();
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    EXPECT_GE(elapsedMs.count(), 10);
}
