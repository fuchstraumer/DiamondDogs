#include "Device.hpp"
#include "DeviceConfig.hpp"
#include "DeviceStage.hpp"
#include "FramePlan.hpp"
#include "FrameLimit.hpp"
#include "FrameTarget.hpp"
#include "LoadedBundle.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomDrawGraph.hpp"
#include "LoomGraph.hpp"
#include "PlatformSystem.hpp"
#include "PlatformTypes.hpp"
#include "ResizeTest.hpp"
#include "ResourceContext.hpp"
#include "RhiSystem.hpp"
#include "ShaderBundle.hpp"
#include "Swapchain.hpp"
#include "SwapchainFrameTarget.hpp"
#include "TargetExtents.hpp"
#include "WindowedBoot.hpp"
#include "timing/FramePacer.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <system_error>
#include <format>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

// Loom step 7 on a swapchain: LoomDrawTest's graph, drawn into SwapchainFrameTarget each frame. --frames=N exits after N
// submitted frames. --resize resizes the window three times (use at least 120 frames): the swapchain must be made again
// once per resize, and after the last resize settles, Loom's OfTarget images must have the framebuffer's extent.
// Synchronization validation is on: a missing acquire wait or present barrier prints a message, and the run fails.
// --minimize minimizes the window for a while at frame 30: no frame may be submitted while it has no area, and frames
// must resume after the restore. --no-maintenance1 makes the swapchain use core presentation, so the fallback runs on a
// device that has swapchain maintenance1. With maintenance1, the test first acquires and releases images (Release).
// --present-modes switches FIFO -> X -> FIFO for every other mode X the swapchain offers, k_FramesPerMode frames each:
// a switch the swapchain reports as switchable must not recreate it, and any other must recreate it once (with --resize
// the resizes recreate it too, so that count is not checked). With maintenance1, at least one switch must need no
// recreate where the surface offers a compatible pair. --no-scaling turns present
// scaling off (on by default where offered), and --deferred-memory turns deferred image memory on (off by default: it
// crashes the NVIDIA driver at the second recreate, see the PlatformSystem README). The test recreates the swapchain
// once before the graph so that the choice applies, and prints the recreate times.

namespace
{
    using namespace loom_draw;

    constexpr int32_t k_FrameSlots = 2;
    constexpr double k_SettleSeconds = 0.1;
    constexpr uint64_t k_TimeoutNs = 5'000'000'000ull;
    // A run with a frame limit gives up after this long with no window close
    constexpr std::chrono::seconds k_RunTimeout{ 60 };
    // Ten times the settle window
    constexpr std::chrono::seconds k_SettleTimeout{ 1 };
    // --minimize: the frame it starts at, how many Ticks run while minimized, and the pause between them
    constexpr int32_t k_MinimizeAtFrame = 30;
    constexpr int32_t k_MinimizedTicks = 60;
    // The driver may still give out one image of the old swapchain after the window lost its area; only the present
    // that follows reports out of date. After these first Ticks, nothing may be submitted.
    constexpr int32_t k_MinimizeGraceTicks = 10;
    constexpr std::chrono::milliseconds k_MinimizedTickPause{ 5 };
    // How long the window gets to report a minimize or a restore
    constexpr std::chrono::seconds k_WindowStateTimeout{ 2 };
    // Acquire-and-release cycles before the graph runs (maintenance1 only)
    constexpr int32_t k_ReleaseCycles = 10;
    // --present-modes: frames in each mode
    constexpr int32_t k_FramesPerMode = 20;
    // one frame reaches the display within a few refreshes; a second means the wait is broken
    constexpr uint64_t k_PresentWaitTimeoutNs = 1'000'000'000ull;
    // P5: the pacer's spin before a deadline, and how close its intervals must be (a mean within 2 %; a VRR gap may
    // be short by the clock's rounding only)
    constexpr std::chrono::microseconds k_PacerSpinMargin{ 300 };
    constexpr double k_CadenceTolerance = 0.02;
    constexpr double k_CeilingToleranceMs = 0.01;

    struct TestOptions
    {
        int32_t FrameLimit{ -1 };
        bool Resize{ false };
        bool Minimize{ false };
        bool CorePresentationOnly{ false };
        bool PresentModes{ false };
        bool PresentScaling{ true };
        bool DeferredMemory{ false };
        bool PresentWait{ false };
        // the best present wait the run may get: the --present-wait-v1 and --no-present-wait switches decline the rest
        PresentWaitSupport MaxPresentWait{ PresentWaitSupport::PresentWait2 };
        bool NoFifoLatestReady{ false };
        // P5: the frame pacer. Without --pacing it is Uncapped with no latency limit, which is the loop as it was.
        foundation::PacingMode Pacing{ foundation::PacingMode::Uncapped };
        double CadenceHz{ 0.0 };
        uint32_t MaxFramesToDisplay{ 0u };
    };

    // Binds the swapchain's present wait to the pacer. A wait with nothing to wait for (an id not presented yet, or one
    // that went to a replaced swapchain) counts as done; only a timeout or an error is a failure.
    class SwapchainPresentWait
    {
    public:
        explicit SwapchainPresentWait(PlatformWindowSystem& window_system) noexcept : platform{ &window_system }
        {
        }

