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
    
    std::vector<int64_t> sleep_times;
    
    // Measure several sleep iterations
    for (int i = 0; i < 5; ++i)
    {
        auto start = std::chrono::high_resolution_clock::now();
        sleeper.sleepAndBackoff();
        auto end = std::chrono::high_resolution_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        sleep_times.push_back(duration.count());
    }
    
    // Each sleep should generally be longer than the previous (with some tolerance for timing variation)
    for (size_t i = 1; i < sleep_times.size(); ++i)
    {
        std::cout << "Sleep " << i << ": " << sleep_times[i] << " microseconds\n";
    }
    
    // At least the later sleeps should be longer than the early ones
    EXPECT_LT(sleep_times[0], sleep_times[sleep_times.size() - 1]) 
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
    std::vector<int64_t> max_sleep_times;
    for (int i = 0; i < 3; ++i)
    {
        auto start = std::chrono::high_resolution_clock::now();
        sleeper.sleep();
        auto end = std::chrono::high_resolution_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        max_sleep_times.push_back(duration.count());
    }
    
    // Sleep times should have converged to a maximum and be relatively stable
    int64_t min_time = *std::min_element(max_sleep_times.begin(), max_sleep_times.end());
    int64_t max_time = *std::max_element(max_sleep_times.begin(), max_sleep_times.end());
    
    std::cout << "Maximum sleep times: ";
    for (auto time : max_sleep_times)
    {
        std::cout << time << " ";
    }
    std::cout << "microseconds\n";
    
    // There should be some reasonable maximum (not growing forever)
    EXPECT_LT(max_time, 100000) << "Sleep time should have a reasonable maximum";
}

TEST_F(ExponentialBackoffSleeperTest, ThreadSafety)
{
    // Test that multiple threads can use the same sleeper safely
    ExponentialBackoffSleeper sleeper;
    
    constexpr int num_threads = 4;
    constexpr int sleeps_per_thread = 10;
    
    std::vector<std::thread> threads;
    std::atomic<int> completed_sleeps{0};
    
    auto worker = [&]()
    {
        for (int i = 0; i < sleeps_per_thread; ++i)
        {
            sleeper.sleep();
            completed_sleeps.fetch_add(1);
        }
    };
    
    // Launch threads
    for (int i = 0; i < num_threads; ++i)
    {
        threads.emplace_back(worker);
    }
    
    // Wait for completion
    for (auto& thread : threads)
    {
        thread.join();
    }
    
    EXPECT_EQ(completed_sleeps.load(), num_threads * sleeps_per_thread);
}

TEST_F(ExponentialBackoffSleeperTest, SpinWaitIntegration)
{
    // Test typical usage pattern in a spin-wait loop
    ExponentialBackoffSleeper sleeper;
    std::atomic<bool> condition{false};
    std::atomic<int> spin_count{0};
    
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
        spin_count.fetch_add(1);
    }
    
    setter.join();
    
    std::cout << "Spin wait completed after " << spin_count.load() << " iterations\n";
    EXPECT_GT(spin_count.load(), 0);
    EXPECT_LT(spin_count.load(), 10000) << "Should not spin excessively";
}

} // namespace threading
