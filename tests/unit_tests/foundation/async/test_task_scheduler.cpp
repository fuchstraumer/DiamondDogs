#include <gtest/gtest.h>

#include "async/FrameStatistics.hpp"
#include "async/Scheduler.hpp"
#include "async/Task.hpp"
#include "async/Timeline.hpp"
#include "async/TimelineSource.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using foundation::FrameStatistics;
using foundation::FrameStatisticsSnapshot;
using foundation::Scheduler;
using foundation::Task;
using foundation::Timeline;
using foundation::TimelineWait;
using foundation::TimelineSource;

namespace
{
    constexpr bool k_HasFrameStatistics = FOUNDATION_ASYNC_FRAME_STATISTICS != 0;

    // increments a counter while it is alive, so a test can count the live frames that hold one
    struct LiveFrameGuard
    {
        explicit LiveFrameGuard(int32_t& _liveCount) noexcept;
        ~LiveFrameGuard();
        LiveFrameGuard(const LiveFrameGuard&) = delete;
        LiveFrameGuard& operator=(const LiveFrameGuard&) = delete;
        int32_t& liveCount;
    };

    Task<int32_t> ChildAwaitsThenReturns(const Timeline& timeline, uint64_t target_value, int32_t return_value);
    Task<void> ParentAwaitsChild(const Timeline& timeline, uint64_t target_value, int32_t& result);
    Task<void> WaitThenCount(const Timeline& timeline, uint64_t target_value, int32_t& counter);
    Task<void> WaitThenSignal(const Timeline& wait_on, uint64_t target_value, Timeline& signal, uint64_t signal_value);
    Task<void> WaitThenRecordThread(const Timeline& timeline, const int32_t& payload, std::thread::id& resumed_on,
                                    int32_t& observed_payload);
    Task<void> LeafAwaitsTimeline(const Timeline& timeline, uint64_t target_value, int32_t& live_count);
    Task<void> MiddleAwaitsLeaf(const Timeline& timeline, uint64_t target_value, int32_t& live_count);
    Task<void> RootAwaitsMiddle(const Timeline& timeline, uint64_t target_value, int32_t& live_count);
    int32_t CountOccurrences(const std::string& text, const std::string& pattern);
    Task<void> WaitThenRecordResult(const Timeline& timeline, uint64_t target_value, TimelineWait& result);

    // signals a timeline from Poll, so a test can see when the Scheduler polls
    class SignalingSource final : public TimelineSource
    {
    public:
        SignalingSource(Timeline& _timeline, uint64_t _value) noexcept;
        void Poll() override;
        int32_t PollCount{ 0 };
        bool SignalOnPoll{ false };

    private:
        Timeline& timeline;
        uint64_t value;
    };

    Task<std::unique_ptr<int32_t>> ChildReturnsUniquePointer(const Timeline& timeline, uint64_t target_value);
    Task<void> ParentStoresUniquePointer(const Timeline& timeline, uint64_t target_value,
                                         std::unique_ptr<int32_t>& result);
    Task<void> ChainLevelC(std::vector<int32_t>& log);
    Task<void> ChainLevelB(std::vector<int32_t>& log);
    Task<void> ChainLevelA(std::vector<int32_t>& log);
    Task<void> SpawnedRoot(std::vector<int32_t>& log);
    Task<void> RootThatSpawnsRoot(Scheduler& scheduler, std::vector<int32_t>& log);
    Task<void> SpawnNextLink(Scheduler& scheduler, int32_t remaining, int32_t& started);
}

TEST(TaskSchedulerTest, ParentReceivesTheValueOfAChildThatAwaitedATimeline)
{
    Timeline timeline;
    Scheduler scheduler;
    int32_t result = 0;

    scheduler.Spawn(ParentAwaitsChild(timeline, 5u, result));
    EXPECT_EQ(scheduler.ParkedCount(), 0) << "a spawned root starts on the next Tick, not in Spawn";

    scheduler.Tick();
    EXPECT_EQ(scheduler.ParkedCount(), 1);
    EXPECT_EQ(result, 0);

    timeline.Signal(4u);
    scheduler.Tick();
    EXPECT_EQ(scheduler.ParkedCount(), 1) << "4 does not reach a target of 5";

    timeline.Signal(5u);
    scheduler.Tick();
    EXPECT_EQ(result, 7);
    EXPECT_EQ(scheduler.ParkedCount(), 0);
    EXPECT_TRUE(scheduler.Idle()) << "the finished root is destroyed in the Tick that finishes it";
}