        bool Wait(uint64_t present_id, uint64_t timeout_ns)
        {
            const PresentWaitStatus status = platform->GetActiveSwapchain()->WaitForPresent(present_id, timeout_ns);
            Presented += status == PresentWaitStatus::Presented ? 1 : 0;
            return status != PresentWaitStatus::Timeout && status != PresentWaitStatus::Failed;
        }

        // the waits that saw a present reach the display: a latency limit that never waits did nothing
        int32_t Presented{ 0 };

    private:
        PlatformWindowSystem* platform;
    };

    // What the pacer did per frame, and when each frame started
    struct PacingRecord
    {
        std::vector<double> FrameGapsMs;
        std::vector<double> PresentWaitMs;
        std::vector<double> SleepMs;
        int32_t PresentWaitFailures{ 0 };
        std::optional<std::chrono::steady_clock::time_point> LastStart;
    };

    // --present-wait: after each submitted frame, the frame before it must reach the display
    struct PresentWaitCheck
    {
        uint64_t LastSeenId{ 0u };
        uint64_t LastWaitedId{ 0u };
        int32_t Presented{ 0 };
        int32_t Retired{ 0 };
        int32_t OutOfDate{ 0 };
        // when each wait for a present returned: with FIFO the wait blocks until the display, so the gaps follow the
        // display's own intervals (present timing would give the exact times; that is a later round)
        std::vector<double> DisplayGapsMs;
        std::optional<std::chrono::steady_clock::time_point> LastDisplayed;
    };

    // --present-modes: which mode comes next, and the recreate count a switch must leave
    struct ModeCycle
    {
        std::vector<PresentMode> Modes;
        size_t Next{ 0u };
        int32_t NextSwitchFrame{ k_FramesPerMode };
        uint32_t ExpectedRecreates{ 0u };
        int32_t Switches{ 0 };
        int32_t WithoutRecreate{ 0 };
        bool CheckRecreates{ true };
    };

    // FIFO between every other mode, so that each mode is entered from FIFO and left to FIFO: FIFO is the mode every
    // surface offers, and the one other modes are most often compatible with
    [[nodiscard]] std::vector<PresentMode> ModeSequence(const std::vector<PresentMode>& supported)
    {
        std::vector<PresentMode> sequence;

        for (const PresentMode mode : supported)
        {
            if (mode == PresentMode::VerticalSync)
            {
                continue;
            }

            sequence.push_back(mode);
            sequence.push_back(PresentMode::VerticalSync);
        }

        return sequence;
    }

    [[nodiscard]] std::string_view PresentModeName(PresentMode mode)
    {
        switch (mode)
        {
        case PresentMode::Immediate:
            return "IMMEDIATE";
        case PresentMode::VerticalSync:
            return "FIFO";
        case PresentMode::VerticalSyncRelaxed:
            return "FIFO_RELAXED";
        case PresentMode::VerticalSyncMailbox:
            return "MAILBOX";
        case PresentMode::VerticalSyncLatestReady:
            return "FIFO_LATEST_READY";
        case PresentMode::SharedDemandRefresh:
            return "SHARED_DEMAND_REFRESH";
        case PresentMode::SharedContinuousRefresh:
            return "SHARED_CONTINUOUS_REFRESH";
        case PresentMode::Invalid:
            break;
        }

        return "INVALID";
    }

    // Before each switch, the earlier one must have left the recreate count it promised. Returns the failures.
    int32_t SwitchPresentMode(ModeCycle& cycle, loom::SwapchainFrameTarget& target, int32_t submitted)
    {
        int32_t failures = 0;

        if (submitted < cycle.NextSwitchFrame || cycle.Modes.empty())
        {
            return failures;
        }

        if (cycle.CheckRecreates && target.RecreateCount() != cycle.ExpectedRecreates)
        {
            std::cerr << std::format("present modes: {} recreations, expected {}\n", target.RecreateCount(),
                                     cycle.ExpectedRecreates);
            ++failures;
        }

        const PresentMode mode = cycle.Modes[cycle.Next];
        cycle.Next = (cycle.Next + 1u) % cycle.Modes.size();
        cycle.NextSwitchFrame += k_FramesPerMode;
        const PresentModeChange change = target.SetPresentMode(mode);
        ++cycle.Switches;

        switch (change)
        {
        case PresentModeChange::NextPresent:
            ++cycle.WithoutRecreate;
            break;
        case PresentModeChange::NeedsRecreate:
            ++cycle.ExpectedRecreates;
            break;
        case PresentModeChange::Unchanged:
            break;
        case PresentModeChange::Unsupported:
            [[fallthrough]];
        case PresentModeChange::Invalid:
            std::cerr << std::format("present modes: {} refused\n", PresentModeName(mode));
            ++failures;
            break;
        }

        std::cout << std::format("present mode {} at frame {}: {}\n", PresentModeName(mode), submitted,
                                 change == PresentModeChange::NextPresent ? "switched" :
                                 change == PresentModeChange::NeedsRecreate ? "recreate" : "unchanged");
        return failures;
    }

