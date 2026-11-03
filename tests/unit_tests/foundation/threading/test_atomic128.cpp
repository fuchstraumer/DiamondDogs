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
        testData = CasData128(0, 0);
    }

    CasData128 testData;
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
    atomic128 atomicVal;
    CasData128 expected(0, 0);
    CasData128 desired(42, 84);
    
    // Should succeed since initial value is 0,0
    bool result = atomicVal.compare_exchange_strong(expected, desired);
    EXPECT_TRUE(result);
    
    // Verify the value was set
    CasData128 current = atomicVal.load();
    EXPECT_EQ(current, desired);
}

TEST_F(Atomic128Test, AtomicCASFailure)
{
    atomic128 atomicVal;
    atomicVal.store(CasData128(10, 20));
    
    CasData128 expected(5, 15);  // Wrong expected value
    CasData128 desired(42, 84);
    
    // Should fail since expected doesn't match current
    bool result = atomicVal.compare_exchange_strong(expected, desired);
    EXPECT_FALSE(result);
    
    // expected should now contain the actual value
    EXPECT_EQ(expected, CasData128(10, 20));
    
    // Value should remain unchanged
    CasData128 current = atomicVal.load();
    EXPECT_EQ(current, CasData128(10, 20));
}

// Concurrency stress test - multiple threads trying to increment
TEST_F(Atomic128Test, ConcurrentIncrementStressTest)
{
    atomic128 atomicCounter;
    atomicCounter.store(CasData128(0, 0));
    
    constexpr int k_NumThreads = 8;
    constexpr int k_IncrementsPerThread = 1000;
    
    std::vector<std::thread> threads;
    std::atomic<int> successfulIncrements{0};
    
    auto incrementWorker = [&]()
    {
        for (int i = 0; i < k_IncrementsPerThread; ++i)
        {
            CasData128 current = atomicCounter.load();
            CasData128 next;
            
            do
            {
                next = CasData128(current.low + 1, current.high);
            } while (!atomicCounter.compare_exchange_weak(current, next));
            
            successfulIncrements.fetch_add(1);
        }
    };
    
    // Launch threads
    for (int i = 0; i < k_NumThreads; ++i)
    {
        threads.emplace_back(incrementWorker);
    }
    
    // Wait for completion
    for (auto& thread : threads)
    {
        thread.join();
    }
    
    // Verify final count
    CasData128 finalValue = atomicCounter.load();
    EXPECT_EQ(finalValue.low, k_NumThreads * k_IncrementsPerThread);
    EXPECT_EQ(finalValue.high, 0);
    EXPECT_EQ(successfulIncrements.load(), k_NumThreads * k_IncrementsPerThread);
}

// ABA problem test - verifies that CAS detects value changes even if final value is the same
TEST_F(Atomic128Test, ABAProtectionTest)
{
    atomic128 atomicVal;
    atomicVal.store(CasData128(100, 1));  // value=100, version=1
    
    std::atomic<bool> thread2CanProceed{false};
    std::atomic<bool> thread1Done{false};
    
    std::thread thread1([&]()
    {
        CasData128 expected = atomicVal.load();  // 100, 1
        
        // Signal thread2 to modify the value
        thread2CanProceed = true;
        
        // Wait for thread2 to finish
        while (!thread1Done.load())
        {
            std::this_thread::yield();
        }
        
        // Try to CAS with old expected value - should fail due to version change
        CasData128 desired(200, expected.high);
        bool result = atomicVal.compare_exchange_strong(expected, desired);
        
        EXPECT_FALSE(result) << "CAS should fail due to ABA protection via version counter";
        EXPECT_EQ(expected.low, 100);  // Value back to original
        EXPECT_EQ(expected.high, 3);   // But version changed
    });
    
    std::thread thread2([&]()
    {
        // Wait for thread1 to load the initial value
        while (!thread2CanProceed.load())
        {
            std::this_thread::yield();
        }
        
        // Change value to something else (A -> B)
        CasData128 current = atomicVal.load();
        CasData128 temp(999, current.high + 1);
        while (!atomicVal.compare_exchange_weak(current, temp)) {}
        
        // Change it back to original value but with incremented version (B -> A')
        current = atomicVal.load();
        CasData128 backToOriginal(100, current.high + 1);
        while (!atomicVal.compare_exchange_weak(current, backToOriginal)) {}
        
        thread1Done = true;
    });
    
    thread1.join();
    thread2.join();
}

// Performance benchmark test
TEST_F(Atomic128Test, PerformanceBenchmark)
{
    atomic128 atomicVal;
    atomicVal.store(CasData128(0, 0));
    
    constexpr int k_Operations = 100000;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < k_Operations; ++i)
    {
        CasData128 current = atomicVal.load();
        CasData128 next(current.low + 1, current.high);
        while (!atomicVal.compare_exchange_weak(current, next))
        {
            next = CasData128(current.low + 1, current.high);
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // This is more of an informational test - print timing info
    std::cout << "128-bit CAS operations: " << k_Operations 
              << " in " << duration.count() << " microseconds\n";
    std::cout << "Average: " << (duration.count() / static_cast<double>(k_Operations)) 
              << " microseconds per operation\n";
    
    // Verify correctness
    CasData128 final = atomicVal.load();
    EXPECT_EQ(final.low, k_Operations);
}

#endif // _MSC_VER

} // namespace foundation::tests::threading
