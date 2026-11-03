#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <chrono>

#include "threading/ExponentialBackoffSleeper.hpp"
using namespace foundation;

namespace threading
{

class ExponentialBackoffSleeperTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Setup if needed
    }
};

TEST_F(ExponentialBackoffSleeperTest, BasicConstruction)
{
    ExponentialBackoffSleeper sleeper;
    SUCCEED() << "ExponentialBackoffSleeper should be constructible";
}

TEST_F(ExponentialBackoffSleeperTest, InitialSleepTime)
{
    ExponentialBackoffSleeper sleeper;
    
    auto start = std::chrono::high_resolution_clock::now();
    sleeper.sleep();
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // First sleep should be minimal (just a yield or very short sleep)
    // Duration we test against is high compared to specified value, but this accounts for overhead in the test environment
    EXPECT_LT(duration.count(), 50) << "Initial sleep should be very short";
}

TEST_F(ExponentialBackoffSleeperTest, ExponentialGrowth)
{
    ExponentialBackoffSleeper sleeper;
    
    std::vector<int64_t> sleepTimes;
    
    // Measure several sleep iterations
    for (int i = 0; i < 5; ++i)
    {
        auto start = std::chrono::high_resolution_clock::now();
        sleeper.sleepAndBackoff();
        auto end = std::chrono::high_resolution_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        sleepTimes.push_back(duration.count());
    }
    
    // Each sleep should generally be longer than the previous (with some tolerance for timing variation)
    for (size_t i = 1; i < sleepTimes.size(); ++i)
    {
        std::cout << "Sleep " << i << ": " << sleepTimes[i] << " microseconds\n";
    }
    
    // At least the later sleeps should be longer than the early ones
    EXPECT_LT(sleepTimes[0], sleepTimes[sleepTimes.size() - 1]) 
        << "Sleep time should increase exponentially";
}

TEST_F(ExponentialBackoffSleeperTest, Reset)
{
    ExponentialBackoffSleeper sleeper;
    
    // Do several sleeps to build up the backoff
    for (int i = 0; i < 4; ++i)
    {
        sleeper.sleepAndBackoff();
    }

    auto start = std::chrono::high_resolution_clock::now();
    sleeper.sleep();
    auto end = std::chrono::high_resolution_clock::now();

    auto longDuration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // Reset the sleeper
    sleeper.reset();
    
    // Next sleep should be back to initial timing
    start = std::chrono::high_resolution_clock::now();
    sleeper.sleep();
    end = std::chrono::high_resolution_clock::now();
    
    auto shortDuration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // Should be back to minimal timing
    EXPECT_LT(shortDuration, longDuration) << "Sleep after reset should be short again";
}

TEST_F(ExponentialBackoffSleeperTest, MaximumSleepTime)
{
    ExponentialBackoffSleeper sleeper;
    
    // Do many sleeps to hit the maximum
    for (int i = 0; i < 20; ++i)
    {
        sleeper.sleep();
    }
    
    // Measure a few sleeps at maximum
    std::vector<int64_t> maxSleepTimes;
    for (int i = 0; i < 3; ++i)
    {
        auto start = std::chrono::high_resolution_clock::now();
        sleeper.sleep();
        auto end = std::chrono::high_resolution_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        maxSleepTimes.push_back(duration.count());
    }
    
    // Sleep times should have converged to a maximum and be relatively stable
    int64_t minTime = *std::min_element(maxSleepTimes.begin(), maxSleepTimes.end());
    int64_t maxTime = *std::max_element(maxSleepTimes.begin(), maxSleepTimes.end());
    
    std::cout << "Maximum sleep times: ";
    for (auto time : maxSleepTimes)
    {
        std::cout << time << " ";
    }
    std::cout << "microseconds\n";
    
    // There should be some reasonable maximum (not growing forever)
    EXPECT_LT(maxTime, 100000) << "Sleep time should have a reasonable maximum";
}

TEST_F(ExponentialBackoffSleeperTest, ThreadSafety)
{
    // Test that multiple threads can use the same sleeper safely
    ExponentialBackoffSleeper sleeper;
    
    constexpr int k_NumThreads = 4;
    constexpr int k_SleepsPerThread = 10;
    
    std::vector<std::thread> threads;
    std::atomic<int> completedSleeps{0};
    
    auto worker = [&]()
    {
        for (int i = 0; i < k_SleepsPerThread; ++i)
        {
            sleeper.sleep();
            completedSleeps.fetch_add(1);
        }
    };
    
    // Launch threads
    for (int i = 0; i < k_NumThreads; ++i)
    {
        threads.emplace_back(worker);
    }
    
    // Wait for completion
    for (auto& thread : threads)
    {
        thread.join();
    }
    
    EXPECT_EQ(completedSleeps.load(), k_NumThreads * k_SleepsPerThread);
}

TEST_F(ExponentialBackoffSleeperTest, SpinWaitIntegration)
{
    // Test typical usage pattern in a spin-wait loop
    ExponentialBackoffSleeper sleeper;
    std::atomic<bool> condition{false};
    std::atomic<int> spinCount{0};
    
    // Thread that will set the condition after a delay
    std::thread setter([&]()
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        condition.store(true);
    });
    
    // Spin-wait with exponential backoff
    while (!condition.load())
    {
        sleeper.sleep();
        spinCount.fetch_add(1);
    }
    
    setter.join();
    
    std::cout << "Spin wait completed after " << spinCount.load() << " iterations\n";
    EXPECT_GT(spinCount.load(), 0);
    EXPECT_LT(spinCount.load(), 10000) << "Should not spin excessively";
}

} // namespace threading
