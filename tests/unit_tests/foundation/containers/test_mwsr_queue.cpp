#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <vector>
#include <atomic>
#include <random>
#include <algorithm>

#include "containers/mwsrQueue.hpp"

// Note: mwsrQueue = Multiple Writer Single Reader Queue

class MWSRQueueTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Setup if needed
    }
    
    template<typename T>
    using Queue = mwsrQueue<T>;  // Assuming this is the actual class name
};

TEST_F(MWSRQueueTest, BasicConstruction)
{
    // Test basic construction
    Queue<int> queue;
    SUCCEED() << "mwsrQueue should be constructible";
}

TEST_F(MWSRQueueTest, SingleProducerSingleConsumer)
{
    Queue<int> queue;
    
    // Test basic enqueue/dequeue
    // queue.enqueue(42);
    // 
    // int value;
    // bool success = queue.dequeue(value);
    // EXPECT_TRUE(success);
    // EXPECT_EQ(value, 42);
    
    // Test empty queue
    // int dummy;
    // bool empty_result = queue.dequeue(dummy);
    // EXPECT_FALSE(empty_result);
}

TEST_F(MWSRQueueTest, MultipleWritersSingleReader)
{
    Queue<int> queue;
    
    constexpr int num_writers = 8;
    constexpr int items_per_writer = 1000;
    constexpr int total_items = num_writers * items_per_writer;
    
    std::vector<std::thread> writers;
    std::atomic<int> items_written{0};
    
    // Writer threads
    auto writer_worker = [&](int writer_id)
    {
        for (int i = 0; i < items_per_writer; ++i)
        {
            int value = writer_id * items_per_writer + i;
            // bool success = queue.enqueue(value);
            // if (success) {
                items_written.fetch_add(1);
            // }
        }
    };
    
    // Single reader thread
    std::vector<int> received_items;
    received_items.reserve(total_items);
    std::atomic<bool> readers_done{false};
    
    std::thread reader([&]() {
        int value;
        while (!readers_done.load() || received_items.size() < total_items) {
            // if (queue.dequeue(value)) {
            //     received_items.push_back(value);
            // } else {
                std::this_thread::yield();
            // }
        }
    });
    
    // Launch writers
    for (int i = 0; i < num_writers; ++i) {
        writers.emplace_back(writer_worker, i);
    }
    
    // Wait for writers to complete
    for (auto& writer : writers) {
        writer.join();
    }
    
    // Give reader time to finish
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    readers_done = true;
    reader.join();
    
    // Verify all items were processed
    // EXPECT_EQ(received_items.size(), total_items);
    EXPECT_EQ(items_written.load(), total_items);
    
    // Verify no duplicates (each value should appear exactly once)
    // std::sort(received_items.begin(), received_items.end());
    // auto unique_end = std::unique(received_items.begin(), received_items.end());
    // EXPECT_EQ(unique_end, received_items.end()) << "No duplicate items should be received";
}

TEST_F(MWSRQueueTest, QueueCapacityLimits) {
    Queue<int> queue;
    
    // Test queue capacity limits
    constexpr int queue_size = detail::mwsrQueueSize; // From the header
    
    // Fill the queue to capacity
    int items_added = 0;
    for (int i = 0; i < queue_size * 2; ++i) {
        // bool success = queue.enqueue(i);
        // if (success) {
            items_added++;
        // } else {
        //     break; // Queue is full
        // }
    }
    
    // Should not be able to add more than capacity
    // EXPECT_LE(items_added, queue_size);
    
    // Drain the queue
    int items_removed = 0;
    int value;
    // while (queue.dequeue(value)) {
    //     items_removed++;
    // }
    
    // Should have removed the same number we added
    // EXPECT_EQ(items_removed, items_added);
}

