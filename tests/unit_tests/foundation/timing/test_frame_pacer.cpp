#include <gtest/gtest.h>
#include "timing/FramePacer.hpp"
#include <chrono>
#include <cstdint>
#include <vector>

// FramePacer on a fake clock: time moves only when a test does work (Advance) or the pacer sleeps, so every interval
// is exact and no test depends on the machine's timer.
namespace
{

    using namespace std::chrono_literals;
    using foundation::FramePacer;
    using foundation::FramePacerSettings;
    using foundation::PacingMode;
    using Clock = foundation::PacerClock::Clock;

    class FakeClock final : public foundation::PacerClock
    {
    public:
        [[nodiscard]] Clock::time_point Now() noexcept override
        {
            return now;
        }

        void SleepUntil(Clock::time_point deadline) noexcept override
        {
            ++Sleeps;

            // a real sleep can wake late; Overshoot plays that
            if (deadline > now)
            {
                now = deadline + Overshoot;
            }
        }

        Clock::duration Overshoot{ 0 };

        void Advance(Clock::duration work) noexcept
        {
            now += work;
        }

        int32_t Sleeps{ 0 };

    private:
        Clock::time_point now{ Clock::time_point{} + 1h };
    };

    // Stands in for the swapchain's present wait: records each id and takes WaitTime to return
    class FakeDisplay
    {
    public:
        explicit FakeDisplay(FakeClock& clock) noexcept : clock{ &clock }
        {
        }

        bool Wait(uint64_t present_id, uint64_t timeout_ns)
        {
            WaitedIds.push_back(present_id);
            LastTimeoutNs = timeout_ns;
            clock->Advance(WaitTime);
            return Succeeds;
        }

        std::vector<uint64_t> WaitedIds;
        uint64_t LastTimeoutNs{ 0u };
        Clock::duration WaitTime{ 0 };
        bool Succeeds{ true };

    private:
        FakeClock* clock;
    };

    [[nodiscard]] FramePacerSettings Settings(PacingMode mode, double cadence_hz, double vrr_max_hz, double vrr_guard_hz,
                                              uint32_t max_frames_to_display)
    {
        return FramePacerSettings
        {
            .Mode = mode,
            .CadenceHz = cadence_hz,
            .VrrMaxHz = vrr_max_hz,
            .VrrGuardHz = vrr_guard_hz,
            .MaxFramesToDisplay = max_frames_to_display,
            .PresentWaitTimeout = 100ms,
        };
    }

    // Runs `frames` frames of `work` each, and returns the time between each frame start and the one before it
    [[nodiscard]] std::vector<Clock::duration> FrameStartGaps(FramePacer& pacer, FakeClock& clock, int32_t frames, Clock::duration work)
    {
        std::vector<Clock::duration> gaps;
        Clock::time_point previous{};

        for (int32_t frame = 0; frame < frames; ++frame)
        {
            const foundation::FramePacerWait waited = pacer.WaitForFrameStart(0u);
            const Clock::time_point start = clock.Now();
            // nothing runs between the pacer and here on the fake clock, so its Start is now
            EXPECT_EQ(waited.Start, start);

            if (frame > 0)
            {
                gaps.push_back(start - previous);
            }

            previous = start;
            clock.Advance(work);
        }

        return gaps;
    }

    [[nodiscard]] double Milliseconds(Clock::duration duration)
    {
        return std::chrono::duration<double, std::milli>(duration).count();
    }

}

TEST(FramePacerTest, RecommendedGuardIsTwoHertzOrTwoPercent)
{
    EXPECT_NEAR(foundation::RecommendedVrrGuardHz(60.0), 2.0, 1.0e-12);
    EXPECT_NEAR(foundation::RecommendedVrrGuardHz(144.0), 2.88, 1.0e-12);
    EXPECT_NEAR(foundation::RecommendedVrrGuardHz(240.0), 4.8, 1.0e-12);
}

TEST(FramePacerTest, UncappedNeverSleeps)
{
    FakeClock clock;
    FramePacer pacer{ clock, Settings(PacingMode::Uncapped, 0.0, 0.0, 0.0, 0u) };
    const std::vector<Clock::duration> gaps = FrameStartGaps(pacer, clock, 10, 1ms);

    EXPECT_EQ(clock.Sleeps, 0);

    for (const Clock::duration gap : gaps)
    {
        EXPECT_EQ(gap, 1ms);
    }
}

TEST(FramePacerTest, VrrCeilingDoesNotWaitInsideTheWindow)
{
    // 10 ms of work is 100 Hz, inside a 48 to 240 Hz window: no frame may wait
    FakeClock clock;
    FramePacer pacer{ clock, Settings(PacingMode::VrrCeiling, 0.0, 240.0, 4.8, 0u) };
    const std::vector<Clock::duration> gaps = FrameStartGaps(pacer, clock, 20, 10ms);

    EXPECT_EQ(clock.Sleeps, 0);

    for (const Clock::duration gap : gaps)
    {
        EXPECT_EQ(gap, 10ms);
    }
}

