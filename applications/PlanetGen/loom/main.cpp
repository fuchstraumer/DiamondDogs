#include "CommandBuffer.hpp"
#include "CommandPool.hpp"
#include "Compile.hpp"
#include "CompiledGraph.hpp"
#include "Device.hpp"
#include "DeviceConfig.hpp"
#include "DeviceRequirements.hpp"
#include "DeviceStage.hpp"
#include "Fence.hpp"
#include "FrameCapture.hpp"
#include "FrameLimit.hpp"
#include "FramePlan.hpp"
#include "GpuBuffers.hpp"
#include "GpuTimers.hpp"
#include "GraphBuilder.hpp"
#include "HeadlessBoot.hpp"
#include "Healpix.hpp"
#include "ImageDataFormats.hpp"
#include "LoadedBundle.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "LoomGraph.hpp"
#include "LoomKeys.hpp"
#include "MantlePlumes.hpp"
#include "PlanetGpuErrors.hpp"
#include "Queue.hpp"
#include "RhiResult.hpp"
#include "PlanetGraph.hpp"
#include "PlanetLiveApp.hpp"
#include "PlanetPresets.hpp"
#include "PlanetSession.hpp"
#include "PlanetShaders.hpp"
#include "PlanetStep.hpp"
#include "ReferenceSet.hpp"
#include "ResourceContext.hpp"
#include "ResourceFuture.hpp"
#include "ResourceTypes.hpp"
#include "RhiFlags.hpp"
#include "RhiResourceFlags.hpp"
#include "RhiSystem.hpp"
#include "ShaderBundle.hpp"
#include "SpineNeeds.hpp"
#include "StepState.hpp"
#include "math/MathStorage.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    constexpr uint64_t k_TimeoutNs = 10'000'000'000ull;

    constexpr bootstrap::BundleRequest k_Bundles[]
    {
        bootstrap::BundleRequest
        {
            .FileName = "ShaderLibrary.ldmanifest",
            .BuildTreePath = PLANET_GEN_MANIFEST,
            .Profile = rhi::k_SpirvProfileName,
        },
    };

    struct LoomOptions
    {
        std::filesystem::path Reference;
        std::filesystem::path CaptureDir;
        planet_loom::PlanetPreset Preset{ planet_loom::PlanetPreset::Reference };
        std::optional<planet_loom::ShapingPreset> Shaping;
        uint64_t Seed{ 42ull };
        uint32_t PlateCount{ 0u };
        uint32_t Order{ 7u };
        uint32_t Steps{ 10u };
        float Dt{ 0.5f };
        uint32_t Width{ 1024u };
        uint32_t Height{ 512u };
        uint32_t Mode{ 0u };
        uint32_t Projection{ 0u };
        bool Words{ false };
        bool Verify{ false };
        bool SelfVerify{ false };
        bool Timestamps{ false };
        std::optional<bool> Telemetry;
        bool Explain{ false };
        bool PlumesEnabled{ false };
        bool PlumesExplicit{ false };
        bool Rift{ true };
        bool Erosion{ true };
        bool Live{ false };
        bool Paused{ false };
        int32_t ToolPort{ -1 };
        int32_t FrameLimit{ -1 };
        std::optional<bool> ValidationFull;
    };

    struct VerifySnapshot
    {
        std::vector<std::byte> Owners;
        std::vector<std::byte> CrustFlags;
        std::vector<std::byte> CrustState;
        std::vector<std::byte> Rotations;
        std::vector<std::byte> PoleRates;
    };

    [[nodiscard]] bool ParseUint(std::string_view text, uint32_t& value)
    {
        const std::from_chars_result parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
    }

    [[nodiscard]] bool ParseUint64(std::string_view text, uint64_t& value)
    {
        const std::from_chars_result parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
    }

    [[nodiscard]] bool ParseInt(std::string_view text, int32_t& value)
    {
        const std::from_chars_result parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
    }

    [[nodiscard]] bool ParseFloat(std::string_view text, float& value)
    {
        const std::from_chars_result parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && std::isfinite(value);
    }

    [[nodiscard]] bool ParseMode(std::string_view text, uint32_t& mode)
    {
        if (ParseUint(text, mode) && mode <= 8u)
        {
            return true;
        }
        if (text == "elevation") { mode = 0u; return true; }
        if (text == "plates")    { mode = 1u; return true; }
        if (text == "age")       { mode = 2u; return true; }
        if (text == "relief")    { mode = 3u; return true; }
        if (text == "fronts")    { mode = 4u; return true; }
        if (text == "voronoi")   { mode = 5u; return true; }
        if (text == "plumes")    { mode = 6u; return true; }
        if (text == "atlas")     { mode = 7u; return true; }
        if (text == "raw")       { mode = 8u; return true; }
        return false;
    }

    [[nodiscard]] bool ParseProjection(std::string_view text, uint32_t& projection)
    {
        if (ParseUint(text, projection))
        {
            return planet_loom::ShaderIndexToProjection(static_cast<int32_t>(projection)) != planet_loom::RenderProjection::Invalid;
        }

        const planet_loom::RenderProjection named = planet_loom::ProjectionFromName(text);

        if (named == planet_loom::RenderProjection::Invalid)
        {
            return false;
        }

        projection = planet_loom::ProjectionToShaderIndex(named);
        return true;
    }

    [[nodiscard]] std::optional<LoomOptions> ParseOptions(std::span<char* const> arguments)
    {
        LoomOptions options;

        for (const char* argument : arguments.subspan(1))
        {
            const std::string_view text{ argument };
            bool valid = true;

            if (text.starts_with("--reference="))
            {
                options.Reference = text.substr(12u);
            }
            else if (text.starts_with("--preset="))
            {
                const std::optional<planet_loom::PlanetPreset> parsed =
                    planet_loom::ParsePlanetPreset(text.substr(9u));
                if (parsed.has_value())
                {
                    options.Preset = *parsed;
                }
                else
                {
                    valid = false;
                }
            }
            else if (text.starts_with("--shaping="))
            {
                const std::optional<planet_loom::ShapingPreset> parsed =
                    planet_loom::ParseShapingPreset(text.substr(10u));
                if (parsed.has_value())
                {
                    options.Shaping = *parsed;
                }
                else
                {
                    valid = false;
                }
            }
            else if (text.starts_with("--seed="))
            {
                valid = ParseUint64(text.substr(7u), options.Seed);
            }
            else if (text.starts_with("--plates="))
            {
                valid = ParseUint(text.substr(9u), options.PlateCount) &&
                        options.PlateCount >= 4u &&
                        options.PlateCount <= 32u;
            }
            else if (text.starts_with("--order="))
            {
                valid = ParseUint(text.substr(8u), options.Order) &&
                        options.Order >= 4u &&
                        options.Order <= 9u;
            }
            else if (text.starts_with("--steps="))
            {
                valid = ParseUint(text.substr(8u), options.Steps) && options.Steps > 0u;
            }
            else if (text.starts_with("--dt="))
            {
                valid = ParseFloat(text.substr(5u), options.Dt) && options.Dt > 0.0f;
            }
            else if (text.starts_with("--width="))
            {
                valid = ParseUint(text.substr(8u), options.Width) && options.Width > 0u;
            }
            else if (text.starts_with("--height="))
            {
                valid = ParseUint(text.substr(9u), options.Height) && options.Height > 0u;
            }
            else if (text.starts_with("--mode="))
            {
                valid = ParseMode(text.substr(7u), options.Mode);
            }
            else if (text.starts_with("--projection="))
            {
                valid = ParseProjection(text.substr(13u), options.Projection);
            }
            else if (text == "--telemetry")
            {
                options.Telemetry = true;
            }
            else if (text == "--no-telemetry")
            {
                options.Telemetry = false;
            }
            else if (text == "--words")
            {
                options.Words = true;
            }
            else if (text == "--verify")
            {
                options.Verify = true;
            }
            else if (text == "--self-verify")
            {
                options.SelfVerify = true;
            }
            else if (text == "--timestamps")
            {
                options.Timestamps = true;
            }
            else if (text == "--explain")
            {
                options.Explain = true;
            }
            else if (text.starts_with("--capture-dir="))
            {
                options.CaptureDir = text.substr(14u);
            }
            else if (text == "--plumes=default")
            {
                options.PlumesEnabled = true;
                options.PlumesExplicit = true;
            }
            else if (text == "--plumes=none")
            {
                options.PlumesEnabled = false;
                options.PlumesExplicit = true;
            }
            else if (text == "--rift=on")
            {
                options.Rift = true;
            }
            else if (text == "--rift=off")
            {
                options.Rift = false;
            }
            else if (text == "--erosion=on")
            {
                options.Erosion = true;
            }
            else if (text == "--erosion=off")
            {
                options.Erosion = false;
            }
            else if (text == "--live")
            {
                options.Live = true;
            }
            else if (text == "--paused")
            {
                options.Paused = true;
            }
            else if (text.starts_with("--tool-port="))
            {
                valid = ParseInt(text.substr(12u), options.ToolPort) &&
                        options.ToolPort >= 0 &&
                        options.ToolPort <= 65535;
            }
            else if (text.starts_with("--frames="))
            {
                valid = ParseInt(text.substr(9u), options.FrameLimit) && options.FrameLimit > 0;
            }
            else if (text == "--validation=full")
            {
                options.ValidationFull = true;
            }
            else if (text == "--validation=none")
            {
                options.ValidationFull = false;
            }
            else
            {
                valid = false;
            }

            if (!valid)
            {
                std::println(stderr, "PlanetGenLoom: '{}' is not an option, or its value is invalid", text);
                return std::nullopt;
            }
        }

        if ((options.Live || options.Preset == planet_loom::PlanetPreset::Atoll) &&
            !options.PlumesExplicit &&
            !options.Verify)
        {
            options.PlumesEnabled = true;
        }

        if (!options.Shaping.has_value())
        {
            if (options.Verify)
            {
                options.Shaping = planet_loom::ShapingPreset::Legacy;
            }
            else if (options.Preset == planet_loom::PlanetPreset::Atoll)
            {
                options.Shaping = planet_loom::ShapingPreset::Atoll;
            }
            else if (options.Preset == planet_loom::PlanetPreset::Earth)
            {
                options.Shaping = planet_loom::ShapingPreset::Earth;
            }
            else
            {
                options.Shaping = planet_loom::ShapingPreset::Legacy;
            }
        }

        if (options.Preset == planet_loom::PlanetPreset::Reference && options.Reference.empty())
        {
            std::println(stderr,
                         "usage: PlanetGenLoom (--reference=<dir> | --preset=earth|atoll) [--shaping=legacy|earth|atoll] "
                         "[--seed=N] [--plates=4..32] [--rift=on|off] [--erosion=on|off] [--order=4..9] [--steps=N] [--dt=F] "
                         "[--width=N] [--height=N] [--mode=0..8|name] [--plumes=default|none] [--words] "
                         "[--verify] [--self-verify] [--timestamps] [--explain] [--capture-dir=<dir>] "
                         "[--live] [--paused] [--frames=N] [--tool-port=0..65535] [--validation=full|none]");
            return std::nullopt;
        }

        return options;
    }

    // the parity checks compare against runs with no rifting
    [[nodiscard]] bool RiftWanted(const LoomOptions& options) noexcept
    {
        return options.Rift && !options.Verify && !options.SelfVerify;
    }

    [[nodiscard]] bool ErosionWanted(const LoomOptions& options) noexcept
    {
        return options.Erosion && !options.Verify && !options.SelfVerify;
    }

    [[nodiscard]] planet_loom::PlanetLiveOptions ToLiveOptions(const LoomOptions& options)
    {
        bool validation = true;
#ifdef NDEBUG
        validation = false;
#endif
        if (options.ValidationFull.has_value())
        {
            validation = *options.ValidationFull;
        }

        bool telemetry = options.ToolPort >= 0;
        if (options.Telemetry.has_value())
        {
            telemetry = *options.Telemetry;
        }

        return planet_loom::PlanetLiveOptions
        {
            .CaptureDir = options.CaptureDir,
            .Preset = options.Preset,
            .Shaping = options.Shaping.value_or(planet_loom::ShapingPreset::Legacy),
            .Seed = options.Seed,
            .Order = options.Order,
            .Dt = options.Dt,
            .Width = options.Width,
            .Height = options.Height,
            .Mode = options.Mode,
            .Projection = options.Projection,
            .Words = options.Words,
            .ValidationFull = validation,
            .Timestamps = options.Timestamps || options.Live,
            .Telemetry = telemetry,
            .PlumesEnabled = options.PlumesEnabled,
            .StartPaused = options.Paused,
            .ToolPort = options.ToolPort,
            .FrameLimit = options.FrameLimit,
            .Explain = options.Explain,
            .RiftEnabled = RiftWanted(options),
            .ErosionEnabled = ErosionWanted(options),
        };
    }

    [[nodiscard]] std::string FormatDiagnosticValue(const loom::DiagnosticValue& value)
    {
        switch (value.Kind)
        {
        case loom::DiagnosticValueKind::None:
            return "-";
        case loom::DiagnosticValueKind::Text:
            return std::format("{}:{}", value.FieldName, value.Text);
        case loom::DiagnosticValueKind::Integer:
            return std::format("{}:{}", value.FieldName, value.Integer);
        case loom::DiagnosticValueKind::Real:
            return std::format("{}:{}", value.FieldName, value.Real);
        }

        return "-";
    }

    void PrintDiagnostics(const loom::CollectingSink& sink)
    {
        for (const loom::Diagnostic& diagnostic : sink.Diagnostics())
        {
            std::println(stderr,
                         "{}({}): {} {}: {} [pass='{}', resource='{}', binding='{}', expected={}, actual={}]",
                         diagnostic.Where.file_name(),
                         diagnostic.Where.line(),
                         loom::ToString(diagnostic.Code),
                         loom::ToString(diagnostic.Severity),
                         loom::SummaryOf(diagnostic.Code),
                         diagnostic.Pass.Name,
                         diagnostic.Resource.Name,
                         diagnostic.Binding,
                         FormatDiagnosticValue(diagnostic.Expected),
                         FormatDiagnosticValue(diagnostic.Actual));
        }
    }

    [[nodiscard]] std::vector<std::byte> ReadBack(ResourceContext& resources,
                                                  const loom::Graph& graph,
                                                  GraphicsResource source,
                                                  size_t bytes)
    {
        if (!source || bytes == 0u)
        {
            return {};
        }

        BufferCreateInfo readbackInfo{};
        readbackInfo.Domain = ResourceDomain::GPUToCPU;
        readbackInfo.Usage = BufferUsageBits::TransferDestination;
        readbackInfo.Size = bytes;
        readbackInfo.Name = "PlanetGenLoom.Readback";
        rhi::ResourceFuture<GraphicsResource> readbackReply = resources.CreateBuffer(readbackInfo, {});

        if (resources.Wait(readbackReply, k_TimeoutNs) != rhi::FutureState::Complete)
        {
            return {};
        }

        const GraphicsResource readback = readbackReply.Get();
        const rhi::GpuWait last = graph.LastSubmit();
        const rhi::TransferWait afterGraph{ last.Semaphore, last.Value };
        rhi::OperationFuture copyReply = last.Submitted
            ? resources.CopyBufferContents(source, readback, std::span{ &afterGraph, 1u })
            : resources.CopyBufferContents(source, readback, {});
        rhi::ResourceFuture<void*> mapReply = resources.MapBuffer(readback, bytes, 0u);
        std::vector<std::byte> result;

        if (resources.Wait(copyReply, k_TimeoutNs) == rhi::FutureState::Complete &&
            resources.Wait(mapReply, k_TimeoutNs) == rhi::FutureState::Complete)
        {
            const auto* mapped = static_cast<const std::byte*>(mapReply.Get());
            result.assign(mapped, mapped + bytes);
        }

        rhi::OperationFuture unmapReply = resources.UnmapBuffer(readback, bytes, 0u);
        rhi::OperationFuture destroyReply = resources.DestroyResource(readback);
        [[maybe_unused]] const rhi::FutureState unmapped = resources.Wait(unmapReply, k_TimeoutNs);
        [[maybe_unused]] const rhi::FutureState destroyed = resources.Wait(destroyReply, k_TimeoutNs);
        return result;
    }

    [[nodiscard]] bool RunLoomSimulation(std::span<char* const> arguments,
                                         const LoomOptions& options,
                                         const planet::HealpixInitialState& initial,
                                         VerifySnapshot* out_snapshot)
    {
        const bool useBytes = !options.Words;
        std::vector<bootstrap::Need> appNeeds;
        appNeeds.append_range(bootstrap::ShaderObjectNeeds());

        if (options.Timestamps)
        {
            appNeeds.append_range(bootstrap::QueryPoolNeeds());
        }

        const bootstrap::DeviceConfig deviceConfig
        {
            .ApiVersion = rhi::ApiVersion::Latest,
            .Validation = options.Verify ? rhi::ValidationLayers::Full : rhi::ValidationLayers::None,
            .ApplicationName = "PlanetGenLoom",
        };

        HeadlessBoot boot;

        if (!StartHeadlessBoot(boot, arguments, k_Bundles, appNeeds, deviceConfig))
        {
            return false;
        }

        ResourceContext& resources = boot.Gpu.Resources();
        const rhi::Device& device = *boot.Device.Rhi().GetDevice();
        const bootstrap::DeviceFilter filter = boot.Device.Filter();

        const planet_loom::PlanetGraphConfig graphConfig
        {
            .HealpixOrder = options.Order,
            .PlateCount = initial.PlateCount,
            .MapWidth = options.Width,
            .MapHeight = options.Height,
            .UseByteCrustFlags = useBytes,
            // only the last step's map is read (the capture), so the map renders on that step alone
            .RenderOnTrigger = true,
            .InteractiveControl = false,
            .LiveMode = false,
            .EnableCapturePass = false,
        };

        loom::GraphBuilder builder{ "PlanetGen.Graph", "PlanetLab" };
        planet_loom::ZeroCountersRecorder zeroRecorder;
        const planet_loom::PlanetGraphCallbacks graphCallbacks
        {
            .ZeroCounters = &zeroRecorder,
            .CaptureCallback = {},
            .UiCallback = {},
        };
        planet_loom::RegisterPlanetGraph(builder, graphConfig, graphCallbacks);

        loom::CollectingSink sink;
        const loom::CompileInputs compileInputs
        {
            .Bundle = boot.Content.Bundles().front().Bundle,
            .Supports = filter.Supports,
            .SupportsUser = filter.User,
            .ShaderLimits = loom::ShaderLimitsOf(device),
            .Limits = loom::LayoutLimitsOf(device),
        };

        std::optional<loom::CompiledGraph> compiled = loom::Compile(builder, compileInputs, sink);

        if (!compiled.has_value())
        {
            std::println(stderr, "PlanetGenLoom: Compile failed");
            PrintDiagnostics(sink);
            return false;
        }

        const loom::RealizeInputs realizeInputs
        {
            .Resources = resources,
            .Device = device,
            .FrameSlots = 1,
            .Timestamps = options.Timestamps ? loom::PassTimestamps::On : loom::PassTimestamps::Off,
            .Aliasing = loom::MemoryAliasing::Off,
            .Sink = sink,
            .FrameTargets = {},
            .TargetSettleSeconds = 0.0,
        };

        loom::RealizeResult realized = loom::Graph::Realize(std::move(*compiled), realizeInputs);

        if (!realized.has_value())
        {
            std::println(stderr, "PlanetGenLoom: Realize failed: {}", loom::ToString(realized.error()));
            PrintDiagnostics(sink);
            return false;
        }

        std::unique_ptr<loom::Graph> graph = std::move(*realized);
        planet_loom::PlanetGraphImports imports;
        const planet_loom::PlumeSystemState plumeState = options.PlumesEnabled
            ? planet_loom::PresetPlumes(options.Preset, initial.InitialTime, options.Seed)
            : planet_loom::EmptyPlumeSystem();
        planet_loom::ShapingState shapingState = planet_loom::ShapingStateForPreset(
            options.Shaping.value_or(planet_loom::ShapingPreset::Legacy),
            static_cast<uint32_t>(options.Seed));
        shapingState.ErosionEnabled = ErosionWanted(options);

        imports.RiftEnabled = RiftWanted(options);

        const planet_loom::RenderMapView mapView
        {
            .Mode = options.Mode,
            .Projection = planet_loom::ShaderIndexToProjection(static_cast<int32_t>(options.Projection)),
        };

        if (!imports.SupplyAll(*graph, resources, initial, options.Dt, options.Order,
                               options.Width, options.Height, mapView, useBytes, plumeState, shapingState))
        {
            graph.reset();
            imports.Destroy(resources);
            return false;
        }

        std::println("PlanetGenLoom: {} points (HEALPix order {}), {} plates, {} steps (dt = {} My), crust flags in {}, plumes={}",
                     initial.PointCount, options.Order, initial.PlateCount, options.Steps, options.Dt,
                     useBytes ? "bytes" : "words", options.PlumesEnabled ? "default" : "none");

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::nanoseconds(k_TimeoutNs);

        // a Tick before every row and pipeline exists skips the passes that need them, and that is not a whole step
        while (!graph->Ready() && std::chrono::steady_clock::now() < deadline)
        {
            const ConsumerTimelineValue pending{ graph->Timeline(), graph->PendingValue() };
            resources.Tick(std::span{ &pending, 1u });
            std::this_thread::yield();
        }

        if (!graph->Ready())
        {
            std::println(stderr, "PlanetGenLoom: the graph resources did not come to exist before the first step");
            graph.reset();
            imports.Destroy(resources);
            return false;
        }

        for (uint32_t step = 0u; step < options.Steps; ++step)
        {
            if (!graph->WaitForFrameSlot(std::chrono::nanoseconds(k_TimeoutNs)))
            {
                std::println(stderr, "PlanetGenLoom: timed out waiting for frame slot before step {}", step);
                graph.reset();
                imports.Destroy(resources);
                return false;
            }

            const float stepTime = initial.InitialTime + static_cast<float>(step + 1u) * options.Dt;

            if (step + 1u == options.Steps)
            {
                graph->Trigger(planet_loom::k_RenderMapPass);
            }

            if (!imports.UpdateUniforms(resources, stepTime, options.Width, options.Height, mapView, plumeState, shapingState))
            {
                graph.reset();
                imports.Destroy(resources);
                return false;
            }

            bool submitted = false;

            while (!submitted && std::chrono::steady_clock::now() < deadline)
            {
                const ConsumerTimelineValue pending{ graph->Timeline(), graph->PendingValue() };
                resources.Tick(std::span{ &pending, 1u });

                const loom::TickResult result = graph->Tick(std::chrono::duration<double>{ 1.0 / 60.0 });

                switch (result.Status)
                {
                case loom::TickStatus::Submitted:
                    submitted = true;
                    break;
                case loom::TickStatus::NoFrameSlot:
                    if (!graph->WaitForFrameSlot(std::chrono::nanoseconds(k_TimeoutNs)))
                    {
                        std::println(stderr, "PlanetGenLoom: frame slot wait timed out");
                        graph.reset();
                        imports.Destroy(resources);
                        return false;
                    }
                    break;
                case loom::TickStatus::NothingToRun:
                    std::this_thread::yield();
                    break;
                case loom::TickStatus::Invalid:
                case loom::TickStatus::TargetNotReady:
                case loom::TickStatus::Failed:
                    std::println(stderr, "PlanetGenLoom: Tick failed:\n{}", graph->ExplainLastTick());
                    PrintDiagnostics(sink);
                    graph.reset();
                    imports.Destroy(resources);
                    return false;
                }
            }

            if (!submitted)
            {
                std::println(stderr, "PlanetGenLoom: timed out waiting for step {} submission\n{}",
                             step, graph->ExplainLastTick());
                graph.reset();
                imports.Destroy(resources);
                return false;
            }
        }

        if (!graph->WaitForFrameSlot(std::chrono::nanoseconds(k_TimeoutNs)))
        {
            std::println(stderr, "PlanetGenLoom: timed out waiting for final step");
            graph.reset();
            imports.Destroy(resources);
            return false;
        }

        if (options.Explain)
        {
            std::println("=== ExplainLastTick ===\n{}", graph->ExplainLastTick());
        }

        if (options.Timestamps)
        {
            const std::optional<rhi::GpuTimingBlock> timings = graph->LatestTimings();
            std::println("=== LatestTimings ===");

            if (!timings.has_value())
            {
                std::println("  (no completed GPU timing block yet; run with --steps >= 2)");
            }
            else
            {
                double totalUs = 0.0;

                for (const rhi::GpuTiming& timing : timings->Timings)
                {
                    const double us = timing.Nanoseconds * 1.0e-3;
                    totalUs += us;
                    std::println("  {:<42} {:9.2f} us", planet_loom::PassNameForHash(timing.Hash), us);
                }

                std::println("  {:<42} {:9.2f} us (top-level {:.2f} us, dropped {})",
                             "TOTAL", totalUs, timings->TopLevelNanoseconds * 1.0e-3, timings->DroppedScopes);
            }
        }

        bool ok = true;

        if (out_snapshot != nullptr)
        {
            const size_t ownersBytes = static_cast<size_t>(initial.PointCount) * sizeof(math::Int4);
            const size_t flagsBytes = initial.CrustFlags.size();
            const size_t stateBytes = initial.CrustState.size() * sizeof(math::Float4);
            const size_t rotBytes = initial.Rotations.size() * sizeof(math::Float4);
            const size_t poleBytes = initial.PoleRates.size() * sizeof(math::Float4);

            out_snapshot->Owners = ReadBack(resources, *graph, graph->BufferOf(planet_loom::k_Owners), ownersBytes);
            out_snapshot->CrustFlags = ReadBack(resources, *graph, imports.CrustFlags, flagsBytes);
            out_snapshot->CrustState = ReadBack(resources, *graph, imports.CrustState, stateBytes);
            out_snapshot->Rotations = ReadBack(resources, *graph, imports.Rotations, rotBytes);
            out_snapshot->PoleRates = ReadBack(resources, *graph, imports.PoleRates, poleBytes);

            if (out_snapshot->Owners.size() != ownersBytes ||
                out_snapshot->CrustFlags.size() != flagsBytes ||
                out_snapshot->CrustState.size() != stateBytes ||
                out_snapshot->Rotations.size() != rotBytes ||
                out_snapshot->PoleRates.size() != poleBytes)
            {
                std::println(stderr, "PlanetGenLoom: Loom readback failed");
                ok = false;
            }
        }

        if (!options.CaptureDir.empty())
        {
            const GraphicsResource mapImage = graph->ImageOf(planet_loom::k_MapOutput);
            if (mapImage)
            {
                capture::FrameCapture capture{ resources, 1u, options.CaptureDir, 1u };
                rhi::CommandPool pool{
                    device.Handle(),
                    rhi::CommandPool::Type::Compute,
                    device.GetQueueFamilyIndices().Graphics
                };
                if (pool.AllocateCommandBuffers(1u).IsSuccess())
                {
                    rhi::CommandBuffer cmd{ pool.GetCommandBuffer(0u) };
                    if (cmd.Begin().IsSuccess())
                    {
                        const capture::CaptureSource source
                        {
                            .Image = rhi::ImageHandle{ mapImage.VkHandle },
                            .Width = options.Width,
                            .Height = options.Height,
                            .Format = ImageFormat{ ImageComponentFormats::RGBA8, ImageDataType::UNorm },
                            .Encoding = capture::CaptureEncoding::sRGB,
                            .CurrentLayout = rhi::ImageLayout::General,
                            .SrcStages = rhi::PipelineStage::ComputeShader,
                            .SrcAccess = rhi::Access::ShaderStorageWrite,
                            .LinearScale = 1.0f,
                            .FinalLayout = rhi::ImageLayout::General,
                            .DstStages = rhi::PipelineStage::AllCommands,
                            .DstAccess = rhi::Access::MemoryRead | rhi::Access::MemoryWrite,
                            .Aspect = rhi::ImageAspect::Color,
                            .OffsetX = 0,
                            .OffsetY = 0,
                            .Downsample = 1u,
                        };
                        const std::string capName = std::format("loom_order{}_step{}_mode{}",
                                                                options.Order, options.Steps, options.Mode);
                        static_cast<void>(capture.RecordTrackedCopy(cmd, 0u, source, capName, capture::CaptureOutput::Png));
                        static_cast<void>(cmd.End());
                        const rhi::CommandBufferHandle handle = cmd.Handle();
                        rhi::Fence fence{ device.Handle() };
                        const rhi::Result submitted = rhi::QueueSubmit(
                            device.GetGeneralQueue(),
                            rhi::SubmitInfo{ {}, std::span{ &handle, 1u }, {} },
                            fence.Handle());
                        if (submitted.IsSuccess())
                        {
                            static_cast<void>(fence.Wait());
                        }
                    }
                }
                capture.Flush();
                const std::vector<capture::CaptureResult> capResults = capture.TakeResults();
                for (const capture::CaptureResult& cap : capResults)
                {
                    std::println("Captured PNG: {} ({}x{}, ok={})", cap.Path.string(), cap.Width, cap.Height, cap.Succeeded);
                }
            }
        }

        if (sink.ErrorCount() != 0)
        {
            PrintDiagnostics(sink);
            ok = false;
        }

        graph.reset();
        imports.Destroy(resources);
        return ok;
    }

    [[nodiscard]] bool RunLegacySimulation(std::span<char* const> arguments,
                                           const LoomOptions& options,
                                           const planet::ReferenceSet& reference,
                                           VerifySnapshot& out_snapshot)
    {
        const planet::PlanetSessionConfig sessionConfig
        {
            .ApplicationName = "PlanetGenLoom.LegacyVerify",
            .Validation = rhi::ValidationLayers::Full,
            .ConfigMode = planet::ConfigFileMode::Ignore,
            .ExtraNeeds = {},
        };

        const std::unique_ptr<planet::PlanetSession> session =
            planet::PlanetSession::Start(arguments, sessionConfig);

        if (session == nullptr)
        {
            return false;
        }

        const planet::CrustFlagChoice choice = options.Words
            ? planet::CrustFlagChoice::Words
            : planet::CrustFlagChoice::Bytes;

        const planet::PlanetGpuResult<std::unique_ptr<planet::PlanetShaders>> shaders =
            session->CreateShaders(choice, planet::k_LiveStepEntryPoints);

        if (!shaders.has_value())
        {
            return false;
        }

        const uint32_t pointCount = static_cast<uint32_t>(planet::HealpixPixelCount(options.Order));
        const planet::StepSize size{ pointCount, static_cast<uint32_t>(reference.PlateCount()) };
        planet::GpuBuffers buffers(session->Resources());

        const std::array<planet::UniformValue, 1> movePlatesInit
        {
            planet::UniformFloat("Time", reference.Time()),
        };

        if (!planet::AddHealpixStepBuffers(buffers, **shaders, reference, options.Order,
                                           (*shaders)->CrustFlags(), false, options.Dt, true) ||
            buffers.AddUniformFrom(**shaders, "MovePlatesAndClearCountersCS", "MovePlatesConstants", movePlatesInit) != planet::PlanetGpuError::Success)
        {
            return false;
        }

        const planet::PlanetGpuResult<std::unique_ptr<planet::PlanetStep>> stepRunner =
            planet::PlanetStep::Create(**shaders, buffers, size);

        if (!stepRunner.has_value())
        {
            return false;
        }

        for (uint32_t step = 0u; step < options.Steps; ++step)
        {
            const float time = reference.Time() + static_cast<float>(step + 1u) * options.Dt;
            const std::array<planet::UniformValue, 1> timeValues
            {
                planet::UniformFloat("Time", time),
            };

            if (buffers.UpdateUniformFrom(**shaders, "FillGapsCS", "Pass3Constants", timeValues) != planet::PlanetGpuError::Success ||
                buffers.UpdateUniformFrom(**shaders, "FrontDistanceAndUpliftCS", "UpliftConstants", timeValues) != planet::PlanetGpuError::Success ||
                buffers.UpdateUniformFrom(**shaders, "MovePlatesAndClearCountersCS", "MovePlatesConstants", timeValues) != planet::PlanetGpuError::Success)
            {
                return false;
            }

            rhi::CommandBuffer commandBuffer = session->BeginCommands();
            (*stepRunner)->RecordLiveStep(commandBuffer);

            if (step + 1u == options.Steps)
            {
                buffers.RecordReadbackCopies(commandBuffer);
            }

            if (!session->SubmitAndWait(commandBuffer))
            {
                return false;
            }
        }

        const std::string_view flagBufferName = planet::CrustFlagBufferNames((*shaders)->CrustFlags());
        const auto owners = buffers.Read("Owners");
        const auto flags = buffers.Read(flagBufferName);
        const auto state = buffers.Read("CrustState");
        const auto rots = buffers.Read("Rotations");
        const auto poles = buffers.Read("PoleRatesIn");

        if (!owners.has_value() || !flags.has_value() || !state.has_value() ||
            !rots.has_value() || !poles.has_value())
        {
            std::println(stderr, "PlanetGenLoom: legacy readback failed");
            return false;
        }

        out_snapshot.Owners = *owners;
        out_snapshot.CrustFlags = *flags;
        out_snapshot.CrustState = *state;
        out_snapshot.Rotations = *rots;
        out_snapshot.PoleRates = *poles;
        return true;
    }

    struct DiffReport
    {
        size_t ByteDiffs{ 0u };
        size_t FloatDiffs{ 0u };
        float MaxAbsFloatDiff{ 0.0f };
    };

    [[nodiscard]] size_t CountByteDiffs(std::span<const std::byte> lhs, std::span<const std::byte> rhs) noexcept
    {
        if (lhs.size() != rhs.size())
        {
            return std::max(lhs.size(), rhs.size());
        }

        size_t diffs = 0u;

        for (size_t i = 0u; i < lhs.size(); ++i)
        {
            if (lhs[i] != rhs[i])
            {
                ++diffs;
            }
        }

        return diffs;
    }

    [[nodiscard]] DiffReport CompareFloat4Buffer(std::span<const std::byte> lhs,
                                                   std::span<const std::byte> rhs,
                                                   bool w_is_int,
                                                   float tol = 1.0e-5f) noexcept
    {
        DiffReport report;
        report.ByteDiffs = CountByteDiffs(lhs, rhs);

        if (lhs.size() != rhs.size() || lhs.size() % sizeof(math::Float4) != 0u)
        {
            report.FloatDiffs = std::max(lhs.size(), rhs.size());
            return report;
        }

        const size_t count = lhs.size() / sizeof(math::Float4);

        for (size_t i = 0u; i < count; ++i)
        {
            math::Float4 a{};
            math::Float4 b{};
            std::memcpy(&a, lhs.data() + i * sizeof(math::Float4), sizeof(math::Float4));
            std::memcpy(&b, rhs.data() + i * sizeof(math::Float4), sizeof(math::Float4));

            const float dx = std::fabs(a.x - b.x);
            const float dy = std::fabs(a.y - b.y);
            const float dz = std::fabs(a.z - b.z);
            report.MaxAbsFloatDiff = std::max({ report.MaxAbsFloatDiff, dx, dy, dz });

            bool differs = dx > tol || dy > tol || dz > tol;

            if (w_is_int)
            {
                if (std::memcmp(&a.w, &b.w, sizeof(float)) != 0)
                {
                    differs = true;
                }
            }
            else
            {
                const float dw = std::fabs(a.w - b.w);
                report.MaxAbsFloatDiff = std::max(report.MaxAbsFloatDiff, dw);

                if (dw > tol)
                {
                    differs = true;
                }
            }

            if (differs)
            {
                if (report.FloatDiffs < 8u)
                {
                    std::println(stderr,
                                 "    diff[{}]: loom=({:.4f}, {:.4f}, {:.4f}, w=0x{:08x}) legacy=({:.4f}, {:.4f}, {:.4f}, w=0x{:08x})",
                                 i,
                                 a.x, a.y, a.z, std::bit_cast<uint32_t>(a.w),
                                 b.x, b.y, b.z, std::bit_cast<uint32_t>(b.w));
                }
                ++report.FloatDiffs;
            }
        }

        return report;
    }
}