    [[nodiscard]] bool HasFlag(int argc, char* argv[], std::string_view flag)
    {
        for (int index = 1; index < argc; ++index)
        {
            if (std::string_view{ argv[index] } == flag)
            {
                return true;
            }
        }

        return false;
    }

    // The text after `prefix` in the first argument that starts with it
    [[nodiscard]] std::optional<std::string_view> FlagValue(int argc, char* argv[], std::string_view prefix)
    {
        for (int index = 1; index < argc; ++index)
        {
            const std::string_view argument{ argv[index] };

            if (argument.starts_with(prefix))
            {
                return argument.substr(prefix.size());
            }
        }

        return std::nullopt;
    }

    // --pacing=uncapped, --pacing=vrr or --pacing=cadence:<hz>. False for anything else.
    [[nodiscard]] bool ParsePacing(std::string_view value, foundation::PacingMode& mode, double& cadence_hz)
    {
        constexpr std::string_view k_Cadence{ "cadence:" };

        if (value == "uncapped")
        {
            mode = foundation::PacingMode::Uncapped;
            return true;
        }

        if (value == "vrr")
        {
            mode = foundation::PacingMode::VrrCeiling;
            return true;
        }

        if (!value.starts_with(k_Cadence))
        {
            return false;
        }

        const std::string_view number = value.substr(k_Cadence.size());
        const std::from_chars_result parsed = std::from_chars(number.data(), number.data() + number.size(), cadence_hz);
        mode = foundation::PacingMode::FixedCadence;
        return parsed.ec == std::errc{} && cadence_hz > 0.0;
    }

    constexpr bootstrap::BundleRequest k_Bundles[]
    {
        bootstrap::BundleRequest
        {
            .FileName = "ShaderLibrary.ldmanifest",
            .BuildTreePath = LOOM_DRAW_TEST_MANIFEST,
            .Profile = rhi::k_SpirvProfileName,
        },
    };

    // One Tick of the app's frame. Returns the status, after it waited for a frame slot when there was none.
    loom::TickStatus TickOnce(WindowedBoot& boot, loom::Graph& graph, std::chrono::steady_clock::time_point& last)
    {
        PlatformWindowSystem& platform = boot.Window.Platform();
        ResourceContext& resources = boot.Gpu.Resources();
        platform.Update();
        const ConsumerTimelineValue pending{ graph.Timeline(), graph.PendingValue() };
        resources.Tick(std::span{ &pending, 1u });

        const auto now = std::chrono::steady_clock::now();
        const loom::TickResult result = graph.Tick(std::chrono::duration<double>{ now - last });
        last = now;

        if (result.Status == loom::TickStatus::NoFrameSlot && !graph.WaitForFrameSlot(std::chrono::nanoseconds(k_TimeoutNs)))
        {
            return loom::TickStatus::Failed;
        }

        return result.Status;
    }

    // Polls the window until `done` holds or k_WindowStateTimeout passes
    template<typename Condition>
    [[nodiscard]] bool WaitForWindow(PlatformWindowSystem& platform, const Condition& done)
    {
        const auto deadline = std::chrono::steady_clock::now() + k_WindowStateTimeout;

        while (!done())
        {
            if (std::chrono::steady_clock::now() > deadline)
            {
                return false;
            }

            platform.Update();
            std::this_thread::sleep_for(k_MinimizedTickPause);
        }

        return true;
    }

    // --minimize: at most one submit in the first k_MinimizeGraceTicks with the window minimized, and none after them.
    // Returns the failures.
    int32_t RunMinimized(WindowedBoot& boot, loom::Graph& graph, std::chrono::steady_clock::time_point& last)
    {
        PlatformWindowSystem& platform = boot.Window.Platform();
        GLFWwindow* window = static_cast<GLFWwindow*>(platform.GetWindowHandle());
        glfwIconifyWindow(window);

        const auto hasNoArea = [&platform]()
        {
            int width = 0;
            int height = 0;
            platform.GetFramebufferSize(width, height);
            return width == 0 || height == 0;
        };

        if (!WaitForWindow(platform, hasNoArea))
        {
            std::cerr << "the window did not minimize\n";
            glfwRestoreWindow(window);
            return 1;
        }

        int32_t submitted = 0;
        int32_t lateSubmitted = 0;
        int32_t failures = 0;

        for (int32_t tick = 0; tick < k_MinimizedTicks; ++tick)
        {
            const loom::TickStatus status = TickOnce(boot, graph, last);
            const int32_t submit = status == loom::TickStatus::Submitted ? 1 : 0;
            submitted += submit;
            lateSubmitted += tick >= k_MinimizeGraceTicks ? submit : 0;
            failures += status == loom::TickStatus::Failed || status == loom::TickStatus::Invalid ? 1 : 0;
            std::this_thread::sleep_for(k_MinimizedTickPause);
        }

        glfwRestoreWindow(window);

        const auto hasArea = [&hasNoArea]()
        {
            return !hasNoArea();
        };

        std::cout << std::format("minimized: {} Ticks, {} submitted, {} after the first {}\n", k_MinimizedTicks,
                                 submitted, lateSubmitted, k_MinimizeGraceTicks);

        if (lateSubmitted != 0 || submitted > 1)
        {
            std::cerr << "frames were submitted while the window had no area\n";
            ++failures;
        }

        if (!WaitForWindow(platform, hasArea))
        {
            std::cerr << "the window did not restore\n";
            ++failures;
        }

        return failures;
    }