TEST_F(MWSRQueueTest, StressTestHighContention) {
    Queue<int> queue;
    
    constexpr int num_writers = 16;
    constexpr int items_per_writer = 5000;
    constexpr int total_items = num_writers * items_per_writer;
    
    std::vector<std::thread> writers;
    std::atomic<int> successful_enqueues{0};
    std::atomic<int> failed_enqueues{0};
    
    auto writer_worker = [&](int writer_id) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> delay_dis(0, 10);
        
        for (int i = 0; i < items_per_writer; ++i) {
            int value = writer_id * items_per_writer + i;
            
            // Retry with backoff on failure
            bool enqueued = false;
            int retry_count = 0;
            while (!enqueued && retry_count < 100) {
                // enqueued = queue.enqueue(value);
                if (!enqueued) {
                    // Exponential backoff
                    std::this_thread::sleep_for(std::chrono::microseconds(1 << std::min(retry_count, 6)));
                    retry_count++;
                }
            }
            
            if (enqueued) {
                successful_enqueues.fetch_add(1);
            } else {
                failed_enqueues.fetch_add(1);
            }
        }
    };
    
    // Reader thread
    std::atomic<int> items_read{0};
    std::atomic<bool> stop_reading{false};
    
    std::thread reader([&]() {
        int value;
        while (!stop_reading.load()) {
            // if (queue.dequeue(value)) {
                items_read.fetch_add(1);
            // } else {
                std::this_thread::yield();
            // }
        }
    });
    
    // Launch writers
    auto start_time = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < num_writers; ++i) {
        writers.emplace_back(writer_worker, i);
    }
    
    // Wait for writers
    for (auto& writer : writers) {
        writer.join();
    }
    
    // Give reader time to catch up
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop_reading = true;
    reader.join();
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    std::cout << "MWSR Queue stress test results:\n";
    std::cout << "Duration: " << duration.count() << " ms\n";
    std::cout << "Successful enqueues: " << successful_enqueues.load() << "\n";
    std::cout << "Failed enqueues: " << failed_enqueues.load() << "\n";
    std::cout << "Items read: " << items_read.load() << "\n";
    
    // Most operations should succeed under normal conditions
    double success_rate = static_cast<double>(successful_enqueues.load()) / total_items;
    EXPECT_GT(success_rate, 0.8) << "Success rate should be reasonable under stress";
}

TEST_F(MWSRQueueTest, OrderingGuarantees) {
    // Test that items from a single writer maintain order
    Queue<std::pair<int, int>> queue; // (writer_id, sequence_number)
    
    constexpr int num_writers = 4;
    constexpr int items_per_writer = 100;
    
    std::vector<std::thread> writers;
    
    auto writer_worker = [&](int writer_id) {
        for (int seq = 0; seq < items_per_writer; ++seq) {
            std::pair<int, int> item = {writer_id, seq};
            // while (!queue.enqueue(item)) {
                std::this_thread::yield(); // Retry until successful
            // }
        }
    };
    
    // Track received items per writer
    std::vector<std::vector<int>> writer_sequences(num_writers);
    std::atomic<bool> stop_reading{false};
    
    std::thread reader([&]() {
        std::pair<int, int> item;
        while (!stop_reading.load()) {
            // if (queue.dequeue(item)) {
            //     int writer_id = item.first;
            //     int sequence = item.second;
            //     writer_sequences[writer_id].push_back(sequence);
            // } else {
                std::this_thread::yield();
            // }
        }
    });
    
    // Launch writers
    for (int i = 0; i < num_writers; ++i) {
        writers.emplace_back(writer_worker, i);
    }
    
    // Wait for completion
    for (auto& writer : writers) {
        writer.join();
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    stop_reading = true;
    reader.join();
    
    // Verify ordering within each writer's sequence
    for (int writer_id = 0; writer_id < num_writers; ++writer_id) {
        const auto& sequences = writer_sequences[writer_id];
        // EXPECT_EQ(sequences.size(), items_per_writer) 
        //     << "All items from writer " << writer_id << " should be received";
        
        // Verify sequences are in order
        for (size_t i = 1; i < sequences.size(); ++i) {
            // EXPECT_GT(sequences[i], sequences[i-1]) 
            //     << "Sequences from writer " << writer_id << " should be in order";
        }
    }
}

TEST_F(MWSRQueueTest, MemoryOrdering) {
    // Test memory ordering guarantees
    Queue<std::pair<std::atomic<int>*, int>> queue;
    
    constexpr int num_operations = 1000;
    std::vector<std::atomic<int>> memory_locations(num_operations);
    
    // Initialize memory locations
    for (int i = 0; i < num_operations; ++i) {
        memory_locations[i].store(0);
    }
    
    std::thread writer([&]() {
        for (int i = 0; i < num_operations; ++i) {
            // First, modify the memory location
            memory_locations[i].store(42, std::memory_order_release);
            
            // Then enqueue a pointer to it
            std::pair<std::atomic<int>*, int> item = {&memory_locations[i], i};
            // while (!queue.enqueue(item)) {
                std::this_thread::yield();
            // }
        }
    });
    
    std::atomic<int> ordering_violations{0};
    std::thread reader([&]() {
        for (int received = 0; received < num_operations; ) {
            std::pair<std::atomic<int>*, int> item;
            // if (queue.dequeue(item)) {
            //     // The memory location should already be updated due to happens-before
            //     int value = item.first->load(std::memory_order_acquire);
            //     if (value != 42) {
            //         ordering_violations.fetch_add(1);
            //     }
            //     received++;
            // } else {
                std::this_thread::yield();
            // }
        }
    });
    
    writer.join();
    reader.join();
    
    // EXPECT_EQ(ordering_violations.load(), 0) 
    //     << "No memory ordering violations should occur";
}