TEST(TaskSchedulerTest, EveryFrameWaitingForOneTargetResumes)
{
    // a counting semaphore fails this: each waiter would consume a count
    Timeline timeline;
    Scheduler scheduler;
    int32_t counter = 0;

    scheduler.Spawn(WaitThenCount(timeline, 3u, counter));
    scheduler.Spawn(WaitThenCount(timeline, 3u, counter));
    scheduler.Spawn(WaitThenCount(timeline, 3u, counter));
    scheduler.Tick();
    EXPECT_EQ(scheduler.ParkedCount(), 3);

    timeline.Signal(3u);
    scheduler.Tick();
    EXPECT_EQ(counter, 3);
    EXPECT_TRUE(scheduler.Idle());
}

TEST(TaskSchedulerTest, ReachedTargetDoesNotPark)
{
    Timeline timeline;
    Scheduler scheduler;
    int32_t counter = 0;

    timeline.Signal(2u);
    scheduler.Spawn(WaitThenCount(timeline, 1u, counter));
    scheduler.Tick();

    EXPECT_EQ(counter, 1);
    EXPECT_EQ(scheduler.ParkedCount(), 0);
    EXPECT_TRUE(scheduler.Idle());
}

TEST(TaskSchedulerTest, SignalFromAWorkerResumesTheFrameOnTheOwnerThread)
{
    Timeline timeline;
    Scheduler scheduler;
    int32_t payload = 0;
    int32_t observedPayload = 0;
    std::thread::id resumedOn{};
    const std::thread::id ownerThread = std::this_thread::get_id();

    scheduler.Spawn(WaitThenRecordThread(timeline, payload, resumedOn, observedPayload));
    scheduler.Tick();

    // the worker writes plain data, then publishes it with the release store in Signal
    std::thread worker(
        [&timeline, &payload]()
        {
            payload = 42;
            timeline.Signal(1u);
        });

    constexpr int64_t k_MaxTicks = 100'000'000;

    while (!scheduler.Idle() && scheduler.TickCount() < k_MaxTicks)
    {
        scheduler.Tick();
        std::this_thread::yield();
    }

    worker.join();

    ASSERT_TRUE(scheduler.Idle());
    EXPECT_EQ(resumedOn, ownerThread);
    EXPECT_EQ(observedPayload, 42);
}

TEST(TaskSchedulerTest, WaitBlocksAWorkerUntilItsTargetIsReached)
{
    Timeline timeline;
    std::atomic<bool> woke{ false };

    std::thread worker(
        [&timeline, &woke]()
        {
            const TimelineWait waited = timeline.Wait(2u);
            EXPECT_EQ(waited, TimelineWait::Reached);
            woke.store(true, std::memory_order::release);
        });

    timeline.Signal(1u);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_FALSE(woke.load(std::memory_order::acquire)) << "1 does not reach a target of 2";

    timeline.Signal(2u);
    worker.join();
    EXPECT_TRUE(woke.load(std::memory_order::acquire));
}

TEST(TaskSchedulerTest, WaitReturnsFailedInsteadOfBlockingForeverWhenTheTimelineFails)
{
    Timeline timeline;
    std::atomic<bool> woke{ false };
    TimelineWait result = TimelineWait::Invalid;

    std::thread worker(
        [&timeline, &woke, &result]()
        {
            result = timeline.Wait(5u);
            woke.store(true, std::memory_order::release);
        });

    timeline.Signal(1u);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_FALSE(woke.load(std::memory_order::acquire)) << "1 does not reach a target of 5";

    timeline.Fail();
    worker.join();
    EXPECT_TRUE(woke.load(std::memory_order::acquire));
    EXPECT_EQ(result, TimelineWait::Failed);
    EXPECT_TRUE(timeline.Failed());
}

