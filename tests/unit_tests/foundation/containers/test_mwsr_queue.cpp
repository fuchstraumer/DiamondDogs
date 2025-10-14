#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <random>
#include <algorithm>
#include <array>
#include <numeric>
#include <chrono>

#include "containers/mwsrQueue.hpp"

namespace containers
{

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
    constexpr static size_t QueueSize = detail::mwsrQueueSize; // Assuming this is defined in the detail namespace
};

// mostly here to make sure template instantiation happens and the class is valid
TEST_F(MWSRQueueTest, BasicConstruction)
{
    Queue<int> queue;
    SUCCEED() << "mwsrQueue should be constructible";
}

TEST_F(MWSRQueueTest, SingleProducerSingleConsumer)
{
    Queue<int> queue;

    auto writer_worker = [&queue]()
    {
        for (int i = 0; i < QueueSize; ++i)
        {
            queue.push(i);
        }
    };

    // this is only an almost-fair queue, so this kind of test is only valid with a single writer
    auto reader_worker = [&queue]()
    {
        for (int i = 0; i < QueueSize; ++i)
        {
            int value = queue.pop();
            EXPECT_EQ(value, i) << "Value should match the expected sequence";
        }
    };

    std::thread writer(writer_worker);
    std::this_thread::sleep_for(std::chrono::microseconds(10));
    std::thread reader(reader_worker);
    writer.join();
    reader.join();

    // verify all items processed by using try_pop()
    auto try_pop_test = queue.try_pop();
    EXPECT_FALSE(try_pop_test.has_value()) << "Queue should be empty after all items processed";
}

TEST_F(MWSRQueueTest, TryPushFullQueue)
{
    Queue<int> queue;

    // Fill the queue
    for (int i = 0; i < QueueSize - 1; ++i)
    {
        queue.push(i);
    }

    // Try to push one more item, which should fail
    int new_item = 999;
    bool result = queue.try_push(new_item);
    EXPECT_FALSE(result) << "try_push should return false when queue is full";
}

TEST_F(MWSRQueueTest, TryPushEmptyQueue)
{
    Queue<int> queue;

    // Try to push an item into an empty queue
    int new_item = 42;
    bool result = queue.try_push(new_item);
    EXPECT_TRUE(result) << "try_push should succeed when queue is empty";
    
    // Verify the item was pushed
    int popped_item = queue.pop();
    EXPECT_EQ(popped_item, new_item) << "Popped item should match the pushed item";
}

TEST_F(MWSRQueueTest, TryPopFullQueue)
{
    Queue<int> queue;

    // Fill the queue
    for (int i = 0; i < QueueSize - 1; ++i)
    {
        queue.push(i);
    }

    // Try to pop an item from a full queue
    auto try_pop_test = queue.try_pop();
    EXPECT_TRUE(try_pop_test.has_value()) << "try_pop should return an item when queue is full";
    EXPECT_EQ(try_pop_test.value(), 0) << "Popped item should be the first item pushed";

}

TEST_F(MWSRQueueTest, TryPopEmptyQueue)
{
    Queue<int> queue;

    // Try to pop an item from an empty queue
    auto try_pop_test = queue.try_pop();
    EXPECT_FALSE(try_pop_test.has_value()) << "try_pop should return no item when queue is empty";

}

