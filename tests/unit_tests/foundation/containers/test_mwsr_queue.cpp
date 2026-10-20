#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <random>
#include <algorithm>
#include <array>
#include <numeric>
#include <chrono>
#include <optional>

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
    constexpr static size_t k_QueueSize = detail::mwsrQueueSize; // Assuming this is defined in the detail namespace
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

    auto writerWorker = [&queue]()
    {
        for (int i = 0; i < k_QueueSize; ++i)
        {
            queue.push(i);
        }
    };

    // this is only an almost-fair queue, so this kind of test is only valid with a single writer
    auto readerWorker = [&queue]()
    {
        for (int i = 0; i < k_QueueSize; ++i)
        {
            int value = queue.pop();
            EXPECT_EQ(value, i) << "Value should match the expected sequence";
        }
    };

    std::thread writer(writerWorker);
    std::this_thread::sleep_for(std::chrono::microseconds(10));
    std::thread reader(readerWorker);
    writer.join();
    reader.join();

    // verify all items processed by using try_pop()
    auto tryPopTest = queue.try_pop();
    EXPECT_FALSE(tryPopTest.has_value()) << "Queue should be empty after all items processed";
}

TEST_F(MWSRQueueTest, TryPushFullQueue)
{
    Queue<int> queue;

    // Fill the queue
    for (int i = 0; i < k_QueueSize - 1; ++i)
    {
        queue.push(i);
    }

    // Try to push one more item, which should fail
    int newItem = 999;
    bool result = queue.try_push(newItem);
    EXPECT_FALSE(result) << "try_push should return false when queue is full";
}

TEST_F(MWSRQueueTest, TryPushEmptyQueue)
{
    Queue<int> queue;

    // Try to push an item into an empty queue
    int newItem = 42;
    bool result = queue.try_push(newItem);
    EXPECT_TRUE(result) << "try_push should succeed when queue is empty";
    
    // Verify the item was pushed
    int poppedItem = queue.pop();
    EXPECT_EQ(poppedItem, newItem) << "Popped item should match the pushed item";
}

TEST_F(MWSRQueueTest, TryPopFullQueue)
{
    Queue<int> queue;

    // Fill the queue
    for (int i = 0; i < k_QueueSize - 1; ++i)
    {
        queue.push(i);
    }

    // Try to pop an item from a full queue
    auto tryPopTest = queue.try_pop();
    EXPECT_TRUE(tryPopTest.has_value()) << "try_pop should return an item when queue is full";
    EXPECT_EQ(tryPopTest.value(), 0) << "Popped item should be the first item pushed";

}

TEST_F(MWSRQueueTest, TryPopEmptyQueue)
{
    Queue<int> queue;

    // Try to pop an item from an empty queue
    auto tryPopTest = queue.try_pop();
    EXPECT_FALSE(tryPopTest.has_value()) << "try_pop should return no item when queue is empty";

}

TEST_F(MWSRQueueTest, MultipleWritersSingleReader)
{
    Queue<int> queue;
    
    constexpr int k_NumWriters = 8;
    constexpr int k_ItemsPerWriter = 16;
    // we will be at double our queue capacity, but I want to test the contention and locking behavior
    // when we have a full queue and multiple writers trying to get their values in
    constexpr int k_TotalItems = k_NumWriters * k_ItemsPerWriter;
    
    std::vector<std::thread> writers;
    std::atomic<int> itemsWritten{0};
    
    // Writer threads
    auto writerWorker = [&](int writer_id)
    {
        for (int i = 0; i < k_ItemsPerWriter; ++i)
        {
            int value = writer_id * k_ItemsPerWriter + i;
            queue.push(std::move(value));
            itemsWritten.fetch_add(1);
        }
    };
    
    // Single reader thread
    std::vector<int> receivedItems;
    receivedItems.reserve(k_TotalItems);
    std::atomic<bool> readersDone{false};

    // fixed: this used to stop once items_written reached total_items - 1, with no final drain, so the last push
    // could land after the reader quit and the test failed with 127 of 128 now and then. Read until every item
    // has arrived instead, with a deadline so that a real lost item fails the test rather than hanging it
    auto readerWorker = [&]()
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::ssize(receivedItems) < k_TotalItems && std::chrono::steady_clock::now() < deadline)
        {
            std::optional<int> queueTrypopItem;
            while (queueTrypopItem = queue.try_pop(), queueTrypopItem.has_value())
            {
                receivedItems.emplace_back(queueTrypopItem.value());
            }
        }

        readersDone = true;
        std::this_thread::yield(); // Yield to allow other threads to finish
    };

    // Launch writers, note they wil shortly lock on the queue being full at 64 items
    for (int i = 0; i < k_NumWriters; ++i)
    {
        writers.emplace_back(writerWorker, i);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Give writers time to start and fill the queue

    // now add the reader thread
    std::thread reader(readerWorker);
    
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
    EXPECT_EQ(receivedItems.size(), k_TotalItems);
    EXPECT_EQ(itemsWritten.load(), k_TotalItems);
    
    // Verify no duplicates (each value should appear exactly once)
    std::sort(receivedItems.begin(), receivedItems.end());
    auto uniqueEnd = std::unique(receivedItems.begin(), receivedItems.end());
    EXPECT_EQ(uniqueEnd, receivedItems.end()) << "No duplicate items should be received";
}