TEST(TaskSchedulerTest, WaitReturnsReachedWhenTheTargetIsReached)
{
    Timeline timeline;
    timeline.Signal(3u);

    EXPECT_EQ(timeline.Wait(3u), TimelineWait::Reached);
    EXPECT_EQ(timeline.Wait(2u), TimelineWait::Reached);
}

TEST(TaskSchedulerTest, FailAfterAValueWasReachedDoesNotChangeWhatReachedSays)
{
    Timeline timeline;
    timeline.Signal(4u);
    EXPECT_FALSE(timeline.Failed());

    timeline.Fail();

    EXPECT_TRUE(timeline.Failed());
    EXPECT_EQ(timeline.Value(), 4u);
    EXPECT_TRUE(timeline.Reached(4u));
    EXPECT_TRUE(timeline.Reached(1u));
    EXPECT_FALSE(timeline.Reached(5u)) << "a failed timeline does not reach values it never reached";
    EXPECT_EQ(timeline.Wait(4u), TimelineWait::Reached) << "a reached target wins over the failure";
    EXPECT_EQ(timeline.Wait(5u), TimelineWait::Failed);
}

TEST(TaskSchedulerTest, AFrameParkedOnATimelineThatFailsResumesOnTheNextTickWithFailed)
{
    Timeline timeline;
    Scheduler scheduler;
    TimelineWait result = TimelineWait::Invalid;

    scheduler.Spawn(WaitThenRecordResult(timeline, 3u, result));
    scheduler.Tick();
    EXPECT_EQ(scheduler.ParkedCount(), 1);
    EXPECT_EQ(result, TimelineWait::Invalid);

    timeline.Signal(1u);
    scheduler.Tick();
    EXPECT_EQ(scheduler.ParkedCount(), 1) << "1 does not reach a target of 3";

    timeline.Fail();
    scheduler.Tick();
    EXPECT_EQ(result, TimelineWait::Failed);
    EXPECT_EQ(scheduler.ParkedCount(), 0);
    EXPECT_TRUE(scheduler.Idle());
}

TEST(TaskSchedulerTest, AFrameThatAwaitsAnAlreadyFailedTimelineDoesNotParkAndGetsFailed)
{
    Timeline timeline;
    Scheduler scheduler;
    TimelineWait result = TimelineWait::Invalid;

    timeline.Fail();
    scheduler.Spawn(WaitThenRecordResult(timeline, 3u, result));
    scheduler.Tick();

    EXPECT_EQ(result, TimelineWait::Failed);
    EXPECT_EQ(scheduler.ParkedCount(), 0);
    EXPECT_TRUE(scheduler.Idle());
}

TEST(TaskSchedulerTest, AFrameParkedOnAReachedTimelineGetsReachedEvenWhenTheTimelineFailedAfterwards)
{
    Timeline timeline;
    Scheduler scheduler;
    TimelineWait result = TimelineWait::Invalid;

    scheduler.Spawn(WaitThenRecordResult(timeline, 2u, result));
    scheduler.Tick();
    EXPECT_EQ(scheduler.ParkedCount(), 1);

    timeline.Signal(2u);
    timeline.Fail();
    scheduler.Tick();

    EXPECT_EQ(result, TimelineWait::Reached);
    EXPECT_TRUE(scheduler.Idle());
}

TEST(TaskSchedulerTest, TargetReachedDuringATickResumesOnTheNextTick)
{
    Timeline first;
    Timeline second;
    Scheduler scheduler;
    int32_t counter = 0;

    scheduler.Spawn(WaitThenSignal(first, 1u, second, 1u));
    scheduler.Spawn(WaitThenCount(second, 1u, counter));
    scheduler.Tick();
    EXPECT_EQ(scheduler.ParkedCount(), 2);

    first.Signal(1u);
    scheduler.Tick();
    EXPECT_EQ(counter, 0) << "second reached 1 during this Tick, after the parked list was read";

    scheduler.Tick();
    EXPECT_EQ(counter, 1);
    EXPECT_TRUE(scheduler.Idle());
}

