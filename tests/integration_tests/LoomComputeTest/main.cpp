#include "Compile.hpp"
#include "CompiledGraph.hpp"
#include "Declarations.hpp"
#include "Device.hpp"
#include "DeviceConfig.hpp"
#include "FramePlan.hpp"
#include "GpuTimers.hpp"
#include "DeviceStage.hpp"
#include "GraphBuilder.hpp"
#include "HeadlessBoot.hpp"
#include "LoadedBundle.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "LoomGraph.hpp"
#include "LoomKeys.hpp"
#include "PassBuilders.hpp"
#include "ResourceContext.hpp"
#include "ResourceFuture.hpp"
#include "ResourceTypes.hpp"
#include "RhiResourceFlags.hpp"
#include "RhiFlags.hpp"
#include "RhiSystem.hpp"
#include "ShaderBundle.hpp"
#include "ShaderLibrary.host.hpp"
#include "SpineNeeds.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
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

// Loom's first GPU proof (spec step 5). A two-pass graph, compiled from the device-free bundle, realized on a headless
// device, and ticked three times on two frame slots:
//  - HashFillCS writes Output, an imported buffer. The test creates it device-local, with a sentinel upload through the
//    transfer system, and supplies it: the first submit must wait for the upload (Completion(), CC-R2).
//  - HashAgainCS reads Output and writes Output2, a graph-declared buffer. That is a read after a write at the level
//    boundary. From the second frame on, HashFillCS's new write of Output must also wait for the last frame's read and
//    write: the frame-start batch.
// Both outputs are then copied out through ResourceContext, behind the graph's timeline, and every word is compared
// with the CPU, for both values of the HASH_TWICE axis. Synchronization validation is on (ValidationLayers::Full), so
// a missing barrier or wait prints a SYNC-HAZARD message, and the run counts as failed.

namespace
{
    namespace host = loom_compute_test_shaders;
    using FillParams = host::LoomHashFill::HashFillCS::Params<host::CookProfile::Spirv>;
    using AgainParams = host::LoomHashFill::HashAgainCS::Params<host::CookProfile::Spirv>;

    // not a multiple of the workgroup size, so the bounds check in the shader is exercised too
    constexpr uint32_t k_ElementCount = 4099u;
    constexpr size_t k_Bytes = sizeof(uint32_t) * k_ElementCount;
    constexpr uint32_t k_Seed = 0x9E3779B9u;
    constexpr uint32_t k_Sentinel = 0xDEADBEEFu;
    constexpr int32_t k_FrameSlots = 2;
    constexpr int32_t k_Ticks = 3;
    constexpr uint64_t k_TimeoutNs = 5'000'000'000ull;

    constexpr loom::ResourceKey<loom::Buffer> k_Output{ "LoomCompute.Output" };
    constexpr loom::ResourceKey<loom::Buffer> k_Output2{ "LoomCompute.Output2" };
    constexpr loom::PassKey k_Fill{ "LoomCompute.Fill" };
    constexpr loom::PassKey k_Again{ "LoomCompute.Again" };
    constexpr loom::ParamsKey<FillParams> k_FillParams{ "LoomCompute.FillParams" };
    constexpr loom::ParamsKey<AgainParams> k_AgainParams{ "LoomCompute.AgainParams" };

    constexpr bootstrap::BundleRequest k_Bundles[]
    {
        bootstrap::BundleRequest
        {
            .FileName = "ShaderLibrary.ldmanifest",
            .BuildTreePath = LOOM_COMPUTE_TEST_MANIFEST,
            .Profile = rhi::k_SpirvProfileName,
        },
    };

    // PCG hash, from Jarzynski and Olano, "Hash Functions for GPU Rendering" (JCGT, 2020)
    uint32_t PcgHash(uint32_t value) noexcept
    {
        const uint32_t state = value * 747796405u + 2891336453u;
        const uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
        return (word >> 22u) ^ word;
    }