    // Acquires and releases k_ReleaseCycles images through the swapchain itself. Validation reports a semaphore that
    // is reused while its signal is pending. Returns the failures.
    int32_t CheckRelease(Swapchain& swapchain)
    {
        int32_t failures = 0;

        for (int32_t cycle = 0; cycle < k_ReleaseCycles; ++cycle)
        {
            const SwapchainAcquire acquire = swapchain.TryAcquire(k_TimeoutNs);

            if (acquire.Status != SwapchainAcquireStatus::Acquired && acquire.Status != SwapchainAcquireStatus::Suboptimal)
            {
                std::cerr << std::format("release check: acquire {} failed\n", cycle);
                return failures + 1;
            }

            if (swapchain.Release(acquire.ImageIndex) != SwapchainReleaseStatus::Released)
            {
                std::cerr << std::format("release check: release {} failed\n", cycle);
                ++failures;
            }
        }

        std::cout << std::format("release check: {} acquire and release cycles\n", k_ReleaseCycles);
        return failures;
    }

    // Adds the time since `last` (when there is one) to `gaps_ms`, and moves `last` to now
    void RecordGapAt(std::vector<double>& gaps_ms, std::optional<std::chrono::steady_clock::time_point>& last,
                     std::chrono::steady_clock::time_point now)
    {
        if (last.has_value())
        {
            gaps_ms.push_back(std::chrono::duration<double, std::milli>(now - *last).count());
        }

        last = now;
    }

    void RecordGap(std::vector<double>& gaps_ms, std::optional<std::chrono::steady_clock::time_point>& last)
    {
        RecordGapAt(gaps_ms, last, std::chrono::steady_clock::now());
    }

    struct Distribution
    {
        double Mean{ 0.0 };
        double Min{ 0.0 };
        double P99{ 0.0 };
        double Max{ 0.0 };
    };

    [[nodiscard]] Distribution Summarize(std::vector<double> values)
    {
        if (values.empty())
        {
            return Distribution{};
        }

        std::ranges::sort(values);
        double sum = 0.0;

        for (const double value : values)
        {
            sum += value;
        }

        const size_t p99 = std::min(values.size() - 1u, values.size() * 99u / 100u);
        return Distribution{ .Mean = sum / static_cast<double>(values.size()), .Min = values.front(), .P99 = values[p99], .Max = values.back() };
    }

    void PrintDistribution(std::string_view name, const std::vector<double>& values_ms)
    {
        const Distribution summary = Summarize(values_ms);
        std::cout << std::format("{}: {} samples, mean {:.3f} ms, min {:.3f}, p99 {:.3f}, max {:.3f}\n", name,
                                 values_ms.size(), summary.Mean, summary.Min, summary.P99, summary.Max);
    }

    [[nodiscard]] foundation::FramePacerSettings PacerSettings(const TestOptions& options, const PlatformWindowSystem& platform)
    {
        // VRR: the top of the window is the active mode's refresh rate (the EDID range only says what the display accepts)
        const double activeHz = platform.GetActiveDisplayInfo().RefreshRateCapabilities.RefreshRate;

        return foundation::FramePacerSettings
        {
            .Mode = options.Pacing,
            .CadenceHz = options.CadenceHz,
            .VrrMaxHz = activeHz,
            .VrrGuardHz = foundation::RecommendedVrrGuardHz(activeHz),
            .MaxFramesToDisplay = options.MaxFramesToDisplay,
            .PresentWaitTimeout = std::chrono::nanoseconds(k_PresentWaitTimeoutNs),
        };
    }