TEST(TaskSchedulerTest, DumpStateWalksTheAsyncBacktraceToTheRoot)
{
    Timeline timeline;
    Scheduler scheduler;
    int32_t liveCount = 0;

    scheduler.Spawn(RootAwaitsMiddle(timeline, 1u, liveCount));
    scheduler.Tick();

    const std::string state = scheduler.DumpState();
    EXPECT_EQ(CountOccurrences(state, "parked: "), 1) << state;
    EXPECT_EQ(CountOccurrences(state, "    frame "), 3) << state;

    timeline.Signal(1u);
    scheduler.Tick();
    EXPECT_TRUE(scheduler.Idle());
    EXPECT_EQ(liveCount, 0);
}

TEST(TaskSchedulerTest, DestroyingTheSchedulerDestroysEveryLiveFrame)
{
    Timeline timeline;
    int32_t liveCount = 0;

    {
        Scheduler scheduler;
        scheduler.Spawn(RootAwaitsMiddle(timeline, 1u, liveCount));
        scheduler.Tick();
        EXPECT_EQ(liveCount, 3);
        // the Debug leak report on stderr is expected here
    }

    EXPECT_EQ(liveCount, 0);
}

TEST(TaskSchedulerTest, SourcesArePolledBeforeParkedFramesResume)
{
    Timeline timeline;
    Scheduler scheduler;
    SignalingSource source(timeline, 1u);
    int32_t counter = 0;

    scheduler.AddSource(source);
    scheduler.Spawn(WaitThenCount(timeline, 1u, counter));
    scheduler.Tick();
    EXPECT_EQ(source.PollCount, 1);
    EXPECT_EQ(scheduler.ParkedCount(), 1);

    source.SignalOnPoll = true;
    scheduler.Tick();
    EXPECT_EQ(source.PollCount, 2);
    EXPECT_EQ(counter, 1) << "the Poll in this Tick reached the target, so the frame resumes in this Tick";
    EXPECT_TRUE(scheduler.Idle());

    scheduler.RemoveSource(source);
    scheduler.Tick();
    EXPECT_EQ(source.PollCount, 2) << "a removed source is not polled";
}

TEST(TaskSchedulerTest, ATaskCanReturnAMoveOnlyValue)
{
    Timeline timeline;
    Scheduler scheduler;
    std::unique_ptr<int32_t> result;

    scheduler.Spawn(ParentStoresUniquePointer(timeline, 1u, result));
    scheduler.Tick();
    EXPECT_EQ(result, nullptr);

    timeline.Signal(1u);
    scheduler.Tick();
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(*result, 9);
    EXPECT_TRUE(scheduler.Idle());
}

TEST(TaskSchedulerTest, AChainOfThreeVoidTasksRunsDownAndReturnsUp)
{
    Scheduler scheduler;
    std::vector<int32_t> log;

    scheduler.Spawn(ChainLevelA(log));
    EXPECT_TRUE(log.empty());

    scheduler.Tick();
    const std::vector<int32_t> expected{ 1, 2, 3, -3, -2, -1 };
    EXPECT_EQ(log, expected) << "no timeline is involved, so symmetric transfer runs the whole chain in one Tick";
    EXPECT_TRUE(scheduler.Idle());
}

TEST(TaskSchedulerTest, ARootThatSpawnsARootStartsTheNewRootInTheSameTickAfterTheSpawnerEnds)
{
    Scheduler scheduler;
    std::vector<int32_t> log;

    scheduler.Spawn(RootThatSpawnsRoot(scheduler, log));
    EXPECT_TRUE(log.empty()) << "a root spawned outside a Tick waits for the next Tick";

    scheduler.Tick();

    // the spawner logs 1, spawns, and logs 3 before it ends; only then does the new root log 2
    const std::vector<int32_t> expected{ 1, 3, 2 };
    EXPECT_EQ(log, expected);
    EXPECT_TRUE(scheduler.Idle()) << "the new root finished in the same Tick, so it was destroyed in it";
}

