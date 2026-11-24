#pragma once
#ifndef FOUNDATION_TIMING_FRAME_PACER_HPP
#define FOUNDATION_TIMING_FRAME_PACER_HPP
#include "timing/HighResolutionWait.hpp"
#include "utility/Delegate.hpp"
#include <chrono>
#include <cstdint>

namespace foundation
{

/** Where the frame pacer reads the time and sleeps. SystemPacerClock is the real one; tests pass a fake. */
class PacerClock
{
public:
    using Clock = std::chrono::steady_clock;

    virtual ~PacerClock() = default;

    [[nodiscard]] virtual Clock::time_point Now() noexcept = 0;
    virtual void SleepUntil(Clock::time_point deadline) noexcept = 0;
};

/** Sleeps with HighResolutionWait to within spin_margin of the deadline, then spins: a sleep can overshoot, a spin
 *  cannot. One instance per pacing thread (HighResolutionWait's rule). */
class SystemPacerClock final : public PacerClock
{
public:
    explicit SystemPacerClock(std::chrono::microseconds spin_margin) noexcept;

    [[nodiscard]] Clock::time_point Now() noexcept override;
    void SleepUntil(Clock::time_point deadline) noexcept override;

private:
    HighResolutionWait highResolutionWait;
    std::chrono::microseconds spinMargin;
};

enum class PacingMode : uint8_t
{
    Invalid = 0,
    Uncapped = 1,       // no CPU wait; only the latency limit applies
    FixedCadence = 2,   // frames start on a fixed schedule at CadenceHz; a late frame restarts the schedule
    VrrCeiling = 3      // a frame starts no sooner than 1 / (VrrMaxHz - VrrGuardHz) after the one before it
};

/** Every field is read for the modes that use it; the others are ignored. */
struct FramePacerSettings
{
    PacingMode Mode{ PacingMode::Invalid };
    double CadenceHz{ 0.0 };
    // VrrCeiling: the top of the display's variable range (the active mode's refresh rate), and the margin kept below it
    double VrrMaxHz{ 0.0 };
    double VrrGuardHz{ 0.0 };
    // The latency limit, for every mode: when a frame starts, at most this many presented frames may still be on their
    // way to the display. 1 is the lowest latency; 0 turns the limit off. It needs a present wait (SetPresentWait).
    uint32_t MaxFramesToDisplay{ 0u };
    std::chrono::nanoseconds PresentWaitTimeout{ 0 };
};

/** The guard below a VRR display's top rate: 2 Hz, or 2 % of the top rate where that is more. 144 Hz gives 2.88 Hz,
 *  240 Hz gives 4.8 Hz. Above the top rate the driver queues frames (FIFO) or tears (IMMEDIATE). */
[[nodiscard]] constexpr double RecommendedVrrGuardHz(double max_hz) noexcept
{
    const double proportional = 0.02 * max_hz;
    return proportional > 2.0 ? proportional : 2.0;
}

/** Waits until the present with the given id (or a later one) reached the display, for at most timeout_ns. True when
 *  it did, or when there is nothing to wait for; false on a timeout or an error. The app binds the swapchain's wait. */
using PresentWaitDelegate = Delegate<bool(uint64_t present_id, uint64_t timeout_ns)>;

/** What one WaitForFrameStart spent, for statistics. */
struct FramePacerWait
{
    std::chrono::nanoseconds PresentWait{ 0 };
    std::chrono::nanoseconds Sleep{ 0 };
    bool PresentWaitFailed{ false };
};

/** Decides when the next frame starts. Call WaitForFrameStart once per frame, before input and before the frame's
 *  work, with the id of the last present (0 when there is none). Single-threaded: the thread that runs the frame loop
 *  owns it. FrameLimiter stays as it is for the old fixtures; this replaces it for new frame loops. */
class FramePacer
{
public:
    using Clock = PacerClock::Clock;

    FramePacer(PacerClock& pacer_clock, const FramePacerSettings& initial_settings) noexcept;

    /** Takes new settings and restarts the schedule: the next frame starts at once. */
    void Configure(const FramePacerSettings& new_settings) noexcept;
    /** An empty delegate turns the latency limit off. */
    void SetPresentWait(PresentWaitDelegate wait) noexcept;

    [[nodiscard]] FramePacerWait WaitForFrameStart(uint64_t last_present_id) noexcept;

    /** The shortest time between two frame starts; zero when the mode sets none. */
    [[nodiscard]] Clock::duration MinFrameInterval() const noexcept;
    [[nodiscard]] const FramePacerSettings& Settings() const noexcept;

private:
    [[nodiscard]] FramePacerWait waitForDisplay(uint64_t last_present_id) noexcept;
    [[nodiscard]] std::chrono::nanoseconds waitForSchedule() noexcept;

    PacerClock* clock{ nullptr };
    FramePacerSettings settings;
    PresentWaitDelegate presentWait;
    Clock::duration interval{};
    Clock::time_point nextStart{};
    bool started{ false };
};

} // namespace foundation

#endif // !FOUNDATION_TIMING_FRAME_PACER_HPP
