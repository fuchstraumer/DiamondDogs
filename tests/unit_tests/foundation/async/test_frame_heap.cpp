#include <gtest/gtest.h>

#include "async/FrameHeap.hpp"
#include "async/FrameStatistics.hpp"
#include "async/Scheduler.hpp"
#include "async/Task.hpp"
#include "async/Timeline.hpp"

#include <chrono>
#include <cstdlib>
#include <mimalloc.h>
#include <new>
#include <cstdint>
#include <print>
#include <thread>
#include <utility>

using foundation::FrameBackend;
using foundation::FrameHeap;
using foundation::FrameStatistics;
using foundation::FrameStatisticsSnapshot;
using foundation::Scheduler;
using foundation::Task;
using foundation::Timeline;
using foundation::TimelineWait;

namespace
{
    constexpr bool k_HasFrameStatistics = FOUNDATION_ASYNC_FRAME_STATISTICS != 0;

    Task<void> ReturnImmediately();
    Task<void> LeafWaitsForTimeline(const Timeline& timeline, uint64_t target_value);
    Task<void> MiddleAwaitsLeaf(const Timeline& timeline, uint64_t target_value);
    Task<void> RootAwaitsMiddle(const Timeline& timeline, uint64_t target_value);

    // Returns the nanoseconds that one frame takes to be created by Spawn, started and destroyed by Tick.
    double MeasureNanosecondsPerFrame(bool use_scheduler_heap, FrameBackend fallback_backend);
}

TEST(FrameHeapTest, EveryFrameOfAChainIsCountedInItsOwnSchedulerAndNotInASecondOne)
{
    if (!k_HasFrameStatistics)
    {
        GTEST_SKIP() << "the frame counters are compiled out of this configuration";
    }

    Timeline timeline;
    Scheduler first;
    first.Spawn(RootAwaitsMiddle(timeline, 1u));
    first.Tick();

    const FrameStatisticsSnapshot firstRunning = first.FrameStatistics();
    EXPECT_EQ(firstRunning.LiveFrames, 3) << "the root, the middle task and the leaf task";
    EXPECT_EQ(firstRunning.TotalFrames, 3);
    EXPECT_GT(firstRunning.LiveBytes, 0u);

    {
        Scheduler second;
        EXPECT_EQ(second.FrameStatistics().TotalFrames, 0);

        second.Spawn(ReturnImmediately());
        second.Tick();

        const FrameStatisticsSnapshot secondDone = second.FrameStatistics();
        EXPECT_EQ(secondDone.TotalFrames, 1);
        EXPECT_EQ(secondDone.LiveFrames, 0);

        const FrameStatisticsSnapshot firstUnchanged = first.FrameStatistics();
        EXPECT_EQ(firstUnchanged.TotalFrames, 3) << "the frame of the second scheduler is not counted in the first";
        EXPECT_EQ(firstUnchanged.LiveFrames, 3);
        EXPECT_EQ(firstUnchanged.LiveBytes, firstRunning.LiveBytes);
    }

    timeline.Signal(1u);
    first.Tick();
    EXPECT_TRUE(first.Idle());
}

TEST(FrameHeapTest, WhenTheChainFinishesEveryCounterReturnsToItsStart)
{
    if (!k_HasFrameStatistics)
    {
        GTEST_SKIP() << "the frame counters are compiled out of this configuration";
    }

    Timeline timeline;
    const FrameStatisticsSnapshot processBefore = FrameStatistics();

    {
        Scheduler scheduler;
        scheduler.Spawn(RootAwaitsMiddle(timeline, 1u));
        scheduler.Tick();
        EXPECT_EQ(scheduler.FrameStatistics().LiveFrames, 3);

        const FrameStatisticsSnapshot processRunning = FrameStatistics();
        EXPECT_EQ(processRunning.LiveFrames - processBefore.LiveFrames, 3);
        EXPECT_GT(processRunning.LiveBytes, processBefore.LiveBytes);

        timeline.Signal(1u);
        scheduler.Tick();
        ASSERT_TRUE(scheduler.Idle());

        const FrameStatisticsSnapshot finished = scheduler.FrameStatistics();
        EXPECT_EQ(finished.LiveFrames, 0);
        EXPECT_EQ(finished.LiveBytes, 0u);
        EXPECT_EQ(finished.TotalFrames, 3);
        EXPECT_GT(finished.PeakLiveBytes, 0u);

        const FrameStatisticsSnapshot processFinished = FrameStatistics();
        EXPECT_EQ(processFinished.LiveFrames, processBefore.LiveFrames);
        EXPECT_EQ(processFinished.LiveBytes, processBefore.LiveBytes);
        EXPECT_EQ(processFinished.TotalFrames - processBefore.TotalFrames, 3);
    }
}