TEST(FramePacerTest, VrrCeilingHoldsFastFramesAtTheCeiling)
{
    // 1 ms of work would be 1000 Hz; every frame start must be one ceiling interval (1 / 235.2 Hz) after the last
    FakeClock clock;
    FramePacer pacer{ clock, Settings(PacingMode::VrrCeiling, 0.0, 240.0, 4.8, 0u) };
    const std::vector<Clock::duration> gaps = FrameStartGaps(pacer, clock, 20, 1ms);
    const double ceilingMs = 1000.0 / 235.2;

    for (const Clock::duration gap : gaps)
    {
        EXPECT_NEAR(Milliseconds(gap), ceilingMs, 1.0e-4);
    }
}

TEST(FramePacerTest, VrrCeilingDoesNotBurstAfterASlowFrame)
{
    // a frame a little slower than the ceiling (1.5 intervals) must not let the next one start early to "catch up":
    // that pair of frames would cross the ceiling. A fixed cadence does catch up from this.
    FakeClock clock;
    FramePacer pacer{ clock, Settings(PacingMode::VrrCeiling, 0.0, 240.0, 4.8, 0u) };
    const double ceilingMs = 1000.0 / 235.2;
    std::vector<Clock::duration> gaps = FrameStartGaps(pacer, clock, 3, 1ms);
    [[maybe_unused]] const foundation::FramePacerWait slow = pacer.WaitForFrameStart(0u);
    clock.Advance(pacer.MinFrameInterval() * 3 / 2);
    Clock::time_point previous = clock.Now();

    for (int32_t frame = 0; frame < 5; ++frame)
    {
        [[maybe_unused]] const foundation::FramePacerWait waited = pacer.WaitForFrameStart(0u);
        const Clock::time_point start = clock.Now();

        if (frame > 0)
        {
            EXPECT_GE(Milliseconds(start - previous), ceilingMs - 1.0e-4);
        }

        previous = start;
        clock.Advance(1ms);
    }
}

TEST(FramePacerTest, VrrCeilingCountsFromTheRealStart)
{
    // every sleep wakes 0.3 ms late (LoomTriangleTest saw this on the real clock): a ceiling that kept a grid would
    // make the next gap 0.3 ms short. No gap may be under the ceiling interval.
    FakeClock clock;
    clock.Overshoot = 300us;
    FramePacer pacer{ clock, Settings(PacingMode::VrrCeiling, 0.0, 240.0, 4.8, 0u) };
    const std::vector<Clock::duration> gaps = FrameStartGaps(pacer, clock, 20, 1ms);

    for (const Clock::duration gap : gaps)
    {
        EXPECT_GE(gap, pacer.MinFrameInterval());
    }
}

TEST(FramePacerTest, FixedCadenceMakesUpForALateWake)
{
    // the same late wakes under a fixed cadence: the grid holds, so the mean stays on the cadence
    FakeClock clock;
    clock.Overshoot = 300us;
    FramePacer pacer{ clock, Settings(PacingMode::FixedCadence, 60.0, 0.0, 0.0, 0u) };
    const std::vector<Clock::duration> gaps = FrameStartGaps(pacer, clock, 31, 5ms);
    Clock::duration total{ 0 };

    for (const Clock::duration gap : gaps)
    {
        total += gap;
    }

    EXPECT_NEAR(Milliseconds(total) / static_cast<double>(gaps.size()), 1000.0 / 60.0, 0.3 / 30.0 + 1.0e-4);
}

TEST(FramePacerTest, FixedCadenceKeepsItsSchedule)
{
    // 60 Hz with 5 ms of work: frame starts land on the 16.67 ms grid, not 16.67 ms after each frame's end
    FakeClock clock;
    FramePacer pacer{ clock, Settings(PacingMode::FixedCadence, 60.0, 0.0, 0.0, 0u) };
    const std::vector<Clock::duration> gaps = FrameStartGaps(pacer, clock, 30, 5ms);

    for (const Clock::duration gap : gaps)
    {
        EXPECT_NEAR(Milliseconds(gap), 1000.0 / 60.0, 1.0e-4);
    }
}

TEST(FramePacerTest, FixedCadenceCatchesUpFromOneLateFrame)
{
    // one frame 20 ms long: the next starts at once, and the one after is back on the grid
    FakeClock clock;
    FramePacer pacer{ clock, Settings(PacingMode::FixedCadence, 60.0, 0.0, 0.0, 0u) };
    const Clock::time_point first = clock.Now();
    [[maybe_unused]] const foundation::FramePacerWait frame0 = pacer.WaitForFrameStart(0u);
    clock.Advance(20ms);
    [[maybe_unused]] const foundation::FramePacerWait frame1 = pacer.WaitForFrameStart(0u);
    EXPECT_EQ(clock.Now() - first, 20ms);
    clock.Advance(1ms);
    [[maybe_unused]] const foundation::FramePacerWait frame2 = pacer.WaitForFrameStart(0u);

    EXPECT_NEAR(Milliseconds(clock.Now() - first), 2000.0 / 60.0, 1.0e-4);
}

