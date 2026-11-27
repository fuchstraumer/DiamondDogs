#include "Compile.hpp"
#include "CompiledGraph.hpp"
#include "Declarations.hpp"
#include "Device.hpp"
#include "DeviceCapability.hpp"
#include "DeviceConfig.hpp"
#include "DeviceFilter.hpp"
#include "DeviceStage.hpp"
#include "FramePlan.hpp"
#include "FrameTarget.hpp"
#include "GraphBuilder.hpp"
#include "HeadlessBoot.hpp"
#include "LoomDrawGraph.hpp"
#include "ImageDataFormats.hpp"
#include "LoadedBundle.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "LoomGraph.hpp"
#include "LoomKeys.hpp"
#include "PassBuilders.hpp"
#include "PassStatements.hpp"
#include "ResourceContext.hpp"
#include "ResourceFlags.hpp"
#include "ResourceFuture.hpp"
#include "ResourceTypes.hpp"
#include "RhiResourceFlags.hpp"
#include "RhiFlags.hpp"
#include "RhiSystem.hpp"
#include "ShaderBundle.hpp"
#include "ShaderProgram.hpp"
#include "SpineNeeds.hpp"
#include "TargetExtents.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

// Loom step 7 on the GPU, headless, against the CPU. One graph of four passes into an offscreen frame target:
//  - Cull (compute) writes one indirect draw (3 vertices from vertex 3) and a draw count of 1.
//  - Prepass draws depth only, with a depth clear and back faces culled. It is a path list: through DrawIndirectCount
//    when the device has it, else through DrawIndirect. The test runs the device's choice, then the fallback forced.
//  - GBuffer draws vertices 0 to 2 (the same triangle) into two colors, depth Equal with no write, back faces culled.
//  - Light draws a fullscreen triangle into the frame target: albedo x normal.z, at the UV that LoomFrame scales.
// The triangle is counterclockwise in Y-up NDC, so it shows only if the negative viewport keeps that winding front.
// Each pixel away from the triangle's edges must match the CPU within one code value. Then Light is disabled, and the
// frame target must hold the output's Clear fallback. Synchronization validation is on: a missing attachment or
// present barrier prints a message, and the run fails.

namespace
{
    using namespace loom_draw;

    constexpr int32_t k_Size = 64;
    constexpr int32_t k_FrameSlots = 2;
    constexpr int32_t k_Ticks = 4;
    constexpr uint64_t k_TimeoutNs = 5'000'000'000ull;
    constexpr int32_t k_Tolerance = 1;

    constexpr bootstrap::BundleRequest k_Bundles[]
    {
        bootstrap::BundleRequest
        {
            .FileName = "ShaderLibrary.ldmanifest",
            .BuildTreePath = LOOM_DRAW_TEST_MANIFEST,
            .Profile = rhi::k_SpirvProfileName,
        },
    };

    // The offscreen frame target's image: rgba8 unorm, rendered to, then copied out
    std::optional<GraphicsResource> CreateScreen(ResourceContext& resources)
    {
        ImageCreateInfo info{};
        info.Domain = ResourceDomain::GPUOnly;
        info.Usage = ImageUsageBits::RenderTargetWrite | ImageUsageBits::TransferSource;
        info.Type = ImageType::Type2D;
        info.Format = ImageFormat{ ImageComponentFormats::RGBA8, ImageDataType::UNorm };
        info.Width = static_cast<uint32_t>(k_Size);
        info.Height = static_cast<uint32_t>(k_Size);
        info.Depth = 1u;
        info.Name = "LoomDraw.Screen";

        ImageViewCreateInfo view{};
        view.ViewType = ImageViewType::Type2D;
        view.Range = ImageRange{ ImageAspectFlags::Color, 0u, 1u, 0u, 1u };
        info.ViewInfo = view;

        rhi::ResourceFuture<GraphicsResource> reply = resources.CreateImage(info);

        if (resources.Wait(reply, k_TimeoutNs) != rhi::FutureState::Complete)
        {
            return std::nullopt;
        }

        return reply.Get();
    }

