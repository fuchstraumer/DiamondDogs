#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <vector>
#include <atomic>
#include <array>
#include <random>

#include "threading/mcas.hpp"
//
//class MCASTest : public ::testing::Test
//{
//protected:
//    void SetUp() override {
//        // Initialize test data
//        for (int i = 0; i < test_values.size(); ++i)
//        {
//            test_values[i].store(i);
//        }
//    }
//
//    static constexpr size_t NUM_TEST_VALUES = 10;
//    std::array<std::atomic<int>, NUM_TEST_VALUES> test_values;
//};
//
//// Test basic MCAS construction and structure
//TEST_F(MCASTest, BasicConstruction)
//{
//    MCAS mcas;
//    SUCCEED() << "MCAS should be constructible";
//}
//
//TEST_F(MCASTest, RowConstruction) {
//    MCAS::Row row;
//    EXPECT_EQ(row.address, nullptr);
//    EXPECT_EQ(row.expectedValue, nullptr);
//    EXPECT_EQ(row.newValue, nullptr);
//    EXPECT_EQ(row.helperPtr, nullptr);
//}
//
//// Test single-word CAS (should work like regular CAS)
//TEST_F(MCASTest, SingleWordCAS)
//{
//    MCAS mcas;
//    
//    int expected_val = 0;
//    int new_val = 42;
//    
//    MCAS::Row row;
//    row.address = &test_values[0];
//    row.expectedValue = &expected_val;
//    row.newValue = &new_val;
//    
//    // This test assumes MCAS::Invoke works with single row
//    // bool result = mcas.Invoke(&row, &row + 1);
//    // EXPECT_TRUE(result);
//    // EXPECT_EQ(test_values[0].load(), 42);
//}
//
//// Test two-word CAS operation
//TEST_F(MCASTest, TwoWordCAS)
//{
//    MCAS mcas;
//    
//    int expected_val1 = 0, expected_val2 = 1;
//    int new_val1 = 100, new_val2 = 200;
//    
//    std::array<MCAS::Row, 2> rows;
//    
//    rows[0].address = &test_values[0];
//    rows[0].expectedValue = &expected_val1;
//    rows[0].newValue = &new_val1;
//    
//    rows[1].address = &test_values[1];
//    rows[1].expectedValue = &expected_val2;
//    rows[1].newValue = &new_val2;
//    
//    // bool result = mcas.Invoke(rows.data(), rows.data() + 2);
//    // EXPECT_TRUE(result);
//    // EXPECT_EQ(test_values[0].load(), 100);
//    // EXPECT_EQ(test_values[1].load(), 200);
//}
//
//// Test MCAS failure when one expected value doesn't match
//TEST_F(MCASTest, TwoWordCASFailure)
//{
//    MCAS mcas;
//    
//    int expected_val1 = 0, expected_val2 = 999; // Wrong expected value
//    int new_val1 = 100, new_val2 = 200;
//    
//    std::array<MCAS::Row, 2> rows;
//    
//    rows[0].address = &test_values[0];
//    rows[0].expectedValue = &expected_val1;
//    rows[0].newValue = &new_val1;
//    
//    rows[1].address = &test_values[1];
//    rows[1].expectedValue = &expected_val2;  // This should be 1, not 999
//    rows[1].newValue = &new_val2;
//    
//    // bool result = mcas.Invoke(rows.data(), rows.data() + 2);
//    // EXPECT_FALSE(result);
//    // Values should remain unchanged
//    // EXPECT_EQ(test_values[0].load(), 0);
//    // EXPECT_EQ(test_values[1].load(), 1);
//}
//
//// Test multi-word CAS with more locations
//TEST_F(MCASTest, MultiWordCAS)
//{
//    MCAS mcas;
//    
//    constexpr size_t num_words = 5;
//    std::array<int, num_words> expected_vals = {0, 1, 2, 3, 4};
//    std::array<int, num_words> new_vals = {10, 20, 30, 40, 50};
//    std::array<MCAS::Row, num_words> rows;
//    
//    for (size_t i = 0; i < num_words; ++i) {
//        rows[i].address = &test_values[i];
//        rows[i].expectedValue = &expected_vals[i];
//        rows[i].newValue = &new_vals[i];
//    }
//    
//    // bool result = mcas.Invoke(rows.data(), rows.data() + num_words);
//    // EXPECT_TRUE(result);
//    
//    // for (size_t i = 0; i < num_words; ++i) {
//    //     EXPECT_EQ(test_values[i].load(), new_vals[i]);
//    // }
//}
//
//// Concurrent MCAS stress test
//TEST_F(MCASTest, ConcurrentMCASStressTest)
//{
//    constexpr int num_threads = 8;
//    constexpr int operations_per_thread = 100;
//    
//    std::vector<std::thread> threads;
//    std::atomic<int> successful_operations{0};
//    std::atomic<int> failed_operations{0};
//    
//    auto mcas_worker = [&](int thread_id)
//    {
//        MCAS mcas;
//        std::random_device rd;
//        std::mt19937 gen(rd());
//        std::uniform_int_distribution<> index_dis(0, NUM_TEST_VALUES - 1);
//        std::uniform_int_distribution<> value_dis(1000, 9999);
//        
//        for (int op = 0; op < operations_per_thread; ++op)
//        {
//            // Try to atomically swap two random locations
//            int idx1 = index_dis(gen);
//            int idx2 = index_dis(gen);
//            if (idx1 == idx2) continue; // Skip same index
//            
//            int expected1 = test_values[idx1].load();
//            int expected2 = test_values[idx2].load();
//            int new1 = value_dis(gen);
//            int new2 = value_dis(gen);
//            
//            std::array<MCAS::Row, 2> rows;
//            rows[0].address = &test_values[idx1];
//            rows[0].expectedValue = &expected1;
//            rows[0].newValue = &new1;
//            
//            rows[1].address = &test_values[idx2];
//            rows[1].expectedValue = &expected2;
//            rows[1].newValue = &new2;
//            
//            // bool result = mcas.Invoke(rows.data(), rows.data() + 2);
//            bool result = false; // Placeholder
//            
//            if (result)
//            {
//                successful_operations.fetch_add(1);
//            } else
//            {
//                failed_operations.fetch_add(1);
//            }
//        }
//    };
//    
//    // Launch threads
//    for (int i = 0; i < num_threads; ++i)
//    {
//        threads.emplace_back(mcas_worker, i);
//    }
//    
//    // Wait for completion
//    for (auto& thread : threads)
//    {
//        thread.join();
//    }
//    
//    int total_operations = successful_operations.load() + failed_operations.load();
//    EXPECT_GT(total_operations, 0);
//    
//    std::cout << "MCAS operations - Successful: " << successful_operations.load() 
//              << ", Failed: " << failed_operations.load() << std::endl;
//}
//
//// Test ABA prevention in MCAS
//TEST_F(MCASTest, ABAPreventionTest)
//{
//    // This test would verify that MCAS properly handles ABA scenarios
//    // where values change and then change back during the operation
//    
//    MCAS mcas;
//    
//    std::atomic<bool> thread1_loaded{false};
//    std::atomic<bool> thread2_done{false};
//    
//    std::thread thread1([&]()
//    {
//        int expected1 = 0, expected2 = 1;
//        int new1 = 100, new2 = 200;
//        
//        std::array<MCAS::Row, 2> rows;
//        rows[0].address = &test_values[0];
//        rows[0].expectedValue = &expected1;
//        rows[0].newValue = &new1;
//        
//        rows[1].address = &test_values[1];
//        rows[1].expectedValue = &expected2;
//        rows[1].newValue = &new2;
//        
//        thread1_loaded = true;
//        
//        // Wait for thread2 to do its ABA manipulation
//        while (!thread2_done.load())
//        {
//            std::this_thread::yield();
//        }
//        
//        // This MCAS should fail due to ABA prevention mechanisms
//        // bool result = mcas.Invoke(rows.data(), rows.data() + 2);
//        // EXPECT_FALSE(result) << "MCAS should detect ABA and fail";
//    });
//    
//    std::thread thread2([&]()
//    {
//        while (!thread1_loaded.load()) 
//        {
//            std::this_thread::yield();
//        }
//        
//        // Perform ABA: change values and change them back
//        int temp1 = test_values[0].exchange(999);
//        int temp2 = test_values[1].exchange(888);
//        
//        std::this_thread::yield();
//        
//        test_values[0].store(temp1);  // Back to original
//        test_values[1].store(temp2);  // Back to original
//        
//        thread2_done = true;
//    });
//    
//    thread1.join();
//    thread2.join();
//}
//
//// Performance comparison test
//TEST_F(MCASTest, PerformanceComparison)
//{
//    constexpr int operations = 10000;
//    
//    // Test single CAS performance
//    auto start_single = std::chrono::high_resolution_clock::now();
//    for (int i = 0; i < operations; ++i)
//    {
//        int expected = test_values[0].load();
//        test_values[0].compare_exchange_weak(expected, expected + 1);
//    }
//    auto end_single = std::chrono::high_resolution_clock::now();
//    auto duration_single = std::chrono::duration_cast<std::chrono::microseconds>(end_single - start_single);
//    
//    // Test MCAS performance (single word)
//    auto start_mcas = std::chrono::high_resolution_clock::now();
//    for (int i = 0; i < operations; ++i)
//    {
//        MCAS mcas;
//        int expected = test_values[1].load();
//        int new_val = expected + 1;
//        
//        MCAS::Row row;
//        row.address = &test_values[1];
//        row.expectedValue = &expected;
//        row.newValue = &new_val;
//        
//        // mcas.Invoke(&row, &row + 1);
//    }
//    auto end_mcas = std::chrono::high_resolution_clock::now();
//    auto duration_mcas = std::chrono::duration_cast<std::chrono::microseconds>(end_mcas - start_mcas);
//    
//    std::cout << "Single CAS: " << duration_single.count() << " microseconds\n";
//    std::cout << "MCAS (1 word): " << duration_mcas.count() << " microseconds\n";
//    std::cout << "MCAS overhead: " << (duration_mcas.count() / static_cast<double>(duration_single.count())) << "x\n";
//}