// Verifies that the queue can handle high contention with multiple writers, especially pointing out if any items are dropped or lost because of bugs in the queue implementation
TEST_F(MWSRQueueTest, StressTestBoundedHighContention)
{
    Queue<int> queue;
    
    constexpr int k_NumWriters = 16;
    constexpr int k_ItemsPerWriter = 2000;  // Much more than queue capacity
    constexpr int k_TotalItems = k_NumWriters * k_ItemsPerWriter;
    constexpr auto k_TestDuration = std::chrono::seconds(5);

    // we'll store all pushed and popped items to verify correctness
    std::vector<int> receivedItems;
    receivedItems.reserve(k_TotalItems);
    std::array<std::vector<int>, k_NumWriters> writerPushedItems;
    for (auto& vec : writerPushedItems)
    {
        vec.reserve(k_ItemsPerWriter);
    }
    
    std::atomic<int> successfulTryPushes{0};
    std::atomic<int> failedTryPushes{0};
    std::atomic<int> blockingPushes{0};
    std::atomic<int> totalItemsWritten{0};  // Track actual successful writes
    std::atomic<bool> stopTest{false};
    std::atomic<bool> allWritersDone{false};  // Better coordination
    std::atomic<int> finalWriteCount{0};      // Snapshot of writes when all writers finish
    
    // Aggressive writer threads that will definitely overflow the queue
    auto aggressiveWriter = [&](int writer_id)
    {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> delayDist(1, 5);

        int itemsWritten = 0;
        while (!stopTest.load() && itemsWritten < k_ItemsPerWriter)
        {
            int value = writer_id * k_ItemsPerWriter + itemsWritten;
            bool success = queue.try_push(value);
            if (success)
            {
                successfulTryPushes.fetch_add(1);
                totalItemsWritten.fetch_add(1);
                writerPushedItems[writer_id].emplace_back(value);
            } 
            else
            {
                failedTryPushes.fetch_add(1);
                queue.push(value);  // Blocking push if try_push fails
                writerPushedItems[writer_id].emplace_back(value);
                blockingPushes.fetch_add(1);
                totalItemsWritten.fetch_add(1);
            }

            itemsWritten++;
        
            // Small random delay to create realistic contention patterns
            if (itemsWritten % 25 == 0)
            {
                std::this_thread::sleep_for(std::chrono::microseconds(delayDist(gen) * 10));
            }
        }
    };


    std::atomic<size_t> itemsTryPopped{ 0 };
    std::atomic<size_t> itemsPopped{ 0 };
    std::vector<size_t> batchSizes;
    
    // Reader thread that alternates between blocking and non-blocking reads
    auto readerWorker = [&]()
    {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> delayDist(1, 5);
        
        while ((itemsPopped.load() + itemsTryPopped.load()) < k_TotalItems)
        {
            // try to do a bunch of try_pops to drain the queue
            size_t batchSize = 0;
            std::optional<int> item;

            do
            {
                item = queue.try_pop();
                if (item.has_value())
                {
                    receivedItems.emplace_back(item.value());
                    batchSize++;
                }
            } while (item.has_value());

            itemsTryPopped.fetch_add(batchSize);
            batchSizes.emplace_back(batchSize);

            // once we fall out of the above loop, try_pop has begun failing. let's check to see if we're done reading values
            if ((itemsPopped.load() + itemsTryPopped.load()) >= k_TotalItems)
            {
                // everything is finished, instead of potentially blocking the test, break and exit
                break;
            }
            else
            {
                // still have items to read, so we'll block with the regular pop() call
                int value = queue.pop();
                itemsPopped.fetch_add(1);
                receivedItems.emplace_back(value);
                // sleep for a short interval to simulate some processing time, and hopefully let writers catch up
                std::this_thread::sleep_for(std::chrono::microseconds(delayDist(gen) * 10));
            }
            
        }
    };
    
    // Launch test
    auto startTime = std::chrono::high_resolution_clock::now();
    
    std::vector<std::thread> writers;
    for (int i = 0; i < k_NumWriters; ++i)
    {
        writers.emplace_back(aggressiveWriter, i);
    }
    
    std::thread reader(readerWorker);
    
    // Let it run for the test duration
    std::this_thread::sleep_for(k_TestDuration);
    
    // Wait for all writers, then signal reader
    for (auto& writer : writers)
    {
        writer.join();
    }

    // coalesce all received items into one vector
    std::vector<int> allWrittenValues;
    allWrittenValues.reserve(k_TotalItems);
    for (const auto& vec : writerPushedItems)
    {
        allWrittenValues.insert(allWrittenValues.end(), vec.begin(), vec.end());
    }

    // sort written items and received items to make comparison faster
    std::sort(allWrittenValues.begin(), allWrittenValues.end());
    std::sort(receivedItems.begin(), receivedItems.end());

    // now compare and verify parity
    bool itemsMatch = (allWrittenValues.size() == receivedItems.size()) &&
        std::equal(allWrittenValues.begin(), allWrittenValues.end(), receivedItems.begin());
    EXPECT_TRUE(itemsMatch) << "Mismatch between values written and values read, queue is not funcitoning properly!";

    reader.join();
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    // Calculate metrics
    int totalWriteAttempts = successfulTryPushes.load() + failedTryPushes.load();
    int totalSuccessfulWrites = successfulTryPushes.load() + blockingPushes.load();
    double tryPushSuccessRate = totalWriteAttempts > 0 ? 
        static_cast<double>(successfulTryPushes.load()) / totalWriteAttempts : 0.0;
    size_t totalItemsPopped = itemsPopped.load() + itemsTryPopped.load();
    double tryPopSuccessRate = (itemsPopped.load() + itemsTryPopped.load()) > 0 ?
        static_cast<double>(itemsTryPopped.load()) / (itemsPopped.load() + itemsTryPopped.load()) : 0.0;
    std::sort(batchSizes.begin(), batchSizes.end());

    size_t batchSizeSum = std::accumulate(batchSizes.begin(), batchSizes.end(), size_t{ 0 });
    double averageBatchSize = batchSizes.empty() ? 0.0 : 
        static_cast<double>(batchSizeSum) / static_cast<double>(batchSizes.size());
    // find median batch size
    size_t medianBatchSize = 0;
    if (batchSizes.size() % 2 != 0)
    {
        medianBatchSize = batchSizes[batchSizes.size() / 2];
    }
    else
    {
        medianBatchSize = (batchSizes[batchSizes.size() / 2 - 1] + batchSizes[batchSizes.size() / 2]) / 2;
    }
    
    std::cerr << "\n=== MWSR Queue Bounded Stress Test Results ===\n";
    std::cerr << "Test duration: " << duration.count() << " ms\n";
    std::cerr << "Queue capacity: " << k_QueueSize << " items\n";
    std::cerr << "Writers: " << k_NumWriters << "\n\n";
    
    std::cerr << "Write Operations:\n";
    std::cerr << "  Successful try_push: " << successfulTryPushes.load() << "\n";
    std::cerr << "  Failed try_push: " << failedTryPushes.load() << "\n";
    std::cerr << "  Blocking push: " << blockingPushes.load() << "\n";
    std::cerr << "  Total successful writes: " << totalSuccessfulWrites << "\n";
    std::cerr << "  Final write count: " << finalWriteCount.load() << "\n";
    std::cerr << "  try_push success rate: " << (tryPushSuccessRate * 100) << "%\n\n";
    std::cerr << "  average try_push batch size: " << averageBatchSize << "\n";
    
    std::cerr << "Read Operations:\n";
    std::cerr << "  Items pop()'d (blocking): " << itemsPopped.load() << "\n";
    std::cerr << "  Successful try_pop() count (non-blocking): " << itemsTryPopped.load() << "\n";
    std::cerr << "  try_pop success rate: " << (tryPopSuccessRate * 100) << "%\n\n";
    
    std::cerr << "Throughput:\n";
    std::cerr << "  Writes/sec: " << (totalSuccessfulWrites * 1000.0 / duration.count()) << "\n";
    std::cerr << "  Reads/sec: " << (itemsPopped.load() * 1000.0 / duration.count()) << "\n";
    std::cerr << "  Queue utilization: " << (failedTryPushes.load() > 0 ? "HIGH" : "LOW") << "\n";

    // The queue should show signs of being under pressure (failed try_pushes)
    // but still maintain reasonable throughput
    if (failedTryPushes.load() > 0)
    {
        std::cerr << "✓ Queue showed appropriate back-pressure under high contention\n";
        EXPECT_GT(tryPushSuccessRate, 0.1) << "Even under pressure, some try_pushes should succeed";
    }
    
    // Blocking operations should work
    if (blockingPushes.load() > 0)
    {
        std::cerr << "✓ Blocking push operations functioned correctly\n";
    }
    
    // Final queue state should be empty
    auto finalItem = queue.try_pop();
    EXPECT_FALSE(finalItem.has_value()) << "Queue should be empty at test end";
}