TEST(FrameHeapTest, AFrameCreatedOnAThreadWithNoSchedulerUsesTheDefaultHeapAndIsFreedCorrectly)
{
    if (!k_HasFrameStatistics)
    {
        GTEST_SKIP() << "the frame counters are compiled out of this configuration";
    }

    Scheduler scheduler;
    const FrameStatisticsSnapshot processBefore = FrameStatistics();
    FrameHeap* currentOnWorker = FrameHeap::Current();
    FrameStatisticsSnapshot processWhileAlive;

    // a new thread has no scheduler, so its frames take the fallback path
    const auto createAndDestroyOnWorker = [&]()
    {
        currentOnWorker = FrameHeap::Current();

        {
            Task<void> task = ReturnImmediately();
            processWhileAlive = FrameStatistics();
        }
    };

    std::thread worker(createAndDestroyOnWorker);
    worker.join();

    EXPECT_EQ(currentOnWorker, nullptr);
    EXPECT_EQ(processWhileAlive.LiveFrames - processBefore.LiveFrames, 1);
    EXPECT_GT(processWhileAlive.LiveBytes, processBefore.LiveBytes);

    const FrameStatisticsSnapshot processAfter = FrameStatistics();
    EXPECT_EQ(processAfter.LiveFrames, processBefore.LiveFrames);
    EXPECT_EQ(processAfter.LiveBytes, processBefore.LiveBytes);
    EXPECT_EQ(processAfter.TotalFrames - processBefore.TotalFrames, 1);

    EXPECT_EQ(scheduler.FrameStatistics().TotalFrames, 0) << "a frame of another thread is not in this scheduler";
}

TEST(FrameHeapTest, SchedulersDestroyedInOrderRestoreTheCurrentHeapEachTime)
{
    FrameHeap* const original = FrameHeap::Current();

    {
        Scheduler first;
        FrameHeap* const firstHeap = FrameHeap::Current();
        EXPECT_NE(firstHeap, original);
        EXPECT_NE(firstHeap, nullptr);

        {
            Scheduler second;
            FrameHeap* const secondHeap = FrameHeap::Current();
            EXPECT_NE(secondHeap, firstHeap);
            EXPECT_NE(secondHeap, nullptr);
        }

        EXPECT_EQ(FrameHeap::Current(), firstHeap) << "the destructor of the second scheduler restores the first";

        {
            Scheduler third;
            EXPECT_NE(FrameHeap::Current(), firstHeap);
        }

        EXPECT_EQ(FrameHeap::Current(), firstHeap);
    }

    EXPECT_EQ(FrameHeap::Current(), original);
}

TEST(FrameHeapTest, TickingTheOlderSchedulerCountsItsFramesInTheOlderHeapAndRestoresTheRouting)
{
    if (!k_HasFrameStatistics)
    {
        GTEST_SKIP() << "the frame counters are compiled out of this configuration";
    }

    Timeline timeline;
    Scheduler older;
    FrameHeap* const olderHeap = FrameHeap::Current();

    {
        Scheduler newer;
        FrameHeap* const newerHeap = FrameHeap::Current();
        ASSERT_NE(newerHeap, olderHeap);

        // the root frame is created by this call, so the newer heap serves it. The middle and leaf frames are created
        // during the Tick of the older scheduler
        older.Spawn(RootAwaitsMiddle(timeline, 1u));
        older.Tick();

        EXPECT_EQ(older.FrameStatistics().TotalFrames, 2) << "the middle task and the leaf task";
        EXPECT_EQ(newer.FrameStatistics().TotalFrames, 1) << "only the root, which Spawn created";
        EXPECT_EQ(FrameHeap::Current(), newerHeap) << "Tick restores the routing that it found";

        timeline.Signal(1u);
        older.Tick();
        EXPECT_TRUE(older.Idle());
    }

    EXPECT_EQ(FrameHeap::Current(), olderHeap);
}

TEST(FrameHeapTest, AFrameFreedOnAnotherThreadBalancesTheHeapThatAllocatedIt)
{
    if (!k_HasFrameStatistics)
    {
        GTEST_SKIP() << "the frame counters are compiled out of this configuration";
    }

    Scheduler scheduler;
    const FrameStatisticsSnapshot processBefore = FrameStatistics();
    Task<void> task = ReturnImmediately();

    EXPECT_EQ(scheduler.FrameStatistics().LiveFrames, 1);

    const auto destroyOnWorker = [&task]()
    {
        Task<void> victim = std::move(task);
    };

    std::thread worker(destroyOnWorker);
    worker.join();

    const FrameStatisticsSnapshot heapAfter = scheduler.FrameStatistics();
    EXPECT_EQ(heapAfter.LiveFrames, 0);
    EXPECT_EQ(heapAfter.LiveBytes, 0u);

    const FrameStatisticsSnapshot processAfter = FrameStatistics();
    EXPECT_EQ(processAfter.LiveFrames, processBefore.LiveFrames);
    EXPECT_EQ(processAfter.LiveBytes, processBefore.LiveBytes);
}

