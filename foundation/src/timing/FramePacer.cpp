#include "timing/FramePacer.hpp"
#include <chrono>
#include <ratio>
#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace
{

    using Clock = foundation::PacerClock::Clock;

    [[nodiscard]] Clock::duration IntervalOf(double hz) noexcept
    {
        if (hz <= 0.0)
        {
            return Clock::duration::zero();
        }

        return std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double, std::ratio<1, 1>>(1.0 / hz));
    }

}

namespace foundation
{

SystemPacerClock::SystemPacerClock(std::chrono::microseconds spin_margin) noexcept : spinMargin{ spin_margin }
{
}

PacerClock::Clock::time_point SystemPacerClock::Now() noexcept
{
    return Clock::now();
}

void SystemPacerClock::SleepUntil(Clock::time_point deadline) noexcept
{
    // sleep to within the spin margin, then spin
    while (true)
    {
        const Clock::duration remaining = deadline - Clock::now();

        if (remaining <= spinMargin)
        {
            break;
        }

        highResolutionWait.SleepFor(remaining - spinMargin);
    }

    while (Clock::now() < deadline)
    {
#if defined(__x86_64__) || defined(_M_X64)
        _mm_pause();
#endif
    }
}

FramePacer::FramePacer(PacerClock& pacer_clock, const FramePacerSettings& initial_settings) noexcept : clock{ &pacer_clock }
{
    Configure(initial_settings);
}

void FramePacer::Configure(const FramePacerSettings& new_settings) noexcept
{
    settings = new_settings;
    started = false;

    switch (settings.Mode)
    {
    case PacingMode::FixedCadence:
        interval = IntervalOf(settings.CadenceHz);
        break;
    case PacingMode::VrrCeiling:
        interval = IntervalOf(settings.VrrMaxHz - settings.VrrGuardHz);
        break;
    case PacingMode::Invalid:
        [[fallthrough]];
    case PacingMode::Uncapped:
        interval = Clock::duration::zero();
        break;
    }
}

void FramePacer::SetPresentWait(PresentWaitDelegate wait) noexcept
{
    presentWait = wait;
}

FramePacerWait FramePacer::WaitForFrameStart(uint64_t last_present_id) noexcept
{
    // the display first: the schedule then counts from when the display caught up
    FramePacerWait result = waitForDisplay(last_present_id);
    waitForSchedule(result);
    return result;
}

Clock::duration FramePacer::MinFrameInterval() const noexcept
{
    return interval;
}

const FramePacerSettings& FramePacer::Settings() const noexcept
{
    return settings;
}

// With a limit of k frames, the present k - 1 before the last one must have reached the display: k = 1 waits for the
// last present itself
FramePacerWait FramePacer::waitForDisplay(uint64_t last_present_id) noexcept
{
    FramePacerWait result{};
    const uint64_t limit = settings.MaxFramesToDisplay;

    if (presentWait.Empty() || limit == 0u || last_present_id < limit)
    {
        return result;
    }

    const uint64_t waitedId = last_present_id + 1u - limit;
    const uint64_t timeoutNs = static_cast<uint64_t>(settings.PresentWaitTimeout.count());
    const Clock::time_point before = clock->Now();
    result.PresentWaitFailed = !presentWait(waitedId, timeoutNs);
    result.PresentWait = std::chrono::duration_cast<std::chrono::nanoseconds>(clock->Now() - before);
    return result;
}

void FramePacer::waitForSchedule(FramePacerWait& result) noexcept
{
    const Clock::time_point now = clock->Now();
    result.Start = now;

    // the first frame after Configure starts at once
    if (interval == Clock::duration::zero() || !started)
    {
        started = true;
        nextStart = now + interval;
        return;
    }

    // a fixed cadence catches up from a frame up to one interval late; later than that, or any lateness under a VRR
    // ceiling, starts a new schedule from now instead of a burst of short frames
    const Clock::duration allowedLateness = settings.Mode == PacingMode::FixedCadence ? interval : Clock::duration::zero();

    if (now >= nextStart + allowedLateness)
    {
        nextStart = now + interval;
        return;
    }

    if (now < nextStart)
    {
        clock->SleepUntil(nextStart);
    }

    // a sleep can wake late: a fixed cadence keeps its grid, so the next frame makes up for it; a VRR ceiling counts
    // from the real start, so no two starts are ever closer than its interval
    const Clock::time_point woke = clock->Now();
    nextStart = settings.Mode == PacingMode::FixedCadence ? nextStart + interval : woke + interval;
    result.Sleep = std::chrono::duration_cast<std::chrono::nanoseconds>(woke - now);
    result.Start = woke;
}

} // namespace foundation