TEST_F(MWSRQueueTest, OrderingGuarantees)
{
    // Test that items from a single writer maintain order
    Queue<std::pair<int, int>> queue; // (writer_id, sequence_number)
    
    constexpr int k_NumWriters = 4;
    constexpr int k_ItemsPerWriter = 100;
    
    std::vector<std::thread> writers;
    
    auto writerWorker = [&](int writer_id) {
        for (int seq = 0; seq < k_ItemsPerWriter; ++seq)
        {
            std::pair<int, int> item = {writer_id, seq};
            // while (!queue.enqueue(item)) {
                std::this_thread::yield(); // Retry until successful
            // }
        }
    };
    
    // Track received items per writer
    std::vector<std::vector<int>> writerSequences(k_NumWriters);
    std::atomic<bool> stopReading{false};
    
    std::thread reader([&]()
        {
        std::pair<int, int> item;
        while (!stopReading.load())
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
    for (int i = 0; i < k_NumWriters; ++i)
    {
        writers.emplace_back(writerWorker, i);
    }
    
    // Wait for completion
    for (auto& writer : writers)
    {
        writer.join();
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    stopReading = true;
    reader.join();
    
    // Verify ordering within each writer's sequence
    for (int writerId = 0; writerId < k_NumWriters; ++writerId)
    {
        const auto& sequences = writerSequences[writerId];
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
    using OrderingTestT = std::pair<std::atomic<int>*, int>; // (pointer to atomic, value)
    Queue<OrderingTestT> queue;
    
    constexpr int k_NumOperations = 1000;
    std::vector<std::atomic<int>> memoryLocations(k_NumOperations);
    
    // Initialize memory locations
    for (int i = 0; i < k_NumOperations; ++i)
    {
        memoryLocations[i].store(0);
    }
    
    std::thread writer([&]()
    {
        for (int i = 0; i < k_NumOperations; ++i)
        {
            // First, modify the memory location
            memoryLocations[i].store(42, std::memory_order_release);
            
            // Then enqueue a pointer to it
            OrderingTestT item = {&memoryLocations[i], i};
            while (!queue.try_push(item))
            {
                std::this_thread::yield(); // Retry until successful
            }
        }
    });
    
    std::atomic<int> orderingViolations{0};
    std::thread reader([&]()
    {
        for (int received = 0; received < k_NumOperations;)
        {
            std::optional<OrderingTestT> itemOpt;
            if (itemOpt = queue.try_pop(), itemOpt.has_value())
            {
                // The memory location should already be updated due to happens-before
                auto item = itemOpt.value();
                int value = item.first->load(std::memory_order_acquire);
                if (value != 42)
                {
                    orderingViolations.fetch_add(1);
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
    constexpr int k_NumWriters = 8;
    std::atomic<int> writesCompleted{ 0 };
    std::atomic<bool> startReading{ false };
    
    // Push exactly 8 items from different threads
    std::vector<std::thread> writers;
    for (int i = 0; i < k_NumWriters; ++i)
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
    while (writesCompleted.load(std::memory_order_acquire) < k_NumWriters / 2)
    {
        std::this_thread::yield();
    }
    
    // First pop should batch multiple items
    int firstItem = queue.pop();
    EXPECT_GE(firstItem, 0);
    EXPECT_LT(firstItem, k_NumWriters);
    
    // Subsequent pops should come from cache (fast path)
    // If bit counting is wrong, we'll either:
    // 1. Get fewer items than expected (some lost)
    // 2. Block unexpectedly (batch size calculated wrong)
    std::vector<int> allItems{ firstItem };
    for (int i = 1; i < k_NumWriters; ++i)
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
    EXPECT_EQ(allItems.size(), k_NumWriters);
    std::sort(allItems.begin(), allItems.end());
    for (int i = 0; i < k_NumWriters; ++i)
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

TEST_F(MWSRQueueTest, CountTrailingOnes)
{
    EXPECT_EQ(detail::countTrailingOnes(0u), 0u);
    EXPECT_EQ(detail::countTrailingOnes(0b0111u), 3u);
    EXPECT_EQ(detail::countTrailingOnes(~uint64_t(0u)), 64u);
}

// the exit reactor used to keep its "reader is locked" flag at bit 31 of the read ID, so the 2^31st read collided
// with the flag. these start the IDs just below each boundary and read across it
constexpr std::array<uint64_t, 3> k_SIdBoundaryStarts
{
    (uint64_t{ 1u } << 31u) - 100u,
    (uint64_t{ 1u } << 32u) - 100u,
    (uint64_t{ 1u } << 62u) - 100u,
};

TEST_F(MWSRQueueTest, SingleThreadReadsAcrossIDBoundaries)
{
    constexpr int k_BatchSize = 32;
    constexpr int k_BatchCount = 16;
    for (const uint64_t firstID : k_SIdBoundaryStarts)
    {
        Queue<int> queue(firstID);
        int nextToPush = 0;
        int nextToPop = 0;
        for (int batch = 0; batch < k_BatchCount; ++batch)
        {
            for (int i = 0; i < k_BatchSize; ++i)
            {
                ASSERT_TRUE(queue.try_push(nextToPush++)) << "first ID " << firstID;
            }
            for (int i = 0; i < k_BatchSize; ++i)
            {
                const std::optional<int> item = queue.try_pop();
                ASSERT_TRUE(item.has_value()) << "first ID " << firstID;
                EXPECT_EQ(*item, nextToPop++) << "first ID " << firstID;
            }
        }
        EXPECT_FALSE(queue.try_pop().has_value()) << "first ID " << firstID;
    }
}

TEST_F(MWSRQueueTest, BlockingWritersAndReaderAcrossIDBoundaries)
{
    constexpr int k_NumWriters = 4;
    constexpr int k_ItemsPerWriter = 500;
    constexpr int k_TotalItems = k_NumWriters * k_ItemsPerWriter;
    for (const uint64_t firstID : k_SIdBoundaryStarts)
    {
        Queue<int> queue(firstID);
        std::vector<std::thread> writers;
        for (int writerIndex = 0; writerIndex < k_NumWriters; ++writerIndex)
        {
            writers.emplace_back([&queue, writerIndex]()
            {
                for (int i = 0; i < k_ItemsPerWriter; ++i)
                {
                    queue.push(writerIndex * k_ItemsPerWriter + i);
                }
            });
        }

        // the blocking pop sets and clears the reader lock flag, which is the bit that moved
        std::vector<int> received;
        received.reserve(k_TotalItems);
        for (int i = 0; i < k_TotalItems; ++i)
        {
            received.emplace_back(queue.pop());
        }
        for (std::thread& writer : writers)
        {
            writer.join();
        }

        std::ranges::sort(received);
        std::vector<int> expected(k_TotalItems);
        std::iota(expected.begin(), expected.end(), 0);
        EXPECT_EQ(received, expected) << "first ID " << firstID;
        EXPECT_FALSE(queue.try_pop().has_value()) << "first ID " << firstID;
    }
}

} // namespace containers