// Not a pass/fail test. Run: --gtest_also_run_disabled_tests --gtest_filter=*Benchmark*, in Release.
TEST(FrameHeapBenchmark, DISABLED_OneMillionSmallTasksOnTheSchedulerHeap)
{
    const double nanoseconds = MeasureNanosecondsPerFrame(true, FrameBackend::Mimalloc);
    std::println("scheduler heap: {:.2f} ns per frame", nanoseconds);
}

TEST(FrameHeapBenchmark, DISABLED_OneMillionSmallTasksOnTheDefaultHeap)
{
    const double nanoseconds = MeasureNanosecondsPerFrame(false, FrameBackend::Mimalloc);
    std::println("default heap: {:.2f} ns per frame", nanoseconds);
}

TEST(FrameHeapBenchmark, DISABLED_OneMillionSmallTasksOnTheWindowsProcessHeap)
{
    const double nanoseconds = MeasureNanosecondsPerFrame(false, FrameBackend::WindowsHeap);
    std::println("windows process heap: {:.2f} ns per frame", nanoseconds);
}

TEST(FrameHeapBenchmark, DISABLED_OneMillionSmallTasksOnCrtMalloc)
{
    const double nanoseconds = MeasureNanosecondsPerFrame(false, FrameBackend::CrtMalloc);
    std::println("crt malloc: {:.2f} ns per frame", nanoseconds);
}

TEST(FrameHeapBenchmark, DISABLED_WhichAllocatorServesNewAndMalloc)
{
    void* fromNew = ::operator new(64);
    void* fromMalloc = std::malloc(64);
    void* fromMimalloc = mi_malloc(64);
    std::println("operator new in mimalloc region: {}", mi_is_in_heap_region(fromNew));
    std::println("std::malloc in mimalloc region: {}", mi_is_in_heap_region(fromMalloc));
    std::println("mi_malloc in mimalloc region (control): {}", mi_is_in_heap_region(fromMimalloc));
    ::operator delete(fromNew);
    std::free(fromMalloc);
    mi_free(fromMimalloc);
}

namespace
{
    Task<void> ReturnImmediately()
    {
        co_return;
    }

    Task<void> LeafWaitsForTimeline(const Timeline& timeline, uint64_t target_value)
    {
        const TimelineWait result = co_await timeline.Until(target_value);
        EXPECT_EQ(result, TimelineWait::Reached);
    }

    Task<void> MiddleAwaitsLeaf(const Timeline& timeline, uint64_t target_value)
    {
        co_await LeafWaitsForTimeline(timeline, target_value);
    }

    Task<void> RootAwaitsMiddle(const Timeline& timeline, uint64_t target_value)
    {
        co_await MiddleAwaitsLeaf(timeline, target_value);
    }

    double MeasureNanosecondsPerFrame(bool use_scheduler_heap, FrameBackend fallback_backend)
    {
        constexpr int32_t k_TotalFrames = 1'000'000;
        constexpr int32_t k_BatchSize = 1'000;
        constexpr int32_t k_WarmUpBatches = 20;

        Scheduler scheduler;
        FrameHeap* const suspended = use_scheduler_heap ? nullptr : FrameHeap::SuspendRouting();
        const FrameBackend previousBackend = FrameHeap::SetFallbackBackend(fallback_backend);

        for (int32_t batch = 0; batch < k_WarmUpBatches; ++batch)
        {
            for (int32_t index = 0; index < k_BatchSize; ++index)
            {
                scheduler.Spawn(ReturnImmediately());
            }

            scheduler.Tick();
        }

        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

        for (int32_t batch = 0; batch < k_TotalFrames / k_BatchSize; ++batch)
        {
            for (int32_t index = 0; index < k_BatchSize; ++index)
            {
                scheduler.Spawn(ReturnImmediately());
            }

            scheduler.Tick();
        }

        const std::chrono::steady_clock::time_point stop = std::chrono::steady_clock::now();

        if (!use_scheduler_heap)
        {
            FrameHeap::SetFallbackBackend(previousBackend);
            FrameHeap::ResumeRouting(suspended);
        }

        EXPECT_TRUE(scheduler.Idle());

        if (k_HasFrameStatistics)
        {
            const int64_t expectedInHeap =
                use_scheduler_heap ? int64_t{ k_TotalFrames } + k_WarmUpBatches * k_BatchSize : 0;
            EXPECT_EQ(scheduler.FrameStatistics().TotalFrames, expectedInHeap);
        }

        const std::chrono::nanoseconds elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start);
        return static_cast<double>(elapsed.count()) / static_cast<double>(k_TotalFrames);
    }
}