    uint32_t ExpectedOutput(uint32_t index, bool hash_twice) noexcept
    {
        const uint32_t hashed = PcgHash(index ^ k_Seed);
        return hash_twice ? PcgHash(hashed) : hashed;
    }

    uint32_t ExpectedOutput2(uint32_t index, bool hash_twice) noexcept
    {
        return PcgHash(ExpectedOutput(index, hash_twice));
    }

    void BuildGraph(loom::GraphBuilder& graph, bool hash_twice)
    {
        graph.Pin("HASH_TWICE", loom::AxisValue::Boolean(hash_twice));
        graph.Import(loom::BufferImportDeclaration{ .Key = k_Output, .ByteSize = static_cast<int64_t>(k_Bytes) });
        graph.Declare(loom::BufferDeclaration
        {
            .Key = k_Output2,
            .Lifetime = loom::ResourceLifetime::Frame,
            .Size = loom::SizeSource::Bytes(static_cast<int64_t>(k_Bytes)),
            .Cpu = loom::CpuAccess::Readback,
            .HistoryDepth = 0,
            .NoHistoryAction = loom::OnNoHistory::Invalid
        });

        const auto fill = [](loom::DispatchBuilder& pass)
        {
            pass.Shader("LoomHashFill", "HashFillCS");
            pass.Bind("Output", k_Output, loom::AccessModifier::WriteOnly);
            pass.Params(k_FillParams);
            pass.DispatchThreads(static_cast<int32_t>(k_ElementCount), 1, 1);
        };

        const auto again = [](loom::DispatchBuilder& pass)
        {
            pass.Shader("LoomHashFill", "HashAgainCS");
            pass.Bind("Hashed", k_Output);
            pass.Bind("Output2", k_Output2, loom::AccessModifier::WriteOnly);
            pass.Params(k_AgainParams);
            pass.DispatchThreads(static_cast<int32_t>(k_ElementCount), 1, 1);
        };

        graph.AddDispatch(k_Fill, loom::RunPolicy::EveryFrame, loom::QueueAffinity::Graphics, fill);
        graph.AddDispatch(k_Again, loom::RunPolicy::EveryFrame, loom::QueueAffinity::Graphics, again);
    }

    void PrintDiagnostics(const loom::CollectingSink& sink, std::string_view variant_name)
    {
        for (const loom::Diagnostic& diagnostic : sink.Diagnostics())
        {
            std::cerr << std::format("[{}] {} {}: {} (pass '{}', resource '{}')\n", variant_name,
                                     loom::ToString(diagnostic.Code), loom::ToString(diagnostic.Severity),
                                     loom::SummaryOf(diagnostic.Code), diagnostic.Pass.Name, diagnostic.Resource.Name);
        }
    }

    // The imported Output: device-local, filled with the sentinel through the transfer system.
    rhi::ResourceFuture<GraphicsResource> CreateOutput(ResourceContext& resources)
    {
        const std::vector<uint32_t> sentinel(k_ElementCount, k_Sentinel);
        const RhiBufferResourceData initial{ std::as_bytes(std::span{ sentinel }), 0u };

        BufferCreateInfo info{};
        info.Domain = ResourceDomain::GPUOnly;
        info.Usage = BufferUsageBits::ComputeShaderUAV | BufferUsageBits::CopySource;
        info.Size = k_Bytes;
        info.Name = "LoomCompute.Output";
        return resources.CreateBuffer(info, std::span{ &initial, 1u });
    }

    // Ticks until k_Ticks frames are submitted. Tick never blocks, so the loop waits outside it: for a frame slot, or
    // for the resources to exist. Returns false on a failed Tick or a timeout.
    bool TickFrames(loom::Graph& graph, ResourceContext& resources, std::string_view variant_name)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::nanoseconds(k_TimeoutNs);
        int32_t submitted = 0;

