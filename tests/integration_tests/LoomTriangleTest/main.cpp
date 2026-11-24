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
#include <GLFW/glfw3.h>
#include <chrono>
#include <cstdint>
#include <format>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <thread>

// Loom step 7 on a swapchain: LoomDrawTest's graph, drawn into SwapchainFrameTarget each frame. --frames=N exits after N
// submitted frames. --resize resizes the window three times (use at least 120 frames): the swapchain must be made again
// once per resize, and after the last resize settles, Loom's OfTarget images must have the framebuffer's extent.
// Synchronization validation is on: a missing acquire wait or present barrier prints a message, and the run fails.
// --minimize minimizes the window for a while at frame 30: no frame may be submitted while it has no area, and frames
// must resume after the restore. --no-maintenance1 makes the swapchain use core presentation, so the fallback runs on a
// device that has swapchain maintenance1. With maintenance1, the test first acquires and releases images (Release).

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

    struct TestOptions
    {
        int32_t FrameLimit{ -1 };
        bool Resize{ false };
        bool Minimize{ false };
        bool CorePresentationOnly{ false };
    };

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

    // Ticks the graph until the window closes or the frame limit is reached (-1: no limit). Returns the submitted
    // count, or nothing when a Tick failed.
    std::optional<int32_t> RunFrames(WindowedBoot& boot, loom::Graph& graph, const TestOptions& options, int32_t& failures)
    {
        PlatformWindowSystem& platform = boot.Window.Platform();
        const auto deadline = std::chrono::steady_clock::now() + k_RunTimeout;
        auto last = std::chrono::steady_clock::now();
        int32_t submitted = 0;
        bool minimized = false;
        const int32_t frameLimit = options.FrameLimit;

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

            if (options.Minimize && !minimized && submitted == k_MinimizeAtFrame)
            {
                minimized = true;
                failures += RunMinimized(boot, graph, last);
            }

            switch (TickOnce(boot, graph, last))
            {
            case loom::TickStatus::Submitted:
                ++submitted;
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

        std::cout << std::format("swapchain maintenance1: device {}, in use {}\n",
                                 device.IsFeatureEnabled(k_SwapchainMaintenance1Feature), swapchain.UsesMaintenance1());

        if (swapchain.UsesMaintenance1())
        {
            failures += CheckRelease(swapchain);
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
        };

        std::unique_ptr<loom::Graph> graph = BuildAndRealize(arguments);

        if (!graph)
        {
            return 1;
        }

        graph->Supply(k_Positions, CreatePositions(resources));
        const std::optional<int32_t> submitted = RunFrames(boot, *graph, options, failures);
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

    const bootstrap::DeviceConfig config
    {
        .ApiVersion = rhi::ApiVersion::Latest,
        .Validation = rhi::ValidationLayers::Full,
        .ApplicationName = "LoomTriangleTest",
    };

    WindowedBoot boot;

    if (!StartWindowedBoot(boot, std::span{ argv, static_cast<size_t>(argc) }, k_Bundles, DeviceNeeds(), config, windowInfo))
    {
        return 1;
    }

    const TestOptions options
    {
        .FrameLimit = ParseFrameLimit(argc, argv),
        .Resize = ParseResizeTest(argc, argv),
        .Minimize = HasFlag(argc, argv, "--minimize"),
        .CorePresentationOnly = HasFlag(argc, argv, "--no-maintenance1"),
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