    // Copies the frame target out behind the graph's last submit. Empty when a step failed.
    std::vector<std::byte> ReadScreen(ResourceContext& resources, const loom::Graph& graph, GraphicsResource screen)
    {
        constexpr size_t k_Bytes = size_t{ k_Size } * size_t{ k_Size } * 4u;

        BufferCreateInfo readbackInfo{};
        readbackInfo.Domain = ResourceDomain::GPUToCPU;
        readbackInfo.Usage = BufferUsageBits::TransferDestination;
        readbackInfo.Size = k_Bytes;
        rhi::ResourceFuture<GraphicsResource> readbackReply = resources.CreateBuffer(readbackInfo, {});

        if (resources.Wait(readbackReply, k_TimeoutNs) != rhi::FutureState::Complete)
        {
            return {};
        }

        const GraphicsResource readback = readbackReply.Get();
        const rhi::GpuWait last = graph.LastSubmit();
        const rhi::TransferWait afterGraph{ last.Semaphore, last.Value };
        const rhi::ImageUsageTransition usage{ .Before = ImageUsageBits::TransferSource, .After = ImageUsageBits::TransferSource };
        rhi::OperationFuture copyReply = resources.CopyImageToBuffer(screen, usage, readback, std::span{ &afterGraph, 1u });
        rhi::ResourceFuture<void*> mapReply = resources.MapBuffer(readback, k_Bytes, 0u);
        std::vector<std::byte> result;

        const bool copied = resources.Wait(copyReply, k_TimeoutNs) == rhi::FutureState::Complete &&
                            resources.Wait(mapReply, k_TimeoutNs) == rhi::FutureState::Complete;

        if (copied)
        {
            const std::byte* mapped = static_cast<const std::byte*>(mapReply.Get());
            result.assign(mapped, mapped + k_Bytes);
        }

        rhi::OperationFuture unmapReply = resources.UnmapBuffer(readback, k_Bytes, 0u);
        rhi::OperationFuture destroyReply = resources.DestroyResource(readback);
        [[maybe_unused]] const rhi::FutureState unmapped = resources.Wait(unmapReply, k_TimeoutNs);
        [[maybe_unused]] const rhi::FutureState destroyed = resources.Wait(destroyReply, k_TimeoutNs);
        return result;
    }

    // Ticks until `count` frames are submitted, and checks that each pass ran or skipped as `light_enabled` says
    bool TickFrames(loom::Graph& graph, ResourceContext& resources, int32_t count, bool light_enabled)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::nanoseconds(k_TimeoutNs);
        int32_t submitted = 0;

        // whole frames: the shader objects are made on Loom's compile thread, so wait until every pipeline exists
        while (!graph.Ready() && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::yield();
        }

        while (submitted < count && std::chrono::steady_clock::now() < deadline)
        {
            const ConsumerTimelineValue pending{ graph.Timeline(), graph.PendingValue() };
            resources.Tick(std::span{ &pending, 1u });

            const loom::TickResult result = graph.Tick(std::chrono::duration<double>{ 1.0 / 60.0 });

            switch (result.Status)
            {
            case loom::TickStatus::Submitted:
                ++submitted;
                break;
            case loom::TickStatus::NoFrameSlot:
                if (!graph.WaitForFrameSlot(std::chrono::nanoseconds(k_TimeoutNs)))
                {
                    std::cerr << "the frame slot did not come free\n";
                    return false;
                }

                break;
            case loom::TickStatus::NothingToRun:
                [[fallthrough]];
            case loom::TickStatus::TargetNotReady:
                std::this_thread::yield();
                break;
            case loom::TickStatus::Invalid:
                [[fallthrough]];
            case loom::TickStatus::Failed:
                std::cerr << "Tick failed\n" << graph.ExplainLastTick();
                return false;
            }
        }

        if (submitted != count)
        {
            std::cerr << "timed out before every Tick was submitted\n" << graph.ExplainLastTick();
            return false;
        }

        const std::span<const loom::PassDecision> decisions = graph.LastPlan().Decisions;
        const bool lightRan = decisions[Light].Reason == loom::SkipReason::Ran;
        const bool othersRan = decisions[Cull].Reason == loom::SkipReason::Ran &&
                               decisions[Prepass].Reason == loom::SkipReason::Ran &&
                               decisions[GBuffer].Reason == loom::SkipReason::Ran;

        if (!othersRan || lightRan != light_enabled)
        {
            std::cerr << "the last Tick ran the wrong passes\n" << graph.ExplainLastTick();
            return false;
        }

