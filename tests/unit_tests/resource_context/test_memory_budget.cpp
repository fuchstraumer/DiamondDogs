#include "ResourceContext.hpp"
#include "ResourceContextFixture.hpp"
#include "ResourceFlags.hpp"
#include "ResourceTypes.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <print>
#include <thread>

namespace
{
    constexpr uint64_t k_MiB = 1024ull * 1024ull;
    constexpr uint64_t k_LargeBufferSize = 256ull * k_MiB;
    constexpr int64_t k_MinimumRiseBytes = 256ll * 1024ll * 1024ll;
    constexpr int64_t k_MinimumFallBytes = 200ll * 1024ll * 1024ll;
    constexpr int64_t k_PublishTimeoutSeconds = 5;

    /** Returns the sum of the usage of every device-local heap in the snapshot. */
    [[nodiscard]] uint64_t DeviceLocalUsage(const MemoryBudgetSnapshot& snapshot);
    /** Returns the number of device-local heaps in the snapshot. */
    [[nodiscard]] int32_t DeviceLocalHeapCount(const MemoryBudgetSnapshot& snapshot);
    /** Ticks the context and returns the budget once the worker publishes a new one. Fails the test after a timeout. */
    [[nodiscard]] MemoryBudgetSnapshot TickAndReadBudget(ResourceContext& context);
    /** Prints FromExtension, the tick sequence, and one line for each heap. */
    void PrintBudget(const MemoryBudgetSnapshot& snapshot);
    [[nodiscard]] BufferCreateInfo DeviceLocalBuffer(size_t size_bytes);
}

TEST_F(ResourceContextTest, MemoryBudgetHasNoHeapsBeforeTick)
{
    const MemoryBudgetSnapshot snapshot = context->MemoryBudgets();

    EXPECT_EQ(snapshot.HeapCount, 0);
    EXPECT_EQ(snapshot.TickSequence, 0);
}

TEST_F(ResourceContextTest, MemoryBudgetPublishesADeviceLocalHeapOnTick)
{
    const MemoryBudgetSnapshot snapshot = TickAndReadBudget(*context);
    PrintBudget(snapshot);

    EXPECT_GT(snapshot.HeapCount, 0);
    EXPECT_GT(DeviceLocalHeapCount(snapshot), 0);
}

TEST_F(ResourceContextTest, MemoryBudgetUsageRisesWithADeviceLocalBuffer)
{
    const MemoryBudgetSnapshot before = TickAndReadBudget(*context);

    const GraphicsResource buffer = CreateBufferAndWait(DeviceLocalBuffer(k_LargeBufferSize), {});
    const MemoryBudgetSnapshot after = TickAndReadBudget(*context);
    PrintBudget(after);

    const int64_t riseBytes = static_cast<int64_t>(DeviceLocalUsage(after)) - static_cast<int64_t>(DeviceLocalUsage(before));
    std::println("memory budget: device-local usage rose by {} MiB after a 256 MiB buffer", riseBytes / static_cast<int64_t>(k_MiB));
    EXPECT_GE(riseBytes, k_MinimumRiseBytes);

    DestroyAndWait(buffer);
}

TEST_F(ResourceContextTest, MemoryBudgetUsageFallsWhenABufferIsDestroyed)
{
    const GraphicsResource buffer = CreateBufferAndWait(DeviceLocalBuffer(k_LargeBufferSize), {});
    const MemoryBudgetSnapshot before = TickAndReadBudget(*context);

    DestroyAndWait(buffer);
    const MemoryBudgetSnapshot after = TickAndReadBudget(*context);
    PrintBudget(after);

    const int64_t fallBytes = static_cast<int64_t>(DeviceLocalUsage(before)) - static_cast<int64_t>(DeviceLocalUsage(after));
    std::println("memory budget: device-local usage fell by {} MiB after destroying a 256 MiB buffer", fallBytes / static_cast<int64_t>(k_MiB));
    EXPECT_GE(fallBytes, k_MinimumFallBytes);
}

namespace
{
    uint64_t DeviceLocalUsage(const MemoryBudgetSnapshot& snapshot)
    {
        uint64_t total = 0u;
        for (int32_t heapIndex = 0; heapIndex < snapshot.HeapCount; ++heapIndex)
        {
            const MemoryHeapBudget& heap = snapshot.Heaps[static_cast<size_t>(heapIndex)];
            if (heap.DeviceLocal)
            {
                total += heap.UsageBytes;
            }
        }

        return total;
    }

    int32_t DeviceLocalHeapCount(const MemoryBudgetSnapshot& snapshot)
    {
        int32_t count = 0;
        for (int32_t heapIndex = 0; heapIndex < snapshot.HeapCount; ++heapIndex)
        {
            if (snapshot.Heaps[static_cast<size_t>(heapIndex)].DeviceLocal)
            {
                ++count;
            }
        }

        return count;
    }

    MemoryBudgetSnapshot TickAndReadBudget(ResourceContext& context)
    {
        const int64_t sequenceBefore = context.MemoryBudgets().TickSequence;
        context.Tick({});

        const std::chrono::steady_clock::time_point deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(k_PublishTimeoutSeconds);
        MemoryBudgetSnapshot snapshot = context.MemoryBudgets();
        while (snapshot.TickSequence == sequenceBefore)
        {
            if (std::chrono::steady_clock::now() > deadline)
            {
                ADD_FAILURE() << "the worker did not publish a memory budget after Tick";
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            snapshot = context.MemoryBudgets();
        }

        return snapshot;
    }

    void PrintBudget(const MemoryBudgetSnapshot& snapshot)
    {
        std::println("memory budget: FromExtension={} HeapCount={} TickSequence={}", snapshot.FromExtension, snapshot.HeapCount, snapshot.TickSequence);
        for (int32_t heapIndex = 0; heapIndex < snapshot.HeapCount; ++heapIndex)
        {
            const MemoryHeapBudget& heap = snapshot.Heaps[static_cast<size_t>(heapIndex)];
            std::println("memory budget: heap {} deviceLocal={} budget={} MiB usage={} MiB",
                heapIndex,
                heap.DeviceLocal,
                heap.BudgetBytes / k_MiB,
                heap.UsageBytes / k_MiB);
        }
    }

    BufferCreateInfo DeviceLocalBuffer(size_t size_bytes)
    {
        BufferCreateInfo info{};
        info.Domain = ResourceDomain::GPUOnly;
        info.Usage = BufferUsageBits::ComputeShaderSRV | BufferUsageBits::CopySource | BufferUsageBits::CopyDestination;
        info.Size = size_bytes;
        return info;
    }
}
