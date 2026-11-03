#include <gtest/gtest.h>

#include "async/Scheduler.hpp"
#include "async/Task.hpp"
#include "async/Yield.hpp"

#include <coroutine>
#include <cstdint>
#include <string>
#include <vector>

using foundation::Scheduler;
using foundation::Task;
using foundation::Yield;

namespace
{
    // Suspends the awaiting frame and stores its handle, so that a test can Post it. The scheduler does not know the frame.
    class SuspendIntoSlot
    {
    public:
        explicit SuspendIntoSlot(std::coroutine_handle<>& _slot) noexcept;
        [[nodiscard]] bool await_ready() const noexcept;
        void await_suspend(std::coroutine_handle<> handle) noexcept;
        void await_resume() const noexcept;

    private:
        std::coroutine_handle<>& slot;
    };

    Task<void> LogThenSuspendThenLog(std::coroutine_handle<>& slot, std::vector<int32_t>& log, int32_t before,
                                     int32_t after);
    Task<void> LogThenPostThenLog(Scheduler& scheduler, std::coroutine_handle<>& slot, std::vector<int32_t>& log,
                                  int32_t before, int32_t after);
    Task<void> SuspendThenPostAnother(Scheduler& scheduler, std::coroutine_handle<>& own_slot,
                                      std::coroutine_handle<>& other_slot, std::vector<int32_t>& log);
    Task<void> LogThenYieldThenLog(std::vector<int32_t>& log, int32_t before, int32_t after);
    Task<void> YieldTwice(std::vector<int32_t>& log);
    Task<void> ChildYields(std::vector<int32_t>& log);
    Task<void> ParentAwaitsYieldingChild(std::vector<int32_t>& log);
    int32_t CountOccurrences(const std::string& text, const std::string& pattern);
}

TEST(PostTest, AHandlePostedDuringATickResumesInTheSameTick)
{
    Scheduler scheduler;
    std::coroutine_handle<> slot{};
    std::vector<int32_t> log;

    scheduler.Spawn(LogThenSuspendThenLog(slot, log, 10, 11));
    scheduler.Tick();
    ASSERT_TRUE(slot);
    EXPECT_EQ(log, (std::vector<int32_t>{ 10 }));

    scheduler.Spawn(LogThenPostThenLog(scheduler, slot, log, 1, 2));

    const int64_t tickBefore = scheduler.TickCount();
    scheduler.Tick();
    const int64_t tickAfter = scheduler.TickCount();

    EXPECT_EQ(tickAfter - tickBefore, 1);
    EXPECT_EQ(log, (std::vector<int32_t>{ 10, 1, 2, 11 })) << "the posted frame runs after the poster suspends or ends";
    EXPECT_TRUE(scheduler.Idle());
}

TEST(PostTest, AHandlePostedOutsideATickResumesInTheNextTick)
{
    Scheduler scheduler;
    std::coroutine_handle<> slot{};
    std::vector<int32_t> log;

    scheduler.Spawn(LogThenSuspendThenLog(slot, log, 10, 11));
    scheduler.Tick();
    ASSERT_TRUE(slot);

    scheduler.Post(slot);
    EXPECT_EQ(log, (std::vector<int32_t>{ 10 })) << "Post never resumes the frame by itself";

    scheduler.Tick();
    EXPECT_EQ(log, (std::vector<int32_t>{ 10, 11 }));
    EXPECT_TRUE(scheduler.Idle());
}

TEST(PostTest, AFrameThatIsResumedFromTheReadyListRunsTheFramesItPostsInTheSameTick)
{
    Scheduler scheduler;
    std::coroutine_handle<> firstSlot{};
    std::coroutine_handle<> secondSlot{};
    std::vector<int32_t> log;

    scheduler.Spawn(SuspendThenPostAnother(scheduler, firstSlot, secondSlot, log));
    scheduler.Spawn(LogThenSuspendThenLog(secondSlot, log, 30, 31));
    scheduler.Tick();
    ASSERT_TRUE(firstSlot);
    ASSERT_TRUE(secondSlot);
    EXPECT_EQ(log, (std::vector<int32_t>{ 30 }));

    scheduler.Spawn(LogThenPostThenLog(scheduler, firstSlot, log, 1, 2));
    scheduler.Tick();

    // the poster ends, then the first frame resumes and posts the second, which resumes in the same drain
    EXPECT_EQ(log, (std::vector<int32_t>{ 30, 1, 2, 21, 22, 31 }));
    EXPECT_TRUE(scheduler.Idle());
}

TEST(YieldTest, AFrameThatYieldsResumesOnTheNextTickAndNotInTheSameOne)
{
    Scheduler scheduler;
    std::vector<int32_t> log;

    scheduler.Spawn(YieldTwice(log));

    scheduler.Tick();
    EXPECT_EQ(log, (std::vector<int32_t>{ 1 }));
    EXPECT_FALSE(scheduler.Idle());

    scheduler.Tick();
    EXPECT_EQ(log, (std::vector<int32_t>{ 1, 2 }));

    scheduler.Tick();
    EXPECT_EQ(log, (std::vector<int32_t>{ 1, 2, 3 }));
    EXPECT_TRUE(scheduler.Idle());
}

