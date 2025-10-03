#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <random>

#include "threading/atomic128.hpp"

namespace threading
{

class Atomic128Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Reset test data
        test_data = CasData128(0, 0);
    }

    CasData128 test_data;
};

// Basic functionality tests
TEST_F(Atomic128Test, DefaultConstruction)
{
    CasData128 data;
    EXPECT_EQ(data.low, 0u);
    EXPECT_EQ(data.high, 0u);
}

TEST_F(Atomic128Test, ValueConstruction)
{
    CasData128 data(0x1234567890ABCDEF, 0xFEDCBA0987654321);
    EXPECT_EQ(data.low, 0x1234567890ABCDEF);
    EXPECT_EQ(data.high, 0xFEDCBA0987654321);
}

TEST_F(Atomic128Test, EqualityOperator)
{
    CasData128 data1(100, 200);
    CasData128 data2(100, 200);
    CasData128 data3(100, 201);
    
    EXPECT_TRUE(data1 == data2);
    EXPECT_FALSE(data1 == data3);
    EXPECT_TRUE(data1 != data3);
}

// Test alignment requirements
TEST_F(Atomic128Test, AlignmentRequirement)
{
    CasData128 data;
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&data) % 16, 0) 
        << "cas_data128_t must be 16-byte aligned for atomic operations";
}

#ifdef _MSC_VER
// Test atomic operations (only available on MSVC currently)
TEST_F(Atomic128Test, AtomicCASBasic)
{
    atomic128 atomic_val;
    CasData128 expected(0, 0);
    CasData128 desired(42, 84);
    
    // Should succeed since initial value is 0,0
    bool result = atomic_val.compare_exchange_strong(expected, desired);
    EXPECT_TRUE(result);
    
    // Verify the value was set
    CasData128 current = atomic_val.load();
    EXPECT_EQ(current, desired);
}

TEST_F(Atomic128Test, AtomicCASFailure)
{
    atomic128 atomic_val;
    atomic_val.store(CasData128(10, 20));
    
    CasData128 expected(5, 15);  // Wrong expected value
    CasData128 desired(42, 84);
    
    // Should fail since expected doesn't match current
    bool result = atomic_val.compare_exchange_strong(expected, desired);
    EXPECT_FALSE(result);
    
    // expected should now contain the actual value
    EXPECT_EQ(expected, CasData128(10, 20));
    
    // Value should remain unchanged
    CasData128 current = atomic_val.load();
    EXPECT_EQ(current, CasData128(10, 20));
}

// Concurrency stress test - multiple threads trying to increment
TEST_F(Atomic128Test, ConcurrentIncrementStressTest)
{
    atomic128 atomic_counter;
    atomic_counter.store(CasData128(0, 0));
    
    constexpr int num_threads = 8;
    constexpr int increments_per_thread = 1000;
    
    std::vector<std::thread> threads;
    std::atomic<int> successful_increments{0};
    
    auto increment_worker = [&]()
    {
        for (int i = 0; i < increments_per_thread; ++i)
        {
            CasData128 current = atomic_counter.load();
            CasData128 next;
            
            do
            {
                next = CasData128(current.low + 1, current.high);
            } while (!atomic_counter.compare_exchange_weak(current, next));
            
            successful_increments.fetch_add(1);
        }
    };
    
    // Launch threads
    for (int i = 0; i < num_threads; ++i)
    {
        threads.emplace_back(increment_worker);
    }
    
    // Wait for completion
    for (auto& thread : threads)
    {
        thread.join();
    }
    
    // Verify final count
    CasData128 final_value = atomic_counter.load();
    EXPECT_EQ(final_value.low, num_threads * increments_per_thread);
    EXPECT_EQ(final_value.high, 0);
    EXPECT_EQ(successful_increments.load(), num_threads * increments_per_thread);
}

// ABA problem test - verifies that CAS detects value changes even if final value is the same
TEST_F(Atomic128Test, ABAProtectionTest)
{
    atomic128 atomic_val;
    atomic_val.store(CasData128(100, 1));  // value=100, version=1
    
    std::atomic<bool> thread2_can_proceed{false};
    std::atomic<bool> thread1_done{false};
    
    std::thread thread1([&]()
    {
        CasData128 expected = atomic_val.load();  // 100, 1
        
        // Signal thread2 to modify the value
        thread2_can_proceed = true;
        
        // Wait for thread2 to finish
        while (!thread1_done.load())
        {
            std::this_thread::yield();
        }
        
        // Try to CAS with old expected value - should fail due to version change
        CasData128 desired(200, expected.high);
        bool result = atomic_val.compare_exchange_strong(expected, desired);
        
        EXPECT_FALSE(result) << "CAS should fail due to ABA protection via version counter";
        EXPECT_EQ(expected.low, 100);  // Value back to original
        EXPECT_EQ(expected.high, 3);   // But version changed
    });
    
    std::thread thread2([&]()
    {
        // Wait for thread1 to load the initial value
        while (!thread2_can_proceed.load())
        {
            std::this_thread::yield();
        }
        
        // Change value to something else (A -> B)
        CasData128 current = atomic_val.load();
        CasData128 temp(999, current.high + 1);
        while (!atomic_val.compare_exchange_weak(current, temp)) {}
        
        // Change it back to original value but with incremented version (B -> A')
        current = atomic_val.load();
        CasData128 back_to_original(100, current.high + 1);
        while (!atomic_val.compare_exchange_weak(current, back_to_original)) {}
        
        thread1_done = true;
    });
    
    thread1.join();
    thread2.join();
}

// Performance benchmark test
TEST_F(Atomic128Test, PerformanceBenchmark)
{
    atomic128 atomic_val;
    atomic_val.store(CasData128(0, 0));
    
    constexpr int operations = 100000;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < operations; ++i)
    {
        CasData128 current = atomic_val.load();
        CasData128 next(current.low + 1, current.high);
        while (!atomic_val.compare_exchange_weak(current, next))
        {
            next = CasData128(current.low + 1, current.high);
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // This is more of an informational test - print timing info
    std::cout << "128-bit CAS operations: " << operations 
              << " in " << duration.count() << " microseconds\n";
    std::cout << "Average: " << (duration.count() / static_cast<double>(operations)) 
              << " microseconds per operation\n";
    
    // Verify correctness
    CasData128 final = atomic_val.load();
    EXPECT_EQ(final.low, operations);
}

#endif // _MSC_VER

} // namespace foundation::tests::threading