int main(int argc, char* argv[])
{
    const std::span<char* const> arguments{ argv, static_cast<size_t>(argc) };
    const std::optional<LoomOptions> options = ParseOptions(arguments);

    if (!options.has_value())
    {
        return 1;
    }

    const planet::CrustFlagStorage storage = options->Words
        ? planet::CrustFlagStorage::Words
        : planet::CrustFlagStorage::Bytes;

    std::optional<planet::ReferenceSet> reference;
    std::optional<planet::HealpixInitialState> initial;

    if (options->Preset == planet_loom::PlanetPreset::Reference)
    {
        reference = planet::ReferenceSet::Open(options->Reference);
        if (!reference.has_value())
        {
            return 1;
        }

        initial = planet::ResampleHealpixState(*reference, options->Order, storage);
        if (!initial.has_value())
        {
            std::println(stderr, "PlanetGenLoom: failed to resample reference state at order {}", options->Order);
            return 1;
        }
    }
    else
    {
        const uint32_t plateCount = options->PlateCount != 0u
            ? options->PlateCount
            : (options->Preset == planet_loom::PlanetPreset::Atoll ? 28u : 20u);

        initial = planet_loom::GenerateProceduralHealpixState(
            options->Preset, options->Order, plateCount, options->Seed, storage);
        if (!initial.has_value())
        {
            std::println(stderr, "PlanetGenLoom: failed to generate procedural state for preset '{}'",
                         planet_loom::PlanetPresetName(options->Preset));
            return 1;
        }
    }

    if (RiftWanted(*options))
    {
        planet_loom::AddRiftSlots(*initial, planet_loom::k_DefaultRiftSlots, storage);
    }

    if (options->Live)
    {
        return planet_loom::RunLivePlanetApp(arguments, ToLiveOptions(*options), *initial);
    }

    if (options->ToolPort >= 0)
    {
        return planet_loom::RunHeadlessToolApp(arguments, ToLiveOptions(*options), *initial);
    }

    const bool needSnapshot = options->Verify || options->SelfVerify;
    VerifySnapshot loomSnapshot;

    if (!RunLoomSimulation(arguments, *options, *initial, needSnapshot ? &loomSnapshot : nullptr))
    {
        return 1;
    }

    if (!needSnapshot)
    {
        return 0;
    }

    VerifySnapshot legacySnapshot;

    if (options->SelfVerify)
    {
        if (!RunLoomSimulation(arguments, *options, *initial, &legacySnapshot))
        {
            return 1;
        }
    }
    else if (!reference.has_value())
    {
        std::println(stderr, "PlanetGenLoom: --verify against Legacy requires --reference (use --self-verify for procedural presets)");
        return 1;
    }
    else if (!RunLegacySimulation(arguments, *options, *reference, legacySnapshot))
    {
        return 1;
    }

    const size_t ownersDiffs = CountByteDiffs(loomSnapshot.Owners, legacySnapshot.Owners);
    const size_t flagsDiffs = CountByteDiffs(loomSnapshot.CrustFlags, legacySnapshot.CrustFlags);
    const DiffReport stateDiffs = CompareFloat4Buffer(loomSnapshot.CrustState, legacySnapshot.CrustState, true);
    const DiffReport rotDiffs = CompareFloat4Buffer(loomSnapshot.Rotations, legacySnapshot.Rotations, false);
    const DiffReport poleDiffs = CompareFloat4Buffer(loomSnapshot.PoleRates, legacySnapshot.PoleRates, false);

    std::println("=== Parity Check (Loom vs Legacy PlanetStep, {} steps) ===", options->Steps);
    std::println("  Owners     : {} byte diffs (of {} bytes)", ownersDiffs, loomSnapshot.Owners.size());
    std::println("  CrustFlags : {} byte diffs (of {} bytes)", flagsDiffs, loomSnapshot.CrustFlags.size());
    std::println("  CrustState : {} byte diffs, {} float4 diffs (>1e-5, max={:.3g})",
                 stateDiffs.ByteDiffs, stateDiffs.FloatDiffs, stateDiffs.MaxAbsFloatDiff);
    std::println("  Rotations  : {} byte diffs, {} float4 diffs (>1e-5, max={:.3g})",
                 rotDiffs.ByteDiffs, rotDiffs.FloatDiffs, rotDiffs.MaxAbsFloatDiff);
    std::println("  PoleRates  : {} byte diffs, {} float4 diffs (>1e-5, max={:.3g})",
                 poleDiffs.ByteDiffs, poleDiffs.FloatDiffs, poleDiffs.MaxAbsFloatDiff);

    const bool exactMatch = ownersDiffs == 0u &&
                            flagsDiffs == 0u &&
                            stateDiffs.ByteDiffs == 0u &&
                            rotDiffs.ByteDiffs == 0u &&
                            poleDiffs.ByteDiffs == 0u;

    const bool floatMatch = ownersDiffs == 0u &&
                            flagsDiffs == 0u &&
                            stateDiffs.FloatDiffs == 0u &&
                            rotDiffs.FloatDiffs == 0u &&
                            poleDiffs.FloatDiffs == 0u;

    if (exactMatch)
    {
        std::println("PASS: bit-exact parity across all 5 buffers.");
        return 0;
    }

    if (floatMatch)
    {
        std::println(stderr, "FAIL: float-tolerant match only (non-zero byte diffs).");
        return 1;
    }

    std::println(stderr, "FAIL: parity mismatch detected.");
    return 1;
}