TEST(FramePacerTest, FixedCadenceRestartsAfterALongStall)
{
    // a stall longer than two intervals starts a new grid from the stall's end, instead of a burst of frames
    FakeClock clock;
    FramePacer pacer{ clock, Settings(PacingMode::FixedCadence, 60.0, 0.0, 0.0, 0u) };
    [[maybe_unused]] const foundation::FramePacerWait frame0 = pacer.WaitForFrameStart(0u);
    clock.Advance(100ms);
    [[maybe_unused]] const foundation::FramePacerWait frame1 = pacer.WaitForFrameStart(0u);
    const Clock::time_point restart = clock.Now();
    clock.Advance(1ms);
    [[maybe_unused]] const foundation::FramePacerWait frame2 = pacer.WaitForFrameStart(0u);

    EXPECT_NEAR(Milliseconds(clock.Now() - restart), 1000.0 / 60.0, 1.0e-4);
}

TEST(FramePacerTest, LatencyLimitWaitsForTheRightPresent)
{
    // a limit of 2: with present N the last one, present N - 1 must have reached the display
    FakeClock clock;
    FakeDisplay display{ clock };
    FramePacer pacer{ clock, Settings(PacingMode::Uncapped, 0.0, 0.0, 0.0, 2u) };
    pacer.SetPresentWait(foundation::PresentWaitDelegate::Create<FakeDisplay, &FakeDisplay::Wait>(&display));

    [[maybe_unused]] const foundation::FramePacerWait none = pacer.WaitForFrameStart(0u);
    [[maybe_unused]] const foundation::FramePacerWait tooEarly = pacer.WaitForFrameStart(1u);
    [[maybe_unused]] const foundation::FramePacerWait second = pacer.WaitForFrameStart(2u);
    [[maybe_unused]] const foundation::FramePacerWait later = pacer.WaitForFrameStart(9u);

    EXPECT_EQ(display.WaitedIds, (std::vector<uint64_t>{ 1u, 8u }));
    EXPECT_EQ(display.LastTimeoutNs, 100'000'000u);
}

TEST(FramePacerTest, LatencyLimitOfOneWaitsForTheLastPresent)
{
    FakeClock clock;
    FakeDisplay display{ clock };
    FramePacer pacer{ clock, Settings(PacingMode::Uncapped, 0.0, 0.0, 0.0, 1u) };
    pacer.SetPresentWait(foundation::PresentWaitDelegate::Create<FakeDisplay, &FakeDisplay::Wait>(&display));
    [[maybe_unused]] const foundation::FramePacerWait waited = pacer.WaitForFrameStart(5u);

    EXPECT_EQ(display.WaitedIds, (std::vector<uint64_t>{ 5u }));
}

TEST(FramePacerTest, LatencyLimitNeedsAPresentWait)
{
    // no delegate: the limit cannot apply, and the pacer must not fail for it
    FakeClock clock;
    FramePacer pacer{ clock, Settings(PacingMode::Uncapped, 0.0, 0.0, 0.0, 1u) };
    const foundation::FramePacerWait waited = pacer.WaitForFrameStart(5u);

    EXPECT_EQ(waited.PresentWait, 0ns);
    EXPECT_FALSE(waited.PresentWaitFailed);
}

TEST(FramePacerTest, ReportsTheWaitsItMade)
{
    FakeClock clock;
    FakeDisplay display{ clock };
    display.WaitTime = 3ms;
    FramePacer pacer{ clock, Settings(PacingMode::VrrCeiling, 0.0, 100.0, 2.0, 1u) };
    pacer.SetPresentWait(foundation::PresentWaitDelegate::Create<FakeDisplay, &FakeDisplay::Wait>(&display));
    [[maybe_unused]] const foundation::FramePacerWait first = pacer.WaitForFrameStart(0u);
    clock.Advance(1ms);
    const foundation::FramePacerWait second = pacer.WaitForFrameStart(1u);

    // the display wait took 3 ms, so the sleep covers the rest of the 1 / 98 Hz interval after 1 + 3 ms
    EXPECT_EQ(second.PresentWait, 3ms);
    EXPECT_NEAR(Milliseconds(second.Sleep), 1000.0 / 98.0 - 4.0, 1.0e-4);
    EXPECT_FALSE(second.PresentWaitFailed);

    display.Succeeds = false;
    clock.Advance(20ms);
    const foundation::FramePacerWait failed = pacer.WaitForFrameStart(2u);

    EXPECT_TRUE(failed.PresentWaitFailed);
}

TEST(FramePacerTest, ConfigureRestartsTheSchedule)
{
    FakeClock clock;
    FramePacer pacer{ clock, Settings(PacingMode::FixedCadence, 30.0, 0.0, 0.0, 0u) };
    [[maybe_unused]] const foundation::FramePacerWait first = pacer.WaitForFrameStart(0u);
    pacer.Configure(Settings(PacingMode::FixedCadence, 120.0, 0.0, 0.0, 0u));
    const Clock::time_point before = clock.Now();
    [[maybe_unused]] const foundation::FramePacerWait restarted = pacer.WaitForFrameStart(0u);

    EXPECT_EQ(clock.Now(), before);
    EXPECT_NEAR(Milliseconds(pacer.MinFrameInterval()), 1000.0 / 120.0, 1.0e-4);
}