    // Prints what the pacer did, and checks what its mode promises. Returns the failures.
    int32_t CheckPacing(const TestOptions& options, const foundation::FramePacerSettings& settings, const PacingRecord& record)
    {
        int32_t failures = 0;
        // from the settings, not from the pacer: a pacer with a wrong interval must not set its own target
        double minIntervalMs = 0.0;

        if (options.Pacing == foundation::PacingMode::FixedCadence)
        {
            minIntervalMs = 1000.0 / settings.CadenceHz;
        }

        if (options.Pacing == foundation::PacingMode::VrrCeiling)
        {
            minIntervalMs = 1000.0 / (settings.VrrMaxHz - settings.VrrGuardHz);
        }

        std::cout << std::format("pacing: mode {}, min interval {:.3f} ms, latency limit {}, {} present wait failures\n",
                                 static_cast<int32_t>(options.Pacing), minIntervalMs, options.MaxFramesToDisplay,
                                 record.PresentWaitFailures);
        PrintDistribution("frame start gaps", record.FrameGapsMs);
        PrintDistribution("pacer sleeps", record.SleepMs);
        PrintDistribution("pacer present waits", record.PresentWaitMs);
        const Distribution gaps = Summarize(record.FrameGapsMs);

        // a fixed cadence catches up from late frames, so its mean holds even when single frames do not
        const bool cadenceOff = options.Pacing == foundation::PacingMode::FixedCadence &&
                                std::abs(gaps.Mean - minIntervalMs) > k_CadenceTolerance * minIntervalMs;

        if (cadenceOff)
        {
            std::cerr << std::format("pacing: mean frame gap {:.3f} ms, cadence {:.3f} ms\n", gaps.Mean, minIntervalMs);
            ++failures;
        }

        // a VRR ceiling never lets two frames start closer than its interval
        if (options.Pacing == foundation::PacingMode::VrrCeiling && gaps.Min < minIntervalMs - k_CeilingToleranceMs)
        {
            std::cerr << std::format("pacing: a frame gap of {:.3f} ms crossed the {:.3f} ms ceiling\n", gaps.Min, minIntervalMs);
            ++failures;
        }

        if (record.PresentWaitFailures != 0)
        {
            std::cerr << "pacing: a present wait timed out or failed\n";
            ++failures;
        }

        return failures;
    }

    // Waits for the present before the last one. Ids must never go back. Without present waits, every wait must say
    // Unsupported and no id is given. Returns the failures.
    int32_t WaitForPreviousPresent(PresentWaitCheck& check, Swapchain& swapchain)
    {
        const uint64_t id = swapchain.LastPresentId();

        if (id < check.LastSeenId)
        {
            std::cerr << std::format("present wait: the id went back from {} to {}\n", check.LastSeenId, id);
            return 1;
        }

        check.LastSeenId = id;

        if (swapchain.PresentWaitTier() == PresentWaitSupport::None)
        {
            const PresentWaitStatus status = swapchain.WaitForPresent(1u, k_PresentWaitTimeoutNs);

            if (id != 0u || status != PresentWaitStatus::Unsupported)
            {
                std::cerr << std::format("present wait: no support, but id {} and status {}\n", id, static_cast<int32_t>(status));
                return 1;
            }

            return 0;
        }

        // nothing new to wait on: the frame before the last present was waited on already
        if (id < 2u || id - 1u <= check.LastWaitedId)
        {
            return 0;
        }

        check.LastWaitedId = id - 1u;
        const PresentWaitStatus status = swapchain.WaitForPresent(id - 1u, k_PresentWaitTimeoutNs);

        switch (status)
        {
        case PresentWaitStatus::Presented:
            ++check.Presented;
            RecordGap(check.DisplayGapsMs, check.LastDisplayed);
            return 0;
        case PresentWaitStatus::Retired:
            ++check.Retired;
            return 0;
        case PresentWaitStatus::OutOfDate:
            ++check.OutOfDate;
            return 0;
        default:
            std::cerr << std::format("present wait: id {} gave status {}\n", id - 1u, static_cast<int32_t>(status));
            return 1;
        }
    }