        while (submitted < k_Ticks && std::chrono::steady_clock::now() < deadline)
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
                    std::cerr << std::format("[{}] the frame slot did not come free\n", variant_name);
                    return false;
                }

                break;
            case loom::TickStatus::NothingToRun:
                std::this_thread::yield();
                break;
            case loom::TickStatus::Invalid:
                [[fallthrough]];
            case loom::TickStatus::TargetNotReady:
                [[fallthrough]];
            case loom::TickStatus::Failed:
                std::cerr << std::format("[{}] Tick failed\n", variant_name);
                return false;
            }
        }

        return submitted == k_Ticks;
    }

    // Copies `source` out behind the graph's last submit, and counts the words that differ from `expected`.
    int32_t CheckBuffer(ResourceContext& resources, const loom::Graph& graph, GraphicsResource source,
                        std::string_view name, std::string_view variant_name, uint32_t (*expected)(uint32_t, bool), bool hash_twice)
    {
        BufferCreateInfo readbackInfo{};
        readbackInfo.Domain = ResourceDomain::GPUToCPU;
        readbackInfo.Usage = BufferUsageBits::CopyDestination;
        readbackInfo.Size = k_Bytes;
        rhi::ResourceFuture<GraphicsResource> readbackReply = resources.CreateBuffer(readbackInfo, {});

        if (resources.Wait(readbackReply, k_TimeoutNs) != rhi::FutureState::Complete)
        {
            std::cerr << std::format("[{}] the {} readback buffer was not created\n", variant_name, name);
            return 1;
        }

        const GraphicsResource readback = readbackReply.Get();
        const rhi::GpuWait last = graph.LastSubmit();
        const rhi::TransferWait afterGraph{ last.Semaphore, last.Value };
        rhi::OperationFuture copyReply = resources.CopyBufferContents(source, readback, std::span{ &afterGraph, 1u });
        rhi::ResourceFuture<void*> mapReply = resources.MapBuffer(readback, k_Bytes, 0u);

        if (resources.Wait(copyReply, k_TimeoutNs) != rhi::FutureState::Complete ||
            resources.Wait(mapReply, k_TimeoutNs) != rhi::FutureState::Complete)
        {
            std::cerr << std::format("[{}] the {} copy or map failed\n", variant_name, name);
            return 1;
        }

        const std::span<const uint32_t> words{ static_cast<const uint32_t*>(mapReply.Get()), k_ElementCount };
        int32_t mismatches = 0;

        for (uint32_t index = 0u; index < k_ElementCount; ++index)
        {
            const uint32_t want = expected(index, hash_twice);

            if (words[index] != want)
            {
                if (mismatches < 8)
                {
                    std::cerr << std::format("[{}] {} word {}: expected 0x{:08X}, got 0x{:08X}\n", variant_name, name,
                                             index, want, words[index]);
                }

                ++mismatches;
            }
        }

        rhi::OperationFuture unmapReply = resources.UnmapBuffer(readback, k_Bytes, 0u);
        rhi::OperationFuture destroyReply = resources.DestroyResource(readback);
        [[maybe_unused]] const rhi::FutureState unmapped = resources.Wait(unmapReply, k_TimeoutNs);
        [[maybe_unused]] const rhi::FutureState destroyed = resources.Wait(destroyReply, k_TimeoutNs);

        if (mismatches != 0)
        {
            std::cerr << std::format("[{}] {}: {} of {} words differ from the CPU\n", variant_name, name, mismatches, k_ElementCount);
            return 1;
        }

        std::cout << std::format("[{}] {}: {} words match the CPU\n", variant_name, name, k_ElementCount);
        return 0;
    }

    void PrintTimings(const loom::Graph& graph, std::string_view variant_name)
    {
        const std::optional<rhi::GpuTimingBlock> timings = graph.LatestTimings();

        if (!timings.has_value())
        {
            return;
        }

        for (const rhi::GpuTiming& timing : timings->Timings)
        {
            std::cout << std::format("[{}] pass time: {:.1f} us\n", variant_name, timing.Nanoseconds / 1000.0);
        }
    }

    int32_t RunVariant(HeadlessBoot& boot, bool hash_twice, loom::PipelineMode mode)
    {
        const std::string_view modeName = mode == loom::PipelineMode::ComputePipelines ? "pipelines" : "shader objects";
        const std::string variantName = std::format("HASH_TWICE={}, {}", hash_twice, modeName);
        ResourceContext& resources = boot.Gpu.Resources();
        const rhi::Device& device = *boot.Device.Rhi().GetDevice();
        const bootstrap::DeviceFilter filter = boot.Device.Filter();
        loom::CollectingSink sink;

        loom::GraphBuilder builder{ "LoomCompute.Graph", "LoomCompute.Shaders" };
        BuildGraph(builder, hash_twice);

        const loom::CompileInputs inputs
        {
            .Bundle = boot.Content.Bundles().front().Bundle,
            .Supports = filter.Supports,
            .SupportsUser = filter.User,
            .ShaderLimits = loom::ShaderLimitsOf(device),
            .Limits = loom::LayoutLimitsOf(device),
        };

        std::optional<loom::CompiledGraph> compiled = loom::Compile(builder, inputs, sink);

        if (!compiled.has_value())
        {
            std::cerr << std::format("[{}] Compile failed\n", variantName);
            PrintDiagnostics(sink, variantName);
            return 1;
        }

        const loom::RealizeInputs realizeInputs
        {
            .Resources = resources,
            .Device = device,
            .FrameSlots = k_FrameSlots,
            .Timestamps = loom::PassTimestamps::On,
            .Pipelines = mode,
            .Sink = sink,
        };

        loom::RealizeResult realized = loom::Graph::Realize(std::move(*compiled), realizeInputs);

        if (!realized.has_value())
        {
            std::cerr << std::format("[{}] Realize refused the graph: {}\n", variantName, loom::ToString(realized.error()));
            return 1;
        }

        std::unique_ptr<loom::Graph> graph = std::move(*realized);
        graph->Supply(k_Output, CreateOutput(resources));
        graph->Set(k_FillParams, FillParams{ .seed = k_Seed, .count = k_ElementCount });
        graph->Set(k_AgainParams, AgainParams{ .count = k_ElementCount });

        int32_t failures = TickFrames(*graph, resources, variantName) ? 0 : 1;
        const GraphicsResource output = graph->BufferOf(k_Output);
        const GraphicsResource output2 = graph->BufferOf(k_Output2);

        if (failures == 0)
        {
            failures += CheckBuffer(resources, *graph, output, "Output", variantName, &ExpectedOutput, hash_twice);
            failures += CheckBuffer(resources, *graph, output2, "Output2", variantName, &ExpectedOutput2, hash_twice);
            PrintTimings(*graph, variantName);
        }

        // the graph waits for its own work, and frees Output2; the test owns Output, so it frees it after the graph
        graph.reset();

        if (output)
        {
            rhi::OperationFuture destroyReply = resources.DestroyResource(output);
            [[maybe_unused]] const rhi::FutureState destroyed = resources.Wait(destroyReply, k_TimeoutNs);
        }

        if (sink.ErrorCount() != 0)
        {
            PrintDiagnostics(sink, variantName);
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
        .ApplicationName = "LoomComputeTest",
    };

    HeadlessBoot boot;

    if (!StartHeadlessBoot(boot, std::span{ argv, static_cast<size_t>(argc) }, k_Bundles, bootstrap::ShaderObjectNeeds(), config))
    {
        return 1;
    }

    int32_t failures = 0;

    // synchronization validation sees a dispatch's descriptor accesses only through a bound VkPipeline, so the
    // pipeline mode is what proves the barriers; the shader object mode proves the engine's own path
    for (const loom::PipelineMode mode : { loom::PipelineMode::ShaderObjects, loom::PipelineMode::ComputePipelines })
    {
        for (const bool hashTwice : { false, true })
        {
            failures += RunVariant(boot, hashTwice, mode);
        }
    }

    if (failures != 0)
    {
        std::cerr << std::format("LoomComputeTest: {} failure(s)\n", failures);
        return 1;
    }

    std::cout << "LoomComputeTest: both variants match the CPU reference in both pipeline modes\n";
    return 0;
}
