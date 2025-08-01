#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <vector>
#include <chrono>

#include "threading/srw_lock.hpp"

class SRWLockTest : public ::testing::Test
{
protected:
    void SetUp() override {
        shared_data = 0;
        reader_count = 0;
    }

    SrwLock lock;
    std::atomic<int> shared_data{0};
    std::atomic<int> reader_count{0};
};

TEST_F(SRWLockTest, ExclusiveWriteLock)
{
    lock.lock_exclusive();
    
    int temp = shared_data.load();
    std::this_thread::sleep_for(std::chrono::microseconds(1));
    shared_data.store(temp + 1);
    
    lock.unlock_exclusive();
    
    EXPECT_EQ(shared_data.load(), 1);
}

TEST_F(SRWLockTest, SharedReadLock)
{
    lock.lock_shared();
    
    // Read operation
    int value = shared_data.load();
    (void)value; // Prevent unused variable warning
    
    lock.unlock_shared();
    
    SUCCEED() << "Shared lock acquired and released successfully";
}

TEST_F(SRWLockTest, MultipleReaders)
{
    constexpr int num_readers = 10;
    constexpr int reads_per_reader = 1000;
    
    std::vector<std::thread> readers;
    std::atomic<int> total_reads{0};
    
    auto reader_worker = [&]()
    {
        for (int i = 0; i < reads_per_reader; ++i)
        {
            lock.lock_shared();
            
            reader_count.fetch_add(1);
            int value = shared_data.load();
            (void)value; // Use the value to prevent optimization
            
            // Simulate some read work
            std::this_thread::yield();
            
            reader_count.fetch_sub(1);
            total_reads.fetch_add(1);
            
            lock.unlock_shared();
        }
    };
    
    // Launch reader threads
    for (int i = 0; i < num_readers; ++i)
    {
        readers.emplace_back(reader_worker);
    }
    
    // Wait for completion
    for (auto& reader : readers)
    {
        reader.join();
    }
    
    EXPECT_EQ(total_reads.load(), num_readers * reads_per_reader);
    EXPECT_EQ(reader_count.load(), 0); // All readers should have finished
}

TEST_F(SRWLockTest, ReaderWriterExclusion)
{
    constexpr int num_readers = 5;
    constexpr int num_writers = 2;
    constexpr int operations = 100;
    
    std::vector<std::thread> threads;
    std::atomic<bool> stop_flag{false};
    std::atomic<int> concurrent_readers{0};
    std::atomic<int> active_writers{0};
    std::atomic<bool> exclusion_violated{false};
    
    auto reader_worker = [&]()
    {
        for (int i = 0; i < operations && !stop_flag.load(); ++i)
        {
            lock.lock_shared();
            
            int readers = concurrent_readers.fetch_add(1) + 1;
            int writers = active_writers.load();
            
            // Check: no writers should be active when readers are reading
            if (writers > 0)
            {
                exclusion_violated = true;
            }
            
            // Simulate read work
            std::this_thread::sleep_for(std::chrono::microseconds(1));
            
            concurrent_readers.fetch_sub(1);
            lock.unlock_shared();
            
            std::this_thread::yield();
        }
    };
    
    auto writer_worker = [&]()
    {
        for (int i = 0; i < operations && !stop_flag.load(); ++i)
        {
            lock.lock_exclusive();
            
            int writers = active_writers.fetch_add(1) + 1;
            int readers = concurrent_readers.load();
            
            // Check: no readers or other writers should be active
            if (readers > 0 || writers > 1)
            {
                exclusion_violated = true;
            }
            
            // Simulate write work
            shared_data.fetch_add(1);
            std::this_thread::sleep_for(std::chrono::microseconds(1));
            
            active_writers.fetch_sub(1);
            lock.unlock_exclusive();
            
            std::this_thread::yield();
        }
    };
    
    // Launch threads
    for (int i = 0; i < num_readers; ++i)
    {
        threads.emplace_back(reader_worker);
    }
    for (int i = 0; i < num_writers; ++i)
    {
        threads.emplace_back(writer_worker);
    }
    
    // Let them run for a while
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    stop_flag = true;
    
    // Wait for completion
    for (auto& thread : threads)
    {
        thread.join();
    }
    
    EXPECT_FALSE(exclusion_violated.load()) << "Reader-writer exclusion was violated";
    EXPECT_EQ(concurrent_readers.load(), 0);
    EXPECT_EQ(active_writers.load(), 0);
}

TEST_F(SRWLockTest, UpgradeDowngrade)
{
    // Test upgrade from shared to exclusive lock (if supported)
    // Note: Not all SRW lock implementations support upgrade/downgrade
    
    lock.lock_shared();
    
    // Try to upgrade (this might not be supported in all implementations)
    lock.unlock_shared();
    lock.lock_exclusive();
    
    shared_data.store(42);
    
    // Downgrade back to shared
    lock.unlock_exclusive();
    lock.lock_shared();
    
    EXPECT_EQ(shared_data.load(), 42);
    
    lock.unlock_shared();
}

TEST_F(SRWLockTest, PerformanceComparison)
{
    constexpr int operations = 50000;
    
    // Test exclusive lock performance
    auto start_exclusive = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < operations; ++i)
    {
        lock.lock_exclusive();
        shared_data.fetch_add(1);
        lock.unlock_exclusive();
    }
    auto end_exclusive = std::chrono::high_resolution_clock::now();
    auto duration_exclusive = std::chrono::duration_cast<std::chrono::microseconds>(end_exclusive - start_exclusive);
    
    // Test shared lock performance
    auto start_shared = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < operations; ++i)
    {
        lock.lock_shared();
        volatile int value = shared_data.load();
        (void)value;
        lock.unlock_shared();
    }
    auto end_shared = std::chrono::high_resolution_clock::now();
    auto duration_shared = std::chrono::duration_cast<std::chrono::microseconds>(end_shared - start_shared);
    
    std::cout << "Exclusive locks: " << duration_exclusive.count() << " microseconds\n";
    std::cout << "Shared locks: " << duration_shared.count() << " microseconds\n";
    std::cout << "Shared/Exclusive ratio: " << (static_cast<double>(duration_shared.count()) / duration_exclusive.count()) << "\n";
}