    // Ticks the graph until the window closes or the frame limit is reached (-1: no limit). Returns the submitted
    // count, or nothing when a Tick failed.
    std::optional<int32_t> RunFrames(WindowedBoot& boot, loom::Graph& graph, loom::SwapchainFrameTarget& target,
                                     const TestOptions& options, int32_t& failures)
    {
        ModeCycle cycle;
        PresentWaitCheck waits;

        if (options.PresentModes)
        {
            cycle.Modes = ModeSequence(boot.Window.Platform().GetActiveSwapchain()->SupportedPresentModes());
            cycle.CheckRecreates = !options.Resize;
        }

        PlatformWindowSystem& platform = boot.Window.Platform();
        const auto deadline = std::chrono::steady_clock::now() + k_RunTimeout;
        auto last = std::chrono::steady_clock::now();
        int32_t submitted = 0;
        bool minimized = false;
        const int32_t frameLimit = options.FrameLimit;

        // the frame loop: pacer, then input, ResourceContext::Tick and Graph::Tick (TickOnce)
        foundation::SystemPacerClock pacerClock{ k_PacerSpinMargin };
        foundation::FramePacer pacer{ pacerClock, PacerSettings(options, platform) };
        SwapchainPresentWait displayWait{ platform };
        pacer.SetPresentWait(foundation::PresentWaitDelegate::Create<SwapchainPresentWait, &SwapchainPresentWait::Wait>(&displayWait));
        PacingRecord pacing;

        while (!platform.ShouldWindowClose() && (frameLimit < 0 || submitted < frameLimit))
        {
            if (frameLimit >= 0 && std::chrono::steady_clock::now() > deadline)
            {
                std::cerr << std::format("timed out after {} of {} frames\n", submitted, frameLimit) << graph.ExplainLastTick();
                return std::nullopt;
            }

            if (options.Resize)
            {
                ApplyScheduledResize(platform, submitted);
            }

            if (options.PresentModes)
            {
                failures += SwitchPresentMode(cycle, target, submitted);
            }

            if (options.Minimize && !minimized && submitted == k_MinimizeAtFrame)
            {
                minimized = true;
                failures += RunMinimized(boot, graph, last);
            }

            const foundation::FramePacerWait paced = pacer.WaitForFrameStart(platform.GetActiveSwapchain()->LastPresentId());
            // the pacer's own start times: its promises hold between those, not between later reads of the clock
            RecordGapAt(pacing.FrameGapsMs, pacing.LastStart, paced.Start);
            pacing.SleepMs.push_back(std::chrono::duration<double, std::milli>(paced.Sleep).count());
            pacing.PresentWaitMs.push_back(std::chrono::duration<double, std::milli>(paced.PresentWait).count());
            pacing.PresentWaitFailures += paced.PresentWaitFailed ? 1 : 0;

            switch (TickOnce(boot, graph, last))
            {
            case loom::TickStatus::Submitted:
                ++submitted;
                failures += options.PresentWait ? WaitForPreviousPresent(waits, *platform.GetActiveSwapchain()) : 0;
                break;
            case loom::TickStatus::NoFrameSlot:
                [[fallthrough]];
            case loom::TickStatus::NothingToRun:
                [[fallthrough]];
            case loom::TickStatus::TargetNotReady:
                break;
            case loom::TickStatus::Invalid:
                [[fallthrough]];
            case loom::TickStatus::Failed:
                std::cerr << "Tick failed\n" << graph.ExplainLastTick();
                return std::nullopt;
            }
        }

        if (options.PresentWait)
        {
            const Swapchain& waited = *platform.GetActiveSwapchain();
            std::cout << std::format("present wait: tier {}, {} presented, {} retired, {} out of date, last id {}\n",
                                     static_cast<int32_t>(waited.PresentWaitTier()), waits.Presented, waits.Retired,
                                     waits.OutOfDate, waited.LastPresentId());

            PrintDistribution("display gaps (present wait returns)", waits.DisplayGapsMs);

            // with any support, waits must have reached the display
            if (waited.PresentWaitTier() != PresentWaitSupport::None && waits.Presented == 0)
            {
                std::cerr << "present wait: no wait saw a present reach the display\n";
                ++failures;
            }
        }

        // a compatible pair exists (FIFO and MAILBOX or IMMEDIATE on most desktops): it must have switched in place
        const Swapchain& swapchain = *platform.GetActiveSwapchain();
        const bool pairOffered = swapchain.UsesMaintenance1() && swapchain.SwitchablePresentModes().size() > 1u;

        if (options.PresentModes && pairOffered && cycle.WithoutRecreate == 0)
        {
            std::cerr << "present modes: no switch happened without a recreate\n";
            ++failures;
        }

        if (options.PresentModes)
        {
            std::cout << std::format("present modes: {} switches, {} without a recreate\n", cycle.Switches,
                                     cycle.WithoutRecreate);
        }

        failures += CheckPacing(options, PacerSettings(options, platform), pacing);
        const bool limitCanWait = options.MaxFramesToDisplay > 0u && platform.GetActiveSwapchain()->PresentWaitTier() != PresentWaitSupport::None;
        std::cout << std::format("pacing: {} latency waits saw a present reach the display\n", displayWait.Presented);

        if (limitCanWait && displayWait.Presented == 0)
        {
            std::cerr << "pacing: the latency limit never waited for the display\n";
            ++failures;
        }

        return submitted;
    }

    // The frame limit can end the run inside the last resize's settle window: Tick on, uncounted, until the allocation
    // follows the framebuffer or k_SettleTimeout passes. CheckSettledExtents reports what remains.
    void SettleExtents(WindowedBoot& boot, loom::Graph& graph)
    {
        PlatformWindowSystem& platform = boot.Window.Platform();
        ResourceContext& resources = boot.Gpu.Resources();
        const auto deadline = std::chrono::steady_clock::now() + k_SettleTimeout;
        auto last = std::chrono::steady_clock::now();

        while (std::chrono::steady_clock::now() < deadline)
        {
            int width = 0;
            int height = 0;
            platform.GetFramebufferSize(width, height);
            const std::optional<loom::TargetExtents> extents = graph.ExtentsOf(k_Screen);
            const bool settled = extents.has_value() && extents->Allocation.Width == width &&
                                 extents->Allocation.Height == height;

            if (settled)
            {
                return;
            }

            platform.Update();
            const ConsumerTimelineValue pending{ graph.Timeline(), graph.PendingValue() };
            resources.Tick(std::span{ &pending, 1u });

            const auto now = std::chrono::steady_clock::now();
            const loom::TickResult result = graph.Tick(std::chrono::duration<double>{ now - last });
            last = now;

            if (result.Status == loom::TickStatus::NoFrameSlot)
            {
                [[maybe_unused]] const bool free = graph.WaitForFrameSlot(std::chrono::nanoseconds(k_TimeoutNs));
            }
        }
    }