// A chain where each root spawns the next must not take one Tick for each link
TEST(TaskSchedulerTest, AChainOf300SpawnsRunsInOneTick)
{
    constexpr int32_t k_Links = 300;

    Scheduler scheduler;
    int32_t started = 0;

    scheduler.Spawn(SpawnNextLink(scheduler, k_Links, started));
    scheduler.Tick();

    EXPECT_EQ(started, k_Links + 1);
    EXPECT_EQ(scheduler.TickCount(), 1);
    EXPECT_TRUE(scheduler.Idle());
}

TEST(TaskSchedulerTest, FrameStatisticsCountEveryFrameOfAChain)
{
    if (!k_HasFrameStatistics)
    {
        GTEST_SKIP() << "the frame counters are compiled out of this configuration";
    }

    Timeline timeline;
    int32_t liveCount = 0;
    const FrameStatisticsSnapshot before = FrameStatistics();

    {
        Scheduler scheduler;
        scheduler.Spawn(RootAwaitsMiddle(timeline, 1u, liveCount));

        const FrameStatisticsSnapshot spawned = FrameStatistics();
        EXPECT_EQ(spawned.LiveFrames - before.LiveFrames, 1) << "a root frame exists as soon as the task is created";
        EXPECT_EQ(spawned.TotalFrames - before.TotalFrames, 1);

        scheduler.Tick();

        // the root started, awaited the middle task, and the middle task awaited the leaf task
        const FrameStatisticsSnapshot running = FrameStatistics();
        EXPECT_EQ(running.LiveFrames - before.LiveFrames, 3);
        EXPECT_EQ(running.TotalFrames - before.TotalFrames, 3);
        EXPECT_GT(running.LiveBytes, before.LiveBytes);
        EXPECT_GE(running.PeakLiveBytes, running.LiveBytes);

        timeline.Signal(1u);
        scheduler.Tick();
        EXPECT_TRUE(scheduler.Idle());
    }

    const FrameStatisticsSnapshot after = FrameStatistics();
    EXPECT_EQ(after.LiveFrames, before.LiveFrames);
    EXPECT_EQ(after.LiveBytes, before.LiveBytes);
    EXPECT_EQ(after.TotalFrames - before.TotalFrames, 3);
    EXPECT_GE(after.PeakLiveBytes, before.PeakLiveBytes);
}

TEST(TaskSchedulerTest, DumpStateNamesTheFunctionOfEveryFrame)
{
#if !(defined(FOUNDATION_ASYNC_SYMBOLS) && FOUNDATION_ASYNC_SYMBOLS)
    GTEST_SKIP() << "frame names need Windows and a build below Release";
#else
    Timeline timeline;
    Scheduler scheduler;
    int32_t liveCount = 0;

    scheduler.Spawn(RootAwaitsMiddle(timeline, 1u, liveCount));
    scheduler.Tick();

    // one line for each frame of the chain: leaf, middle, root
    const std::string state = scheduler.DumpState();
    EXPECT_EQ(CountOccurrences(state, "LeafAwaitsTimeline"), 1) << state;
    EXPECT_EQ(CountOccurrences(state, "MiddleAwaitsLeaf"), 1) << state;
    EXPECT_EQ(CountOccurrences(state, "RootAwaitsMiddle"), 1) << state;

    timeline.Signal(1u);
    scheduler.Tick();
    EXPECT_TRUE(scheduler.Idle());
#endif
}

namespace
{
    LiveFrameGuard::LiveFrameGuard(int32_t& _liveCount) noexcept : liveCount{ _liveCount }
    {
        ++liveCount;
    }

    LiveFrameGuard::~LiveFrameGuard()
    {
        --liveCount;
    }

    Task<int32_t> ChildAwaitsThenReturns(const Timeline& timeline, uint64_t target_value, int32_t return_value)
    {
        const TimelineWait waited = co_await timeline.Until(target_value);
        EXPECT_EQ(waited, TimelineWait::Reached);
        co_return return_value;
    }

    Task<void> ParentAwaitsChild(const Timeline& timeline, uint64_t target_value, int32_t& result)
    {
        result = co_await ChildAwaitsThenReturns(timeline, target_value, 7);
    }

    Task<void> WaitThenRecordResult(const Timeline& timeline, uint64_t target_value, TimelineWait& result)
    {
        result = co_await timeline.Until(target_value);
    }

