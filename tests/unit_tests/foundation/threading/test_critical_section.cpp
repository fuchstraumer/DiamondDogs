#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <vector>
#include <chrono>

#include "threading/CriticalSection.hpp"

namespace threading
{

class CriticalSectionTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        sharedCounter = 0;
    }

    CriticalSection cs;
    std::atomic<int> sharedCounter{0};
};

TEST_F(CriticalSectionTest, BasicLockUnlock)
{
    cs.lock();
    // Critical section - should be exclusive
    int temp = sharedCounter.load();
    std::this_thread::sleep_for(std::chrono::microseconds(1));
    sharedCounter.store(temp + 1);
    cs.unlock();
    
    EXPECT_EQ(sharedCounter.load(), 1);
}

TEST_F(CriticalSectionTest, RAIIGuard)
{
    // Also verifies standard library compatibility, meeting BasicLockable requirements
    {
        std::lock_guard<CriticalSection> guard(cs);
        int temp = sharedCounter.load();
        std::this_thread::sleep_for(std::chrono::microseconds(1));
        sharedCounter.store(temp + 1);
    } // cs should unlock here
    
    EXPECT_EQ(sharedCounter.load(), 1);
}

TEST_F(CriticalSectionTest, ConcurrentAccess)
{
    constexpr int k_NumThreads = 10;
    constexpr int k_IncrementsPerThread = 1000;
    
    std::vector<std::thread> threads;
    
    auto worker = [&]()
    {
        for (int i = 0; i < k_IncrementsPerThread; ++i)
        {
            std::lock_guard<CriticalSection> guard(cs);
            int temp = sharedCounter.load();
            // Simulate some work that could cause race conditions
            std::this_thread::yield();
            sharedCounter.store(temp + 1);
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
    
    // Verify no race conditions occurred
    EXPECT_EQ(sharedCounter.load(), k_NumThreads * k_IncrementsPerThread);
}

TEST_F(CriticalSectionTest, TryLock)
{
    bool acquired = cs.TryLock();
    EXPECT_TRUE(acquired);
}

TEST_F(CriticalSectionTest, TryLockRecursive)
{
    // Test try_lock functionality if available
    bool acquired = cs.TryLock();
    EXPECT_TRUE(acquired);
    
    bool reacquired = false;
    if (acquired)
    {
        // Try to acquire again from same thread - behavior depends on implementation
        // Some critical sections are recursive, others are not
        reacquired = cs.TryLock();
    }
    EXPECT_TRUE(reacquired);
}

TEST_F(CriticalSectionTest, TryLockFailure)
{
    // Create two threads, one will hold the lock and the other will try and fail to acquire it.
    std::thread t1([&]()
    {
        cs.lock();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        cs.unlock();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Ensure t1 has locked the critical section

    bool acquired = cs.TryLock();
    EXPECT_FALSE(acquired) << "Should not be able to acquire lock while another thread holds it";

    t1.join();

    acquired = cs.TryLock();
    EXPECT_TRUE(acquired) << "Should be able to acquire lock after it has been released by the other thread";
}

TEST_F(CriticalSectionTest, PerformanceBenchmark)
{
    constexpr int k_Operations = 100000;
    
    auto start = std::chrono::high_resolution_clock::now();
    size_t counter = 0;
    
    for (int i = 0; i < k_Operations; ++i)
    {
        std::lock_guard<CriticalSection> guard(cs);
        counter += 1;
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    std::cout << "CriticalSection operations: " << k_Operations 
              << " in " << duration.count() << " microseconds\n";
    std::cout << "Average: " << (duration.count() / static_cast<double>(k_Operations)) 
              << " microseconds per lock/unlock\n";
    
    EXPECT_EQ(counter, k_Operations);
}

} // namespace threading