    // After the last resize settled, all three extents must equal the framebuffer
    int32_t CheckSettledExtents(const PlatformWindowSystem& platform, const loom::Graph& graph)
    {
        int width = 0;
        int height = 0;
        platform.GetFramebufferSize(width, height);
        const std::optional<loom::TargetExtents> extents = graph.ExtentsOf(k_Screen);

        if (!extents.has_value())
        {
            std::cerr << "the graph has no extents for its frame target\n";
            return 1;
        }

        const auto matches = [width, height](const loom::Extent2D& extent)
        {
            return extent.Width == width && extent.Height == height;
        };

        std::cout << std::format("extents: target {}x{}, allocation {}x{}, render {}x{}, framebuffer {}x{}\n",
                                 extents->Target.Width, extents->Target.Height, extents->Allocation.Width,
                                 extents->Allocation.Height, extents->Render.Width, extents->Render.Height, width, height);

        if (!matches(extents->Target) || !matches(extents->Allocation) || !matches(extents->Render))
        {
            std::cerr << "the OfTarget images did not follow the settled swapchain extent\n";
            return 1;
        }

        return 0;
    }

    int32_t RunTest(WindowedBoot& boot, const TestOptions& options)
    {
        ResourceContext& resources = boot.Gpu.Resources();
        PlatformWindowSystem& platform = boot.Window.Platform();
        const rhi::Device& device = *boot.Device.Rhi().GetDevice();
        Swapchain& swapchain = *platform.GetActiveSwapchain();
        int32_t failures = 0;

        if (options.CorePresentationOnly)
        {
            swapchain.LimitToCorePresentation();
        }

        // the create choices apply at the next Recreate; nothing is acquired or submitted yet
        swapchain.SetPresentScaling(options.PresentScaling);
        swapchain.SetDeferredImageMemory(options.DeferredMemory);

        if (swapchain.Recreate() != SwapchainRecreateResult::Success)
        {
            std::cerr << "the first recreate failed\n";
            return 1;
        }

        std::cout << std::format("present scaling {}, deferred image memory {}\n", swapchain.UsesPresentScaling(),
                                 swapchain.UsesDeferredImageMemory());

        std::cout << std::format("swapchain maintenance1: device {}, in use {}\n",
                                 device.IsFeatureEnabled(k_SwapchainMaintenance1Feature), swapchain.UsesMaintenance1());

        if (swapchain.UsesMaintenance1())
        {
            failures += CheckRelease(swapchain);
        }

        // a declined need must lower the tier: the fallback runs on the real device, not behind a switch
        std::cout << std::format("present wait tier: {} (at most {})\n", static_cast<int32_t>(swapchain.PresentWaitTier()),
                                 static_cast<int32_t>(options.MaxPresentWait));

        if (swapchain.PresentWaitTier() > options.MaxPresentWait)
        {
            std::cerr << "the swapchain uses a present wait that the device was not given\n";
            ++failures;
        }

        // FIFO latest ready without its feature: the surface still lists it, the swapchain must not offer it
        const std::vector<PresentMode> supported = swapchain.SupportedPresentModes();
        const bool latestReadyOffered = std::ranges::contains(supported, PresentMode::VerticalSyncLatestReady);

        if (options.NoFifoLatestReady)
        {
            std::cout << std::format("fifo latest ready: feature {}, offered {}\n",
                                     device.IsFeatureEnabled(k_FifoLatestReadyFeature), latestReadyOffered);
        }

        if (options.NoFifoLatestReady && latestReadyOffered)
        {
            std::cerr << "FIFO latest ready is offered without its feature\n";
            ++failures;
        }

        loom::SwapchainFrameTarget target{ device, swapchain };
        const loom::FrameTargetBinding binding{ .Key = k_Screen, .Target = target };
        loom::CollectingSink sink;
        const bootstrap::DeviceFilter filter = boot.Device.Filter();

        const RealizeArguments arguments
        {
            .Bundle = boot.Content.Bundles().front().Bundle,
            .Filter = filter,
            .Resources = resources,
            .Device = device,
            .FrameSlots = k_FrameSlots,
            .Sink = sink,
            .Targets = std::span{ &binding, 1u },
            .TargetSettleSeconds = k_SettleSeconds,
            .ForcedPaths = {},
        };

        std::unique_ptr<loom::Graph> graph = BuildAndRealize(arguments);

        if (!graph)
        {
            return 1;
        }

        graph->Supply(k_Positions, CreatePositions(resources));
        const std::optional<int32_t> submitted = RunFrames(boot, *graph, target, options, failures);
        failures += submitted.has_value() ? 0 : 1;

        if (submitted.has_value())
        {
            std::cout << std::format("{} frames submitted\n", *submitted);
        }

        if (submitted.has_value() && options.Resize)
        {
            failures += VerifyScheduledResize(platform, target.RecreateCount());
            SettleExtents(boot, *graph);
            failures += CheckSettledExtents(platform, *graph);
        }

        const SwapchainStatistics statistics = swapchain.Statistics();
        const double meanMs = statistics.Recreates > 0u
            ? static_cast<double>(statistics.RecreateNanoseconds) / statistics.Recreates / 1.0e6
            : 0.0;
        std::cout << std::format("swapchain: {} recreates, mean {:.3f} ms, max {:.3f} ms, {} mode switches without recreate, "
                                 "{} retired with presents pending\n",
                                 statistics.Recreates, meanMs,
                                 static_cast<double>(statistics.MaxRecreateNanoseconds) / 1.0e6,
                                 statistics.PresentModeSwitches, statistics.RetiredWithPendingPresents);

        const GraphicsResource positions = graph->BufferOf(k_Positions);

        // the graph waits for its own work before the swapchain target and the import go
        graph.reset();

        if (positions)
        {
            rhi::OperationFuture destroyReply = resources.DestroyResource(positions);
            [[maybe_unused]] const rhi::FutureState destroyed = resources.Wait(destroyReply, k_TimeoutNs);
        }

        if (sink.ErrorCount() != 0)
        {
            PrintDiagnostics(sink);
            ++failures;
        }

        return failures;
    }

}