TEST(YieldTest, AChildTaskCanYieldAndTheParentResumesAfterIt)
{
    Scheduler scheduler;
    std::vector<int32_t> log;

    scheduler.Spawn(ParentAwaitsYieldingChild(log));

    scheduler.Tick();
    EXPECT_EQ(log, (std::vector<int32_t>{ 1, 2 }));

    scheduler.Tick();
    EXPECT_EQ(log, (std::vector<int32_t>{ 1, 2, 3, 4 }));
    EXPECT_TRUE(scheduler.Idle());
}

TEST(YieldTest, YieldedFramesRunBeforePostedFrames)
{
    Scheduler scheduler;
    std::coroutine_handle<> slot{};
    std::vector<int32_t> log;

    scheduler.Spawn(LogThenYieldThenLog(log, 1, 2));
    scheduler.Spawn(LogThenSuspendThenLog(slot, log, 10, 11));
    scheduler.Tick();
    EXPECT_EQ(log, (std::vector<int32_t>{ 1, 10 }));

    scheduler.Spawn(LogThenPostThenLog(scheduler, slot, log, 20, 21));
    scheduler.Tick();

    // 20 and 21 come first: the poster is a spawned root, and roots start before the ready list runs
    EXPECT_EQ(log, (std::vector<int32_t>{ 1, 10, 20, 21, 2, 11 }));
    EXPECT_TRUE(scheduler.Idle());
}

TEST(YieldTest, DumpStateListsYieldedAndReadyFramesLikeParkedFrames)
{
    Scheduler scheduler;
    std::coroutine_handle<> slot{};
    std::vector<int32_t> log;

    scheduler.Spawn(ParentAwaitsYieldingChild(log));
    scheduler.Spawn(LogThenSuspendThenLog(slot, log, 10, 11));
    scheduler.Tick();

    std::string state = scheduler.DumpState();
    EXPECT_EQ(CountOccurrences(state, "yielded: "), 1) << state;
    EXPECT_EQ(CountOccurrences(state, "ready: "), 0) << state;
    EXPECT_EQ(CountOccurrences(state, "    frame "), 2) << state << "the child and its parent";

    scheduler.Post(slot);
    state = scheduler.DumpState();
    EXPECT_EQ(CountOccurrences(state, "yielded: "), 1) << state;
    EXPECT_EQ(CountOccurrences(state, "ready: "), 1) << state;
    EXPECT_EQ(CountOccurrences(state, "    frame "), 3) << state;

    scheduler.Tick();
    EXPECT_TRUE(scheduler.Idle());
    EXPECT_TRUE(scheduler.DumpState().empty());
}

namespace
{
    SuspendIntoSlot::SuspendIntoSlot(std::coroutine_handle<>& _slot) noexcept : slot{ _slot }
    {
    }

    bool SuspendIntoSlot::await_ready() const noexcept
    {
        return false;
    }

    void SuspendIntoSlot::await_suspend(std::coroutine_handle<> handle) noexcept
    {
        slot = handle;
    }

    void SuspendIntoSlot::await_resume() const noexcept
    {
    }

    Task<void> LogThenSuspendThenLog(std::coroutine_handle<>& slot, std::vector<int32_t>& log, int32_t before,
                                     int32_t after)
    {
        log.push_back(before);
        co_await SuspendIntoSlot{ slot };
        log.push_back(after);
    }

    Task<void> LogThenPostThenLog(Scheduler& scheduler, std::coroutine_handle<>& slot, std::vector<int32_t>& log,
                                  int32_t before, int32_t after)
    {
        log.push_back(before);
        scheduler.Post(slot);
        log.push_back(after);
        co_return;
    }

    Task<void> SuspendThenPostAnother(Scheduler& scheduler, std::coroutine_handle<>& own_slot,
                                      std::coroutine_handle<>& other_slot, std::vector<int32_t>& log)
    {
        co_await SuspendIntoSlot{ own_slot };
        log.push_back(21);
        scheduler.Post(other_slot);
        log.push_back(22);
    }

    Task<void> LogThenYieldThenLog(std::vector<int32_t>& log, int32_t before, int32_t after)
    {
        log.push_back(before);
        co_await Yield();
        log.push_back(after);
    }

    Task<void> YieldTwice(std::vector<int32_t>& log)
    {
        log.push_back(1);
        co_await Yield();
        log.push_back(2);
        co_await Yield();
        log.push_back(3);
    }

    Task<void> ChildYields(std::vector<int32_t>& log)
    {
        log.push_back(2);
        co_await Yield();
        log.push_back(3);
    }

    Task<void> ParentAwaitsYieldingChild(std::vector<int32_t>& log)
    {
        log.push_back(1);
        co_await ChildYields(log);
        log.push_back(4);
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
