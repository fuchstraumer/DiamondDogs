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
#include "SwapchainFrameTarget.hpp"
#include "TargetExtents.hpp"
#include "WindowedBoot.hpp"
#include <chrono>
#include <cstdint>
#include <format>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <vector>

// Loom step 7 on a swapchain: LoomDrawTest's graph, drawn into SwapchainFrameTarget each frame. --frames=N exits after N
// submitted frames. --resize resizes the window three times (use at least 120 frames): the swapchain must be made again
// once per resize, and after the last resize settles, Loom's OfTarget images must have the framebuffer's extent.
// Synchronization validation is on: a missing acquire wait or present barrier prints a message, and the run fails.

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

    constexpr bootstrap::BundleRequest k_Bundles[]
    {
        bootstrap::BundleRequest
        {
            .FileName = "ShaderLibrary.ldmanifest",
            .BuildTreePath = LOOM_DRAW_TEST_MANIFEST,
            .Profile = rhi::k_SpirvProfileName,
        },
    };

    // LoomDrawTest's needs, and the present fences, which the swapchain maintenance1 extension (EXT or KHR) gives.
    // Both are Wanted: without them the target waits for the whole device.
    std::vector<bootstrap::Need> TriangleNeeds()
    {
        std::vector<bootstrap::Need> needs{ DeviceNeeds().begin(), DeviceNeeds().end() };
        needs.push_back(bootstrap::Need{ .Name = "VK_KHR_surface_maintenance1", .Kind = bootstrap::NeedKind::InstanceExtension,
                                         .Level = bootstrap::NeedLevel::Wanted });
        needs.push_back(bootstrap::Need{ .Name = "VK_EXT_surface_maintenance1", .Kind = bootstrap::NeedKind::InstanceExtension,
                                         .Level = bootstrap::NeedLevel::Wanted });
        needs.push_back(bootstrap::Need{ .Name = loom::SwapchainFrameTarget::k_PresentFenceFeature,
                                         .Kind = bootstrap::NeedKind::DeviceFeature, .Level = bootstrap::NeedLevel::Wanted });
        return needs;
    }

    // Ticks the graph until the window closes or `frame_limit` frames are submitted (-1: no limit). Returns the
    // submitted count, or nothing when a Tick failed.
    std::optional<int32_t> RunFrames(WindowedBoot& boot, loom::Graph& graph, int32_t frame_limit, bool resize_test)
    {
        PlatformWindowSystem& platform = boot.Window.Platform();
        ResourceContext& resources = boot.Gpu.Resources();
        const auto deadline = std::chrono::steady_clock::now() + k_RunTimeout;
        auto last = std::chrono::steady_clock::now();
        int32_t submitted = 0;

        while (!platform.ShouldWindowClose() && (frame_limit < 0 || submitted < frame_limit))
        {
            if (frame_limit >= 0 && std::chrono::steady_clock::now() > deadline)
            {
                std::cerr << std::format("timed out after {} of {} frames\n", submitted, frame_limit) << graph.ExplainLastTick();
                return std::nullopt;
            }

            if (resize_test)
            {
                ApplyScheduledResize(platform, submitted);
            }

            platform.Update();
            const ConsumerTimelineValue pending{ graph.Timeline(), graph.PendingValue() };
            resources.Tick(std::span{ &pending, 1u });

            const auto now = std::chrono::steady_clock::now();
            const loom::TickResult result = graph.Tick(std::chrono::duration<double>{ now - last });
            last = now;

            switch (result.Status)
            {
            case loom::TickStatus::Submitted:
                ++submitted;
                break;
            case loom::TickStatus::NoFrameSlot:
                if (!graph.WaitForFrameSlot(std::chrono::nanoseconds(k_TimeoutNs)))
                {
                    std::cerr << "the frame slot did not come free\n";
                    return std::nullopt;
                }

                break;
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

    int32_t RunTest(WindowedBoot& boot, int32_t frame_limit, bool resize_test)
    {
        ResourceContext& resources = boot.Gpu.Resources();
        PlatformWindowSystem& platform = boot.Window.Platform();
        const rhi::Device& device = *boot.Device.Rhi().GetDevice();

        loom::SwapchainFrameTarget target{ device, *platform.GetActiveSwapchain(), k_FrameSlots };
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
        std::cout << std::format("present fences: {}\n", device.IsFeatureEnabled(loom::SwapchainFrameTarget::k_PresentFenceFeature));
        const std::optional<int32_t> submitted = RunFrames(boot, *graph, frame_limit, resize_test);
        int32_t failures = submitted.has_value() ? 0 : 1;

        if (submitted.has_value())
        {
            std::cout << std::format("{} frames submitted\n", *submitted);
        }

        if (submitted.has_value() && resize_test)
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

    const std::vector<bootstrap::Need> needs = TriangleNeeds();
    WindowedBoot boot;

    if (!StartWindowedBoot(boot, std::span{ argv, static_cast<size_t>(argc) }, k_Bundles, needs, config, windowInfo))
    {
        return 1;
    }

    const int32_t failures = RunTest(boot, ParseFrameLimit(argc, argv), ParseResizeTest(argc, argv));

    if (failures != 0)
    {
        std::cerr << std::format("LoomTriangleTest: {} failure(s)\n", failures);
        return 1;
    }

    std::cout << "LoomTriangleTest: passed\n";
    return 0;
}