int main(int argc, char* argv[])
{
    const PlatformWindowCreateInfo windowInfo
    {
        "DiamondDogs - Loom Triangle Test",
        nullptr, // the primary display
        PlatformWindowMode::Windowed,
        1280,
        720,
        100,
        100,
        60.0f,
        PlatformWindowBehaviorFlags{ true, true, true, false, false }
    };

    // the fallback switches decline Wanted needs, so the device really lacks the feature
    const bool presentWaitV1 = HasFlag(argc, argv, "--present-wait-v1");
    const bool noPresentWait = HasFlag(argc, argv, "--no-present-wait");
    const bool noFifoLatestReady = HasFlag(argc, argv, "--no-fifo-latest-ready");
    std::vector<std::string> declined;

    if (presentWaitV1 || noPresentWait)
    {
        declined.emplace_back(k_PresentId2Feature);
        declined.emplace_back(k_PresentWait2Feature);
    }

    if (noPresentWait)
    {
        declined.emplace_back(k_PresentIdFeature);
        declined.emplace_back(k_PresentWaitFeature);
    }

    if (noFifoLatestReady)
    {
        declined.emplace_back(k_FifoLatestReadyFeature);
    }

    PresentWaitSupport maxPresentWait = PresentWaitSupport::PresentWait2;

    if (presentWaitV1)
    {
        maxPresentWait = PresentWaitSupport::PresentWait;
    }

    if (noPresentWait)
    {
        maxPresentWait = PresentWaitSupport::None;
    }

    const bootstrap::DeviceConfig config
    {
        .ApiVersion = rhi::ApiVersion::Latest,
        .Validation = rhi::ValidationLayers::Full,
        .ApplicationName = "LoomTriangleTest",
        .Declined = std::move(declined),
    };

    WindowedBoot boot;

    if (!StartWindowedBoot(boot, std::span{ argv, static_cast<size_t>(argc) }, k_Bundles, DeviceNeeds(), config, windowInfo))
    {
        return 1;
    }

    foundation::PacingMode pacing = foundation::PacingMode::Uncapped;
    double cadenceHz = 0.0;
    const std::optional<std::string_view> pacingValue = FlagValue(argc, argv, "--pacing=");

    if (pacingValue.has_value() && !ParsePacing(*pacingValue, pacing, cadenceHz))
    {
        std::cerr << std::format("unknown --pacing={} (uncapped, vrr, cadence:<hz>)\n", *pacingValue);
        return 1;
    }

    // --latency=k: at most k presented frames may be on their way to the display when a frame starts
    uint32_t maxFramesToDisplay = 0u;
    const std::optional<std::string_view> latencyValue = FlagValue(argc, argv, "--latency=");

    if (latencyValue.has_value())
    {
        const std::from_chars_result parsed = std::from_chars(latencyValue->data(), latencyValue->data() + latencyValue->size(), maxFramesToDisplay);

        if (parsed.ec != std::errc{})
        {
            std::cerr << std::format("unknown --latency={}\n", *latencyValue);
            return 1;
        }
    }

    const TestOptions options
    {
        .FrameLimit = ParseFrameLimit(argc, argv),
        .Resize = ParseResizeTest(argc, argv),
        .Minimize = HasFlag(argc, argv, "--minimize"),
        .CorePresentationOnly = HasFlag(argc, argv, "--no-maintenance1"),
        .PresentModes = HasFlag(argc, argv, "--present-modes"),
        .PresentScaling = !HasFlag(argc, argv, "--no-scaling"),
        .DeferredMemory = HasFlag(argc, argv, "--deferred-memory"),
        .PresentWait = HasFlag(argc, argv, "--present-wait") || presentWaitV1 || noPresentWait,
        .MaxPresentWait = maxPresentWait,
        .NoFifoLatestReady = noFifoLatestReady,
        .Pacing = pacing,
        .CadenceHz = cadenceHz,
        .MaxFramesToDisplay = maxFramesToDisplay,
    };

    const int32_t failures = RunTest(boot, options);

    if (failures != 0)
    {
        std::cerr << std::format("LoomTriangleTest: {} failure(s)\n", failures);
        return 1;
    }

    std::cout << "LoomTriangleTest: passed\n";
    return 0;
}
