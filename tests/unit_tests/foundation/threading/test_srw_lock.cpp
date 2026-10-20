#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <vector>
#include <chrono>

#include "threading/SrwLock.hpp"

namespace threading
{

class SRWLockTest : public ::testing::Test
{
protected:
    void SetUp() override {
        sharedData = 0;
        readerCount = 0;
    }

    SrwLock lock;
    std::atomic<int> sharedData{0};
    std::atomic<int> readerCount{0};
};

TEST_F(SRWLockTest, ExclusiveWriteLock)
{
    lock.lock_exclusive();
    
    int temp = sharedData.load();
    std::this_thread::sleep_for(std::chrono::microseconds(1));
    sharedData.store(temp + 1);
    
    lock.unlock_exclusive();
    
    EXPECT_EQ(sharedData.load(), 1);
}

TEST_F(SRWLockTest, SharedReadLock)
{
    lock.lock_shared();
    
    // Read operation
    int value = sharedData.load();
    (void)value; // Prevent unused variable warning
    
    lock.unlock_shared();
    
    SUCCEED() << "Shared lock acquired and released successfully";
}

TEST_F(SRWLockTest, MultipleReaders)
{
    constexpr int k_NumReaders = 10;
    constexpr int k_ReadsPerReader = 1000;
    
    std::vector<std::thread> readers;
    std::atomic<int> totalReads{0};
    
    auto readerWorker = [&]()
    {
        for (int i = 0; i < k_ReadsPerReader; ++i)
        {
            lock.lock_shared();
            
            readerCount.fetch_add(1);
            int value = sharedData.load();
            (void)value; // Use the value to prevent optimization
            
            // Simulate some read work
            std::this_thread::yield();
            
            readerCount.fetch_sub(1);
            totalReads.fetch_add(1);
            
            lock.unlock_shared();
        }
    };
    
    // Launch reader threads
    for (int i = 0; i < k_NumReaders; ++i)
    {
        readers.emplace_back(readerWorker);
    }
    
    // Wait for completion
    for (auto& reader : readers)
    {
        reader.join();
    }
    
    EXPECT_EQ(totalReads.load(), k_NumReaders * k_ReadsPerReader);
    EXPECT_EQ(readerCount.load(), 0); // All readers should have finished
}

TEST_F(SRWLockTest, ReaderWriterExclusion)
{
    constexpr int k_NumReaders = 5;
    constexpr int k_NumWriters = 2;
    constexpr int k_Operations = 100;
    
    std::vector<std::thread> threads;
    std::atomic<bool> stopFlag{false};
    std::atomic<int> concurrentReaders{0};
    std::atomic<int> activeWriters{0};
    std::atomic<bool> exclusionViolated{false};
    
    auto readerWorker = [&]()
    {
        for (int i = 0; i < k_Operations && !stopFlag.load(); ++i)
        {
            lock.lock_shared();
            
            int readers = concurrentReaders.fetch_add(1) + 1;
            int writers = activeWriters.load();
            
            // Check: no writers should be active when readers are reading
            if (writers > 0)
            {
                exclusionViolated = true;
            }
            
            // Simulate read work
            std::this_thread::sleep_for(std::chrono::microseconds(1));
            
            concurrentReaders.fetch_sub(1);
            lock.unlock_shared();
            
            std::this_thread::yield();
        }
    };
    
    auto writerWorker = [&]()
    {
        for (int i = 0; i < k_Operations && !stopFlag.load(); ++i)
        {
            lock.lock_exclusive();
            
            int writers = activeWriters.fetch_add(1) + 1;
            int readers = concurrentReaders.load();
            
            // Check: no readers or other writers should be active
            if (readers > 0 || writers > 1)
            {
                exclusionViolated = true;
            }
            
            // Simulate write work
            sharedData.fetch_add(1);
            std::this_thread::sleep_for(std::chrono::microseconds(1));
            
            activeWriters.fetch_sub(1);
            lock.unlock_exclusive();
            
            std::this_thread::yield();
        }
    };
    
    // Launch threads
    for (int i = 0; i < k_NumReaders; ++i)
    {
        threads.emplace_back(readerWorker);
    }
    for (int i = 0; i < k_NumWriters; ++i)
    {
        threads.emplace_back(writerWorker);
    }
    
    // Let them run for a while
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    stopFlag = true;
    
    // Wait for completion
    for (auto& thread : threads)
    {
        thread.join();
    }
    
    EXPECT_FALSE(exclusionViolated.load()) << "Reader-writer exclusion was violated";
    EXPECT_EQ(concurrentReaders.load(), 0);
    EXPECT_EQ(activeWriters.load(), 0);
}

TEST_F(SRWLockTest, PerformanceComparison)
{
    constexpr int k_Operations = 50000;
    
    // Test exclusive lock performance
    auto startExclusive = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < k_Operations; ++i)
    {
        lock.lock_exclusive();
        sharedData.fetch_add(1);
        lock.unlock_exclusive();
    }
    auto endExclusive = std::chrono::high_resolution_clock::now();
    auto durationExclusive = std::chrono::duration_cast<std::chrono::microseconds>(endExclusive - startExclusive);
    
    // Test shared lock performance
    auto startShared = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < k_Operations; ++i)
    {
        lock.lock_shared();
        volatile int value = sharedData.load();
        (void)value;
        lock.unlock_shared();
    }
    auto endShared = std::chrono::high_resolution_clock::now();
    auto durationShared = std::chrono::duration_cast<std::chrono::microseconds>(endShared - startShared);
    
    std::cout << "Exclusive locks: " << durationExclusive.count() << " microseconds\n";
    std::cout << "Shared locks: " << durationShared.count() << " microseconds\n";
    std::cout << "Shared/Exclusive ratio: " << (static_cast<double>(durationShared.count()) / durationExclusive.count()) << "\n";
}

} // namespace threading