    Task<void> WaitThenCount(const Timeline& timeline, uint64_t target_value, int32_t& counter)
    {
        const TimelineWait waited = co_await timeline.Until(target_value);
        EXPECT_EQ(waited, TimelineWait::Reached);
        ++counter;
    }

    Task<void> WaitThenSignal(const Timeline& wait_on, uint64_t target_value, Timeline& signal, uint64_t signal_value)
    {
        const TimelineWait waited = co_await wait_on.Until(target_value);
        EXPECT_EQ(waited, TimelineWait::Reached);
        signal.Signal(signal_value);
    }

    Task<void> WaitThenRecordThread(const Timeline& timeline, const int32_t& payload, std::thread::id& resumed_on,
                                    int32_t& observed_payload)
    {
        const TimelineWait waited = co_await timeline.Until(1u);
        EXPECT_EQ(waited, TimelineWait::Reached);
        resumed_on = std::this_thread::get_id();
        observed_payload = payload;
    }

    Task<void> LeafAwaitsTimeline(const Timeline& timeline, uint64_t target_value, int32_t& live_count)
    {
        LiveFrameGuard guard{ live_count };
        const TimelineWait waited = co_await timeline.Until(target_value);
        EXPECT_EQ(waited, TimelineWait::Reached);
    }

    Task<void> MiddleAwaitsLeaf(const Timeline& timeline, uint64_t target_value, int32_t& live_count)
    {
        LiveFrameGuard guard{ live_count };
        co_await LeafAwaitsTimeline(timeline, target_value, live_count);
    }

    Task<void> RootAwaitsMiddle(const Timeline& timeline, uint64_t target_value, int32_t& live_count)
    {
        LiveFrameGuard guard{ live_count };
        co_await MiddleAwaitsLeaf(timeline, target_value, live_count);
    }

    SignalingSource::SignalingSource(Timeline& _timeline, uint64_t _value) noexcept :
        timeline{ _timeline },
        value{ _value }
    {
    }

    void SignalingSource::Poll()
    {
        ++PollCount;

        if (SignalOnPoll)
        {
            timeline.Signal(value);
        }
    }

    Task<std::unique_ptr<int32_t>> ChildReturnsUniquePointer(const Timeline& timeline, uint64_t target_value)
    {
        const TimelineWait waited = co_await timeline.Until(target_value);
        EXPECT_EQ(waited, TimelineWait::Reached);
        co_return std::make_unique<int32_t>(9);
    }

    Task<void> ParentStoresUniquePointer(const Timeline& timeline, uint64_t target_value,
                                         std::unique_ptr<int32_t>& result)
    {
        result = co_await ChildReturnsUniquePointer(timeline, target_value);
    }

    Task<void> ChainLevelC(std::vector<int32_t>& log)
    {
        log.push_back(3);
        co_return;
    }

    Task<void> ChainLevelB(std::vector<int32_t>& log)
    {
        log.push_back(2);
        co_await ChainLevelC(log);
        log.push_back(-3);
    }

    Task<void> ChainLevelA(std::vector<int32_t>& log)
    {
        log.push_back(1);
        co_await ChainLevelB(log);
        log.push_back(-2);
        log.push_back(-1);
    }

    Task<void> SpawnedRoot(std::vector<int32_t>& log)
    {
        log.push_back(2);
        co_return;
    }

    Task<void> RootThatSpawnsRoot(Scheduler& scheduler, std::vector<int32_t>& log)
    {
        log.push_back(1);
        scheduler.Spawn(SpawnedRoot(log));
        log.push_back(3);
        co_return;
    }

    Task<void> SpawnNextLink(Scheduler& scheduler, int32_t remaining, int32_t& started)
    {
        ++started;

        if (remaining > 0)
        {
            scheduler.Spawn(SpawnNextLink(scheduler, remaining - 1, started));
        }

        co_return;
    }

    int32_t CountOccurrences(const std::string& text, const std::string& pattern)
    {
        int32_t count = 0;
        size_t position = text.find(pattern);

        while (position != std::string::npos)
        {
            ++count;
            position = text.find(pattern, position + pattern.size());
        }

        return count;
    }
}