        return true;
    }

    // Twice the signed area of (a, b, p), in NDC: positive when p is left of a->b (counterclockwise, Y-up)
    float EdgeFunction(const std::array<float, 4u>& a, const std::array<float, 4u>& b, float x, float y) noexcept
    {
        return (b[0] - a[0]) * (y - a[1]) - (b[1] - a[1]) * (x - a[0]);
    }

    // The distance from (x, y) to the triangle's nearest edge, negative outside
    float InsideDistance(float x, float y) noexcept
    {
        float nearest = 1.0e9f;

        for (size_t edge = 0u; edge < 3u; ++edge)
        {
            const std::array<float, 4u>& a = k_Triangle[edge];
            const std::array<float, 4u>& b = k_Triangle[(edge + 1u) % 3u];
            const float length = std::hypot(b[0] - a[0], b[1] - a[1]);
            nearest = std::min(nearest, EdgeFunction(a, b, x, y) / length);
        }

        return nearest;
    }

    int32_t UnormByte(float value) noexcept
    {
        return static_cast<int32_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
    }

    // The CPU model of one pixel, or nothing for a pixel too near an edge to call. Row 0 is the top (NDC y = +1).
    std::optional<std::array<int32_t, 4u>> ExpectedLit(int32_t x, int32_t y) noexcept
    {
        constexpr float k_EdgeMargin = 2.0f / static_cast<float>(k_Size);

        const float ndcX = (static_cast<float>(x) + 0.5f) / static_cast<float>(k_Size) * 2.0f - 1.0f;
        const float ndcY = 1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(k_Size) * 2.0f;
        const float inside = InsideDistance(ndcX, ndcY);

        if (std::abs(inside) < k_EdgeMargin)
        {
            return std::nullopt;
        }

        if (inside < 0.0f)
        {
            return std::array<int32_t, 4u>{ 0, 0, 0, 255 };
        }

        // GBufferPS: albedo (uv, 0.25) with uv = xy * 0.5 + 0.5, and normal.z = 1
        return std::array<int32_t, 4u>{ UnormByte(ndcX * 0.5f + 0.5f), UnormByte(ndcY * 0.5f + 0.5f), UnormByte(0.25f), 255 };
    }

    // Compares each pixel that `expected` calls, within k_Tolerance. Prints the first few misses.
    int32_t CheckPixels(std::span<const std::byte> bytes, std::string_view name,
                        std::optional<std::array<int32_t, 4u>> (*expected)(int32_t, int32_t))
    {
        if (bytes.size() < size_t{ k_Size } * size_t{ k_Size } * 4u)
        {
            std::cerr << std::format("{}: the readback failed\n", name);
            return 1;
        }

        int32_t checked = 0;
        int32_t covered = 0;
        int32_t mismatches = 0;

        for (int32_t y = 0; y < k_Size; ++y)
        {
            for (int32_t x = 0; x < k_Size; ++x)
            {
                const std::optional<std::array<int32_t, 4u>> want = expected(x, y);

                if (!want.has_value())
                {
                    continue;
                }

                const size_t offset = (static_cast<size_t>(y) * k_Size + static_cast<size_t>(x)) * 4u;
                std::array<int32_t, 4u> got{};
                bool same = true;

                for (size_t channel = 0u; channel < 4u; ++channel)
                {
                    got[channel] = std::to_integer<int32_t>(bytes[offset + channel]);
                    same = same && std::abs(got[channel] - (*want)[channel]) <= k_Tolerance;
                }

                ++checked;
                covered += (*want)[0] != 0 || (*want)[1] != 0 ? 1 : 0;

                if (same)
                {
                    continue;
                }

                if (mismatches < 8)
                {
                    std::cerr << std::format("{} pixel ({}, {}): expected ({}, {}, {}, {}), got ({}, {}, {}, {})\n", name, x, y,
                                             (*want)[0], (*want)[1], (*want)[2], (*want)[3], got[0], got[1], got[2], got[3]);
                }

                ++mismatches;
            }
        }

        if (mismatches != 0)
        {
            std::cerr << std::format("{}: {} of {} pixels differ from the CPU\n", name, mismatches, checked);
            return 1;
        }

        std::cout << std::format("{}: {} pixels match the CPU ({} inside the triangle)\n", name, checked, covered);
        return 0;
    }

    std::optional<std::array<int32_t, 4u>> ExpectedFallback([[maybe_unused]] int32_t x, [[maybe_unused]] int32_t y) noexcept
    {
        return std::array<int32_t, 4u>{ UnormByte(k_FallbackColor[0]), UnormByte(k_FallbackColor[1]),
                                        UnormByte(k_FallbackColor[2]), UnormByte(k_FallbackColor[3]) };
    }

    void DestroyResource(ResourceContext& resources, GraphicsResource resource)
    {
        if (!resource)
        {
            return;
        }

        rhi::OperationFuture destroyReply = resources.DestroyResource(resource);
        [[maybe_unused]] const rhi::FutureState destroyed = resources.Wait(destroyReply, k_TimeoutNs);
    }

    int32_t RunGraph(ResourceContext& resources, loom::Graph& graph, const loom::OffscreenFrameTarget& target,
                     GraphicsResource screen)
    {
        int32_t failures = 0;

        if (!TickFrames(graph, resources, k_Ticks, true))
        {
            return 1;
        }

        failures += CheckPixels(ReadScreen(resources, graph, screen), "Lit frame", &ExpectedLit);

        graph.Enable(k_Light, false);

        if (!TickFrames(graph, resources, 2, false))
        {
            return failures + 1;
        }

        failures += CheckPixels(ReadScreen(resources, graph, screen), "Clear fallback", &ExpectedFallback);

        const uint32_t presents = target.PresentCount();

        if (presents != static_cast<uint32_t>(k_Ticks + 2))
        {
            std::cerr << std::format("the frame target was handed back {} times, want {}\n", presents, k_Ticks + 2);
            ++failures;
        }

        return failures;
    }

    // The path that a run must choose: the forced one, or CountPath exactly when the device has DrawIndirectCount
    loom::PathKey ExpectedPath(const bootstrap::DeviceFilter& filter, std::span<const loom::ForcedPath> forced)
    {
        if (!forced.empty())
        {
            return forced.front().Path;
        }

        return bootstrap::HasCapability(filter, bootstrap::DeviceCapability::DrawIndirectCount) ? k_CountPath : k_SinglePath;
    }

    int32_t RunTest(HeadlessBoot& boot, std::span<const loom::ForcedPath> forced)
    {
        ResourceContext& resources = boot.Gpu.Resources();
        const std::optional<GraphicsResource> screen = CreateScreen(resources);

        if (!screen.has_value())
        {
            std::cerr << "the frame target image was not created\n";
            return 1;
        }

        loom::OffscreenFrameTarget target{ *screen, lodestone::TextureFormat::RGBA8Unorm, loom::Extent2D{ k_Size, k_Size },
                                           rhi::ImageLayout::TransferSource };
        const loom::FrameTargetBinding binding{ .Key = k_Screen, .Target = target };
        loom::CollectingSink sink;
        const bootstrap::DeviceFilter filter = boot.Device.Filter();

        const RealizeArguments arguments
        {
            .Bundle = boot.Content.Bundles().front().Bundle,
            .Filter = filter,
            .Resources = resources,
            .Device = *boot.Device.Rhi().GetDevice(),
            .FrameSlots = k_FrameSlots,
            .Sink = sink,
            .Targets = std::span{ &binding, 1u },
            .TargetSettleSeconds = 0.0,
            .ForcedPaths = forced,
        };

        std::unique_ptr<loom::Graph> graph = BuildAndRealize(arguments);

        if (!graph)
        {
            DestroyResource(resources, *screen);
            return 1;
        }

        int32_t failures = 0;
        const loom::PathKey expected = ExpectedPath(filter, forced);

        if (!graph->Compiled().IsChosen(k_DepthList, expected))
        {
            std::cerr << std::format("the depth list chose '{}', want '{}'\n",
                                     graph->Compiled().ChosenPath(k_DepthList).Name, expected.Name);
            ++failures;
        }

        graph->Supply(k_Positions, CreatePositions(resources));
        failures += RunGraph(resources, *graph, target, *screen);
        const GraphicsResource positions = graph->BufferOf(k_Positions);

        // the graph waits for its own work and frees what it made; the test owns the import and the target
        graph.reset();
        DestroyResource(resources, positions);
        DestroyResource(resources, *screen);

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
    const bootstrap::DeviceConfig config
    {
        .ApiVersion = rhi::ApiVersion::Latest,
        .Validation = rhi::ValidationLayers::Full,
        .ApplicationName = "LoomDrawTest",
    };

    HeadlessBoot boot;

    if (!StartHeadlessBoot(boot, std::span{ argv, static_cast<size_t>(argc) }, k_Bundles, DeviceNeeds(), config))
    {
        return 1;
    }

    // the device's choice, then the fallback on the same device
    const loom::ForcedPath forceSingle[]{ loom::ForcedPath{ .List = k_DepthList, .Path = k_SinglePath } };
    int32_t failures = RunTest(boot, {});
    failures += RunTest(boot, forceSingle);

    if (failures != 0)
    {
        std::cerr << std::format("LoomDrawTest: {} failure(s)\n", failures);
        return 1;
    }

    std::cout << "LoomDrawTest: both depth paths, the draw passes and the Clear fallback match the CPU\n";
    return 0;
}