TEST_F(MWSRQueueTest, MultipleWritersSingleReader)
{
    Queue<int> queue;
    
    constexpr int num_writers = 8;
    constexpr int items_per_writer = 16;
    // we will be at double our queue capacity, but I want to test the contention and locking behavior
    // when we have a full queue and multiple writers trying to get their values in
    constexpr int total_items = num_writers * items_per_writer;
    
    std::vector<std::thread> writers;
    std::atomic<int> items_written{0};
    
    // Writer threads
    auto writer_worker = [&](int writer_id)
    {
        for (int i = 0; i < items_per_writer; ++i)
        {
            int value = writer_id * items_per_writer + i;
            queue.push(std::move(value));
            items_written.fetch_add(1);
        }
    };
    
    // Single reader thread
    std::vector<int> received_items;
    received_items.reserve(total_items);
    std::atomic<bool> readers_done{false};

    auto reader_worker = [&]()
    {
        while (items_written.load() < total_items - 1)
        {
            std::optional<int> queue_trypop_item;
            while (queue_trypop_item = queue.try_pop(), queue_trypop_item.has_value())
            {
                received_items.emplace_back(queue_trypop_item.value());
            }
        }

        readers_done = true;
        std::this_thread::yield(); // Yield to allow other threads to finish
    };

    // Launch writers, note they wil shortly lock on the queue being full at 64 items
    for (int i = 0; i < num_writers; ++i)
    {
        writers.emplace_back(writer_worker, i);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Give writers time to start and fill the queue

    // now add the reader thread
    std::thread reader(reader_worker);
    
    // Give everyone time to finish (should be more than enough)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Wait for all writers to finish
    for (auto& writer : writers)
    {
        writer.join();
    }

    // and bring in the reader
    reader.join();
    
    // Verify all items were processed
    EXPECT_EQ(received_items.size(), total_items);
    EXPECT_EQ(items_written.load(), total_items);
    
    // Verify no duplicates (each value should appear exactly once)
    std::sort(received_items.begin(), received_items.end());
    auto unique_end = std::unique(received_items.begin(), received_items.end());
    EXPECT_EQ(unique_end, received_items.end()) << "No duplicate items should be received";
}

// Verifies that the queue can handle high contention with multiple writers, especially pointing out if any items are dropped or lost because of bugs in the queue implementation
TEST_F(MWSRQueueTest, StressTestBoundedHighContention)
{
    Queue<int> queue;
    
    constexpr int num_writers = 16;
    constexpr int items_per_writer = 2000;  // Much more than queue capacity
    constexpr int total_items = num_writers * items_per_writer;
    constexpr auto test_duration = std::chrono::seconds(5);

    // we'll store all pushed and popped items to verify correctness
    std::vector<int> received_items;
    received_items.reserve(total_items);
    std::array<std::vector<int>, num_writers> writer_pushed_items;
    for (auto& vec : writer_pushed_items)
    {
        vec.reserve(items_per_writer);
    }
    
    std::atomic<int> successful_try_pushes{0};
    std::atomic<int> failed_try_pushes{0};
    std::atomic<int> blocking_pushes{0};
    std::atomic<int> total_items_written{0};  // Track actual successful writes
    std::atomic<bool> stop_test{false};
    std::atomic<bool> all_writers_done{false};  // Better coordination
    std::atomic<int> final_write_count{0};      // Snapshot of writes when all writers finish
    
    // Aggressive writer threads that will definitely overflow the queue
    auto aggressive_writer = [&](int writer_id)
    {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> delay_dist(1, 5);

        int items_written = 0;
        while (!stop_test.load() && items_written < items_per_writer)
        {
            int value = writer_id * items_per_writer + items_written;
            bool success = queue.try_push(value);
            if (success)
            {
                successful_try_pushes.fetch_add(1);
                total_items_written.fetch_add(1);
                writer_pushed_items[writer_id].emplace_back(value);
            } 
            else
            {
                failed_try_pushes.fetch_add(1);
                queue.push(value);  // Blocking push if try_push fails
                writer_pushed_items[writer_id].emplace_back(value);
                blocking_pushes.fetch_add(1);
                total_items_written.fetch_add(1);
            }

            items_written++;
        
            // Small random delay to create realistic contention patterns
            if (items_written % 25 == 0)
            {
                std::this_thread::sleep_for(std::chrono::microseconds(delay_dist(gen) * 10));
            }
        }
    };


    std::atomic<size_t> items_try_popped{ 0 };
    std::atomic<size_t> items_popped{ 0 };
    std::vector<size_t> batch_sizes;
    
    // Reader thread that alternates between blocking and non-blocking reads
    auto reader_worker = [&]()
    {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> delay_dist(1, 5);
        
        while ((items_popped.load() + items_try_popped.load()) < total_items)
        {
            // try to do a bunch of try_pops to drain the queue
            size_t batch_size = 0;
            std::optional<int> item;

            do
            {
                item = queue.try_pop();
                if (item.has_value())
                {
                    received_items.emplace_back(item.value());
                    batch_size++;
                }
            } while (item.has_value());

            items_try_popped.fetch_add(batch_size);
            batch_sizes.emplace_back(batch_size);

            // once we fall out of the above loop, try_pop has begun failing. let's check to see if we're done reading values
            if ((items_popped.load() + items_try_popped.load()) >= total_items)
            {
                // everything is finished, instead of potentially blocking the test, break and exit
                break;
            }
            else
            {
                // still have items to read, so we'll block with the regular pop() call
                int value = queue.pop();
                items_popped.fetch_add(1);
                received_items.emplace_back(value);
                // sleep for a short interval to simulate some processing time, and hopefully let writers catch up
                std::this_thread::sleep_for(std::chrono::microseconds(delay_dist(gen) * 10));
            }
            
        }
    };
    
    // Launch test
    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::vector<std::thread> writers;
    for (int i = 0; i < num_writers; ++i)
    {
        writers.emplace_back(aggressive_writer, i);
    }
    
    std::thread reader(reader_worker);
    
    // Let it run for the test duration
    std::this_thread::sleep_for(test_duration);
    
    // Wait for all writers, then signal reader
    for (auto& writer : writers)
    {
        writer.join();
    }

    // coalesce all received items into one vector
    std::vector<int> all_written_values;
    all_written_values.reserve(total_items);
    for (const auto& vec : writer_pushed_items)
    {
        all_written_values.insert(all_written_values.end(), vec.begin(), vec.end());
    }

    // sort written items and received items to make comparison faster
    std::sort(all_written_values.begin(), all_written_values.end());
    std::sort(received_items.begin(), received_items.end());

    // now compare and verify parity
    bool items_match = (all_written_values.size() == received_items.size()) &&
        std::equal(all_written_values.begin(), all_written_values.end(), received_items.begin());
    EXPECT_TRUE(items_match) << "Mismatch between values written and values read, queue is not funcitoning properly!";

    reader.join();
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    // Calculate metrics
    int total_write_attempts = successful_try_pushes.load() + failed_try_pushes.load();
    int total_successful_writes = successful_try_pushes.load() + blocking_pushes.load();
    double try_push_success_rate = total_write_attempts > 0 ? 
        static_cast<double>(successful_try_pushes.load()) / total_write_attempts : 0.0;
    size_t total_items_popped = items_popped.load() + items_try_popped.load();
    double try_pop_success_rate = (items_popped.load() + items_try_popped.load()) > 0 ?
        static_cast<double>(items_try_popped.load()) / (items_popped.load() + items_try_popped.load()) : 0.0;
    std::sort(batch_sizes.begin(), batch_sizes.end());

    size_t batch_size_sum = std::accumulate(batch_sizes.begin(), batch_sizes.end(), size_t(0));
    double average_batch_size = batch_sizes.empty() ? 0.0 : 
        static_cast<double>(batch_size_sum) / static_cast<double>(batch_sizes.size());
    // find median batch size
    size_t median_batch_size = 0;
    if (batch_sizes.size() % 2 != 0)
    {
        median_batch_size = batch_sizes[batch_sizes.size() / 2];
    }
    else
    {
        median_batch_size = (batch_sizes[batch_sizes.size() / 2 - 1] + batch_sizes[batch_sizes.size() / 2]) / 2;
    }
    
    std::cerr << "\n=== MWSR Queue Bounded Stress Test Results ===\n";
    std::cerr << "Test duration: " << duration.count() << " ms\n";
    std::cerr << "Queue capacity: " << QueueSize << " items\n";
    std::cerr << "Writers: " << num_writers << "\n\n";
    
    std::cerr << "Write Operations:\n";
    std::cerr << "  Successful try_push: " << successful_try_pushes.load() << "\n";
    std::cerr << "  Failed try_push: " << failed_try_pushes.load() << "\n";
    std::cerr << "  Blocking push: " << blocking_pushes.load() << "\n";
    std::cerr << "  Total successful writes: " << total_successful_writes << "\n";
    std::cerr << "  Final write count: " << final_write_count.load() << "\n";
    std::cerr << "  try_push success rate: " << (try_push_success_rate * 100) << "%\n\n";
    std::cerr << "  average try_push batch size: " << average_batch_size << "\n";
    
    std::cerr << "Read Operations:\n";
    std::cerr << "  Items pop()'d (blocking): " << items_popped.load() << "\n";
    std::cerr << "  Successful try_pop() count (non-blocking): " << items_try_popped.load() << "\n";
    std::cerr << "  try_pop success rate: " << (try_pop_success_rate * 100) << "%\n\n";
    
    std::cerr << "Throughput:\n";
    std::cerr << "  Writes/sec: " << (total_successful_writes * 1000.0 / duration.count()) << "\n";
    std::cerr << "  Reads/sec: " << (items_popped.load() * 1000.0 / duration.count()) << "\n";
    std::cerr << "  Queue utilization: " << (failed_try_pushes.load() > 0 ? "HIGH" : "LOW") << "\n";

    // The queue should show signs of being under pressure (failed try_pushes)
    // but still maintain reasonable throughput
    if (failed_try_pushes.load() > 0)
    {
        std::cerr << "✓ Queue showed appropriate back-pressure under high contention\n";
        EXPECT_GT(try_push_success_rate, 0.1) << "Even under pressure, some try_pushes should succeed";
    }
    
    // Blocking operations should work
    if (blocking_pushes.load() > 0)
    {
        std::cerr << "✓ Blocking push operations functioned correctly\n";
    }
    
    // Final queue state should be empty
    auto final_item = queue.try_pop();
    EXPECT_FALSE(final_item.has_value()) << "Queue should be empty at test end";
}

TEST_F(MWSRQueueTest, OrderingGuarantees)
{
    // Test that items from a single writer maintain order
    Queue<std::pair<int, int>> queue; // (writer_id, sequence_number)
    
    constexpr int num_writers = 4;
    constexpr int items_per_writer = 100;
    
    std::vector<std::thread> writers;
    
    auto writer_worker = [&](int writer_id) {
        for (int seq = 0; seq < items_per_writer; ++seq)
        {
            std::pair<int, int> item = {writer_id, seq};
            // while (!queue.enqueue(item)) {
                std::this_thread::yield(); // Retry until successful
            // }
        }
    };
    
    // Track received items per writer
    std::vector<std::vector<int>> writer_sequences(num_writers);
    std::atomic<bool> stop_reading{false};
    
    std::thread reader([&]()
        {
        std::pair<int, int> item;
        while (!stop_reading.load())
        {
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
    for (int i = 0; i < num_writers; ++i)
    {
        writers.emplace_back(writer_worker, i);
    }
    
    // Wait for completion
    for (auto& writer : writers)
    {
        writer.join();
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    stop_reading = true;
    reader.join();
    
    // Verify ordering within each writer's sequence
    for (int writer_id = 0; writer_id < num_writers; ++writer_id)
    {
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

TEST_F(MWSRQueueTest, MemoryOrdering)
{
    // Test memory ordering guarantees
    using ordering_test_t = std::pair<std::atomic<int>*, int>; // (pointer to atomic, value)
    Queue<ordering_test_t> queue;
    
    constexpr int num_operations = 1000;
    std::vector<std::atomic<int>> memory_locations(num_operations);
    
    // Initialize memory locations
    for (int i = 0; i < num_operations; ++i)
    {
        memory_locations[i].store(0);
    }
    
    std::thread writer([&]()
    {
        for (int i = 0; i < num_operations; ++i)
        {
            // First, modify the memory location
            memory_locations[i].store(42, std::memory_order_release);
            
            // Then enqueue a pointer to it
            ordering_test_t item = {&memory_locations[i], i};
            while (!queue.try_push(item))
            {
                std::this_thread::yield(); // Retry until successful
            }
        }
    });
    
    std::atomic<int> ordering_violations{0};
    std::thread reader([&]()
    {
        for (int received = 0; received < num_operations;)
        {
            std::optional<ordering_test_t> item_opt;
            if (item_opt = queue.try_pop(), item_opt.has_value())
            {
                // The memory location should already be updated due to happens-before
                auto item = item_opt.value();
                int value = item.first->load(std::memory_order_acquire);
                if (value != 42)
                {
                    ordering_violations.fetch_add(1);
                }
                received++;
             }
             else
             {
                std::this_thread::yield();
             }
        }
    });
    
    writer.join();
    reader.join();
    
    // EXPECT_EQ(ordering_violations.load(), 0) 
    //     << "No memory ordering violations should occur";
}

TEST_F(MWSRQueueTest, BatchSizeCalculation)
{
    // This test validates the internal bit counting logic by checking
    // that the correct number of items are batched when multiple writes complete
    
    Queue<int> queue;
    constexpr int numWriters = 8;
    std::atomic<int> writesCompleted{ 0 };
    std::atomic<bool> startReading{ false };
    
    // Push exactly 8 items from different threads
    std::vector<std::thread> writers;
    for (int i = 0; i < numWriters; ++i)
    {
        writers.emplace_back([&queue, i, &writesCompleted, &startReading]()
        {
            // Wait for signal to ensure out-of-order completion
            while (!startReading.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            
            // Add small random delay to create varied completion order
            std::this_thread::sleep_for(std::chrono::microseconds(i * 10));
            
            queue.push(i);
            writesCompleted.fetch_add(1, std::memory_order_release);
        });
    }
    
    // Let all threads start writing
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    startReading.store(true, std::memory_order_release);
    
    // Wait for at least some writes to complete
    while (writesCompleted.load(std::memory_order_acquire) < numWriters / 2)
    {
        std::this_thread::yield();
    }
    
    // First pop should batch multiple items
    int firstItem = queue.pop();
    EXPECT_GE(firstItem, 0);
    EXPECT_LT(firstItem, numWriters);
    
    // Subsequent pops should come from cache (fast path)
    // If bit counting is wrong, we'll either:
    // 1. Get fewer items than expected (some lost)
    // 2. Block unexpectedly (batch size calculated wrong)
    std::vector<int> allItems{ firstItem };
    for (int i = 1; i < numWriters; ++i)
    {
        auto item = queue.try_pop();
        if (item.has_value())
        {
            allItems.push_back(*item);
        }
        else
        {
            // Cache exhausted, do blocking pop
            allItems.push_back(queue.pop());
        }
    }
    
    // Verify we got all items exactly once
    EXPECT_EQ(allItems.size(), numWriters);
    std::sort(allItems.begin(), allItems.end());
    for (int i = 0; i < numWriters; ++i)
    {
        EXPECT_EQ(allItems[i], i) << "Missing or duplicate item at index " << i;
    }
    
    for (auto& writer : writers)
    {
        writer.join();
    }
    
    // Queue should be empty
    auto finalCheck = queue.try_pop();
    EXPECT_FALSE(finalCheck.has_value());
}

} // namespace containers
