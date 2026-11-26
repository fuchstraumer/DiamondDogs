#include "Compile.hpp"
#include "CompiledGraph.hpp"
#include "Declarations.hpp"
#include "Device.hpp"
#include "DeviceConfig.hpp"
#include "DeviceStage.hpp"
#include "FramePlan.hpp"
#include "GraphBuilder.hpp"
#include "HeadlessBoot.hpp"
#include "ImageDataFormats.hpp"
#include "LoadedBundle.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "LoomGraph.hpp"
#include "LoomKeys.hpp"
#include "PassBuilders.hpp"
#include "ResourceContext.hpp"
#include "ResourceFlags.hpp"
#include "ResourceFuture.hpp"
#include "ResourceTypes.hpp"
#include "RhiResourceFlags.hpp"
#include "RhiFlags.hpp"
#include "RhiSystem.hpp"
#include "ShaderBundle.hpp"
#include "ShaderLibrary.host.hpp"
#include "SpineNeeds.hpp"
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

// Loom step 6 on the GPU, headless, against the CPU. One graph of seven passes, ticked k_Ticks times on two frame slots:
//  - Paint writes Painted, a storage image; Sample samples it in the next level, so Loom changes its layout between
//    them. Sample also samples Lut, an imported image the test uploads, with a point sampler, and its dispatch covers
//    Lut's extent, which Loom learns only from the supplied image.
//  - Accumulate adds 1 to last frame's Counter (a history buffer, OnNoHistory::Clear); AccumulateImage does the same on
//    CountImage (a history image), and CopyCount copies this frame's CountImage to a buffer. Each count must equal the
//    number of times its pass ran: the first run reads a cleared copy, and every later run reads the copy the frame
//    before wrote.
//  - Changed (WhenInputsChange) adds its params value to Changed. The test sets the same value every Tick, and a new
//    value once: it must run twice. Triggered (OnTrigger) adds 1 to Triggered; two triggers before one Tick and one
//    later must make two runs.
// Synchronization validation is on, so a missing barrier or a wrong layout prints a message, and the run fails.

namespace
{
    namespace host = loom_image_test_shaders;
    using PaintParams = host::LoomImages::PaintCS::Params<host::CookProfile::Spirv>;
    using SampleParams = host::LoomImages::SampleCS::Params<host::CookProfile::Spirv>;
    using AccumulateParams = host::LoomImages::AccumulateCS::Params<host::CookProfile::Spirv>;
    using CopyCountParams = host::LoomImages::CopyCountCS::Params<host::CookProfile::Spirv>;
    using ChangedParams = host::LoomImages::ChangedCS::Params<host::CookProfile::Spirv>;
    using TriggeredParams = host::LoomImages::TriggeredCS::Params<host::CookProfile::Spirv>;

    constexpr int32_t k_Size = 32;
    constexpr uint32_t k_Texels = static_cast<uint32_t>(k_Size * k_Size);
    constexpr uint32_t k_Seed = 7u;
    constexpr int32_t k_FrameSlots = 2;
    constexpr int32_t k_Ticks = 8;
    constexpr uint64_t k_TimeoutNs = 5'000'000'000ull;

    constexpr loom::ResourceKey<loom::Image> k_Painted{ "LoomImage.Painted" };
    constexpr loom::ResourceKey<loom::Image> k_Lut{ "LoomImage.Lut" };
    constexpr loom::ResourceKey<loom::Sampler> k_Point{ "LoomImage.Point" };
    constexpr loom::ResourceKey<loom::Buffer> k_Sampled{ "LoomImage.Sampled" };
    constexpr loom::ResourceKey<loom::Buffer> k_Counter{ "LoomImage.Counter" };
    constexpr loom::ResourceKey<loom::Image> k_CountImage{ "LoomImage.CountImage" };
    constexpr loom::ResourceKey<loom::Buffer> k_CountOut{ "LoomImage.CountOut" };
    constexpr loom::ResourceKey<loom::Buffer> k_Changed{ "LoomImage.Changed" };
    constexpr loom::ResourceKey<loom::Buffer> k_Triggered{ "LoomImage.Triggered" };

    constexpr loom::PassKey k_Paint{ "LoomImage.Paint" };
    constexpr loom::PassKey k_Sample{ "LoomImage.Sample" };
    constexpr loom::PassKey k_Accumulate{ "LoomImage.Accumulate" };
    constexpr loom::PassKey k_AccumulateImage{ "LoomImage.AccumulateImage" };
    constexpr loom::PassKey k_CopyCount{ "LoomImage.CopyCount" };
    constexpr loom::PassKey k_ChangedPass{ "LoomImage.ChangedPass" };
    constexpr loom::PassKey k_TriggeredPass{ "LoomImage.TriggeredPass" };

    constexpr loom::ParamsKey<PaintParams> k_PaintParams{ "LoomImage.PaintParams" };
    constexpr loom::ParamsKey<SampleParams> k_SampleParams{ "LoomImage.SampleParams" };
    constexpr loom::ParamsKey<AccumulateParams> k_AccumulateParams{ "LoomImage.AccumulateParams" };
    constexpr loom::ParamsKey<CopyCountParams> k_CopyCountParams{ "LoomImage.CopyCountParams" };
    constexpr loom::ParamsKey<ChangedParams> k_ChangedParams{ "LoomImage.ChangedParams" };
    constexpr loom::ParamsKey<TriggeredParams> k_TriggeredParams{ "LoomImage.TriggeredParams" };

    // The passes in declaration order, which is their pass index
    enum PassIndex : int32_t
    {
        Paint = 0,
        Sample = 1,
        Accumulate = 2,
        AccumulateImage = 3,
        CopyCount = 4,
        ChangedPass = 5,
        TriggeredPass = 6,
        PassCount = 7
    };

    constexpr bootstrap::BundleRequest k_Bundles[]
    {
        bootstrap::BundleRequest
        {
            .FileName = "ShaderLibrary.ldmanifest",
            .BuildTreePath = LOOM_IMAGE_TEST_MANIFEST,
            .Profile = rhi::k_SpirvProfileName,
        },
    };

    loom::BufferDeclaration ReadbackBuffer(loom::ResourceKey<loom::Buffer> key, int64_t byte_count, int32_t history_depth,
                                           loom::OnNoHistory no_history)
    {
        return loom::BufferDeclaration
        {
            .Key = key,
            .Lifetime = loom::ResourceLifetime::Persistent,
            .Size = loom::SizeSource::Bytes(byte_count),
            .Cpu = loom::CpuAccess::Readback,
            .HistoryDepth = history_depth,
            .NoHistoryAction = no_history,
        };
    }

    loom::ImageDeclaration FixedImage(loom::ResourceKey<loom::Image> key, lodestone::TextureFormat format,
                                      int32_t history_depth, loom::OnNoHistory no_history)
    {
        return loom::ImageDeclaration
        {
            .Key = key,
            .Lifetime = loom::ResourceLifetime::Persistent,
            .Format = format,
            .Extent = loom::ExtentSource::Fixed(k_Size, k_Size, 1),
            .Mips = loom::MipSource::One(),
            .Layers = loom::LayerSource::Fixed(1),
            .Cpu = loom::CpuAccess::None,
            .HistoryDepth = history_depth,
            .NoHistoryAction = no_history,
        };
    }

    void DeclareResources(loom::GraphBuilder& graph)
    {
        constexpr int64_t k_Words = int64_t{ k_Texels } * 4;
        constexpr int64_t k_Float4s = int64_t{ k_Texels } * 16;

        graph.Declare(FixedImage(k_Painted, lodestone::TextureFormat::RGBA32Float, 0, loom::OnNoHistory::Invalid));
        graph.Import(loom::ImportDeclaration{ .Key = k_Lut,
                                              .Expectation = loom::ImageExpectation::AnyExtent(lodestone::TextureFormat::RGBA32Float, 1) });

        SamplerCreateInfo point{};
        point.MagFilter = ImageFilteringMode::Nearest;
        point.MinFilter = ImageFilteringMode::Nearest;
        point.MipFilter = ImageFilteringMode::Nearest;
        point.AddressU = SamplerCreateInfo::AddressMode::ClampToEdge;
        point.AddressV = SamplerCreateInfo::AddressMode::ClampToEdge;
        point.AddressW = SamplerCreateInfo::AddressMode::ClampToEdge;
        point.Name = "LoomImage.Point";
        graph.Declare(loom::SamplerDeclaration{ .Key = k_Point, .CreateInfo = point });

        graph.Declare(ReadbackBuffer(k_Sampled, k_Float4s, 0, loom::OnNoHistory::Invalid));
        graph.Declare(ReadbackBuffer(k_Counter, k_Words, 1, loom::OnNoHistory::Clear));
        graph.Declare(FixedImage(k_CountImage, lodestone::TextureFormat::R32Uint, 1, loom::OnNoHistory::Clear));
        graph.Declare(ReadbackBuffer(k_CountOut, k_Words, 0, loom::OnNoHistory::Invalid));
        graph.Import(loom::BufferImportDeclaration{ .Key = k_Changed, .ByteSize = k_Words });
        graph.Import(loom::BufferImportDeclaration{ .Key = k_Triggered, .ByteSize = k_Words });
    }

    void AddPasses(loom::GraphBuilder& graph)
    {
        constexpr loom::RunPolicy k_EveryFrame = loom::RunPolicy::EveryFrame;
        constexpr loom::QueueAffinity k_Graphics = loom::QueueAffinity::Graphics;

        const auto paint = [](loom::DispatchBuilder& pass)
        {
            pass.Shader("LoomImages", "PaintCS");
            pass.Bind("Painted", k_Painted, loom::AccessModifier::WriteOnly);
            pass.Params(k_PaintParams);
            pass.DispatchThreads(k_Size, k_Size, 1);
        };

        const auto sample = [](loom::DispatchBuilder& pass)
        {
            pass.Shader("LoomImages", "SampleCS");
            pass.Bind("PaintedRead", k_Painted);
            pass.Bind("Lut", k_Lut);
            pass.Bind("PointSampler", k_Point);
            pass.Bind("Sampled", k_Sampled, loom::AccessModifier::WriteOnly);
            pass.Params(k_SampleParams);
            pass.DispatchThreadsOver(k_Lut);
        };

        const auto accumulate = [](loom::DispatchBuilder& pass)
        {
            pass.Shader("LoomImages", "AccumulateCS");
            pass.BindPrevious("PrevCount", k_Counter);
            pass.Bind("NextCount", k_Counter, loom::AccessModifier::WriteOnly);
            pass.Params(k_AccumulateParams);
            pass.DispatchThreads(static_cast<int32_t>(k_Texels), 1, 1);
        };

        const auto accumulateImage = [](loom::DispatchBuilder& pass)
        {
            pass.Shader("LoomImages", "AccumulateImageCS");
            pass.BindPrevious("PrevCountImage", k_CountImage);
            pass.Bind("NextCountImage", k_CountImage, loom::AccessModifier::WriteOnly);
            pass.DispatchThreads(k_Size, k_Size, 1);
        };

        const auto copyCount = [](loom::DispatchBuilder& pass)
        {
            pass.Shader("LoomImages", "CopyCountCS");
            pass.Bind("CountImageRead", k_CountImage);
            pass.Bind("CountImageOut", k_CountOut, loom::AccessModifier::WriteOnly);
            pass.Params(k_CopyCountParams);
            pass.DispatchThreads(k_Size, k_Size, 1);
        };

        const auto changed = [](loom::DispatchBuilder& pass)
        {
            pass.Shader("LoomImages", "ChangedCS");
            pass.Bind("Changed", k_Changed);
            pass.Params(k_ChangedParams);
            pass.DispatchThreads(static_cast<int32_t>(k_Texels), 1, 1);
        };

        const auto triggered = [](loom::DispatchBuilder& pass)
        {
            pass.Shader("LoomImages", "TriggeredCS");
            pass.Bind("Triggered", k_Triggered);
            pass.Params(k_TriggeredParams);
            pass.DispatchThreads(static_cast<int32_t>(k_Texels), 1, 1);
        };

        graph.AddDispatch(k_Paint, k_EveryFrame, k_Graphics, paint);
        graph.AddDispatch(k_Sample, k_EveryFrame, k_Graphics, sample);
        graph.AddDispatch(k_Accumulate, k_EveryFrame, k_Graphics, accumulate);
        graph.AddDispatch(k_AccumulateImage, k_EveryFrame, k_Graphics, accumulateImage);
        graph.AddDispatch(k_CopyCount, k_EveryFrame, k_Graphics, copyCount);
        graph.AddDispatch(k_ChangedPass, loom::RunPolicy::WhenInputsChange, k_Graphics, changed);
        graph.AddDispatch(k_TriggeredPass, loom::RunPolicy::OnTrigger, k_Graphics, triggered);
    }

    void PrintDiagnostics(const loom::CollectingSink& sink)
    {
        for (const loom::Diagnostic& diagnostic : sink.Diagnostics())
        {
            std::cerr << std::format("{} {}: {} (pass '{}', resource '{}')\n", loom::ToString(diagnostic.Code),
                                     loom::ToString(diagnostic.Severity), loom::SummaryOf(diagnostic.Code),
                                     diagnostic.Pass.Name, diagnostic.Resource.Name);
        }
    }

    std::unique_ptr<loom::Graph> BuildAndRealize(HeadlessBoot& boot, loom::CollectingSink& sink)
    {
        ResourceContext& resources = boot.Gpu.Resources();
        const rhi::Device& device = *boot.Device.Rhi().GetDevice();
        const bootstrap::DeviceFilter filter = boot.Device.Filter();

        loom::GraphBuilder builder{ "LoomImage.Graph", "LoomImage.Shaders" };
        DeclareResources(builder);
        AddPasses(builder);

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
            std::cerr << "Compile failed\n";
            PrintDiagnostics(sink);
            return nullptr;
        }

        const loom::RealizeInputs realizeInputs
        {
            .Resources = resources,
            .Device = device,
            .FrameSlots = k_FrameSlots,
            .Timestamps = loom::PassTimestamps::Off,
            .Aliasing = loom::MemoryAliasing::On,
            .Sink = sink,
            .FrameTargets = {},
            .TargetSettleSeconds = 0.0,
        };

        loom::RealizeResult realized = loom::Graph::Realize(std::move(*compiled), realizeInputs);

        if (!realized.has_value())
        {
            std::cerr << std::format("Realize refused the graph: {}\n", loom::ToString(realized.error()));
            return nullptr;
        }

        return std::move(*realized);
    }

    float LutValue(int32_t x, int32_t y, int32_t component) noexcept
    {
        const std::array<float, 4u> texel{ 100.0f + static_cast<float>(x), 200.0f + static_cast<float>(y), 0.0f, 1.0f };
        return texel[static_cast<size_t>(component)];
    }

    float PaintedValue(int32_t x, int32_t y, int32_t component) noexcept
    {
        const std::array<float, 4u> texel{ static_cast<float>(x), static_cast<float>(y), static_cast<float>(k_Seed),
                                           static_cast<float>(x * y) };
        return texel[static_cast<size_t>(component)];
    }

    // The imported Lut: device-local rgba32f, uploaded, and left in the compute sampled usage.
    rhi::ResourceFuture<GraphicsResource> CreateLut(ResourceContext& resources)
    {
        std::vector<float> texels(static_cast<size_t>(k_Texels) * 4u);

        for (int32_t y = 0; y < k_Size; ++y)
        {
            for (int32_t x = 0; x < k_Size; ++x)
            {
                for (int32_t component = 0; component < 4; ++component)
                {
                    texels[(static_cast<size_t>(y) * k_Size + x) * 4u + component] = LutValue(x, y, component);
                }
            }
        }

        ImageCreateInfo info{};
        info.Domain = ResourceDomain::GPUOnly;
        info.Usage = ImageUsageBits::ComputeShaderSRV;
        info.Type = ImageType::Type2D;
        info.Format = ImageFormat{ ImageComponentFormats::RGBA32, ImageDataType::Float };
        info.Width = static_cast<uint32_t>(k_Size);
        info.Height = static_cast<uint32_t>(k_Size);
        info.Depth = 1u;
        info.Name = "LoomImage.Lut";

        ImageViewCreateInfo view{};
        view.ViewType = ImageViewType::Type2D;
        view.Range = ImageRange{ ImageAspectFlags::Color, 0u, 1u, 0u, 1u };
        info.ViewInfo = view;

        RhiImageResourceData data{};
        data.Data = std::as_bytes(std::span{ texels });
        data.Width = k_Size;
        data.Height = k_Size;
        data.Depth = 1;
        return resources.CreateImage(info, std::span{ &data, 1u }, ImageUsageBits::ComputeShaderSRV);
    }

    rhi::ResourceFuture<GraphicsResource> CreateZeroBuffer(ResourceContext& resources, std::string_view name)
    {
        const std::vector<uint32_t> zeros(k_Texels, 0u);
        const RhiBufferResourceData initial{ std::as_bytes(std::span{ zeros }), 0u };

        BufferCreateInfo info{};
        info.Domain = ResourceDomain::GPUOnly;
        info.Usage = BufferUsageBits::ComputeShaderUAV | BufferUsageBits::TransferSource;
        info.Size = sizeof(uint32_t) * k_Texels;
        info.Name = name;
        return resources.CreateBuffer(info, std::span{ &initial, 1u });
    }

    // Copies `source` out behind the graph's last submit. Empty when a step failed.
    std::vector<std::byte> ReadBack(ResourceContext& resources, const loom::Graph& graph, GraphicsResource source, size_t bytes)
    {
        BufferCreateInfo readbackInfo{};
        readbackInfo.Domain = ResourceDomain::GPUToCPU;
        readbackInfo.Usage = BufferUsageBits::TransferDestination;
        readbackInfo.Size = bytes;
        rhi::ResourceFuture<GraphicsResource> readbackReply = resources.CreateBuffer(readbackInfo, {});

        if (resources.Wait(readbackReply, k_TimeoutNs) != rhi::FutureState::Complete)
        {
            return {};
        }

        const GraphicsResource readback = readbackReply.Get();
        const rhi::GpuWait last = graph.LastSubmit();
        const rhi::TransferWait afterGraph{ last.Semaphore, last.Value };
        rhi::OperationFuture copyReply = resources.CopyBufferContents(source, readback, std::span{ &afterGraph, 1u });
        rhi::ResourceFuture<void*> mapReply = resources.MapBuffer(readback, bytes, 0u);
        std::vector<std::byte> result;

        const bool copied = resources.Wait(copyReply, k_TimeoutNs) == rhi::FutureState::Complete &&
                            resources.Wait(mapReply, k_TimeoutNs) == rhi::FutureState::Complete;

        if (copied)
        {
            const std::byte* mapped = static_cast<const std::byte*>(mapReply.Get());
            result.assign(mapped, mapped + bytes);
        }

        rhi::OperationFuture unmapReply = resources.UnmapBuffer(readback, bytes, 0u);
        rhi::OperationFuture destroyReply = resources.DestroyResource(readback);
        [[maybe_unused]] const rhi::FutureState unmapped = resources.Wait(unmapReply, k_TimeoutNs);
        [[maybe_unused]] const rhi::FutureState destroyed = resources.Wait(destroyReply, k_TimeoutNs);
        return result;
    }

    // Counts the words of `name` that differ from `expected(index)`. Prints the first few.
    int32_t CheckWords(std::span<const std::byte> bytes, std::string_view name, size_t count,
                       const std::function<uint32_t(size_t)>& expected)
    {
        if (bytes.size() < count * sizeof(uint32_t))
        {
            std::cerr << std::format("{}: the readback failed\n", name);
            return 1;
        }

        int32_t mismatches = 0;

        for (size_t index = 0u; index < count; ++index)
        {
            uint32_t word = 0u;
            std::memcpy(&word, bytes.data() + index * sizeof(uint32_t), sizeof(uint32_t));
            const uint32_t want = expected(index);

            if (word == want)
            {
                continue;
            }

            if (mismatches < 8)
            {
                std::cerr << std::format("{} word {}: expected 0x{:08X}, got 0x{:08X}\n", name, index, want, word);
            }

            ++mismatches;
        }

        if (mismatches != 0)
        {
            std::cerr << std::format("{}: {} of {} words differ from the CPU\n", name, mismatches, count);
            return 1;
        }

        std::cout << std::format("{}: {} words match the CPU\n", name, count);
        return 0;
    }

    uint32_t FloatBits(float value) noexcept
    {
        uint32_t bits = 0u;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    }

    // How often each pass ran, from each submitted Tick's plan
    using RunCounts = std::array<int32_t, PassCount>;

    // The per-Tick inputs: the same Changed value every Tick, a new one at Tick 3; two triggers before Tick 1, one
    // before Tick 4.
    void SetInputs(loom::Graph& graph, int32_t tick)
    {
        const uint32_t value = tick < 3 ? 1u : 2u;
        graph.Set(k_ChangedParams, ChangedParams{ .count = k_Texels, .value = value });

        if (tick == 1)
        {
            graph.Trigger(k_TriggeredPass);
            graph.Trigger(k_TriggeredPass);
        }

        if (tick == 4)
        {
            graph.Trigger(k_TriggeredPass);
        }
    }

    // Ticks until k_Ticks frames are submitted, and counts the runs of each pass. Tick never blocks, so the loop waits
    // outside it.
    std::optional<RunCounts> TickFrames(loom::Graph& graph, ResourceContext& resources)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::nanoseconds(k_TimeoutNs);
        RunCounts runs{};
        int32_t submitted = 0;

        while (submitted < k_Ticks && std::chrono::steady_clock::now() < deadline)
        {
            SetInputs(graph, submitted);
            const ConsumerTimelineValue pending{ graph.Timeline(), graph.PendingValue() };
            resources.Tick(std::span{ &pending, 1u });

            const loom::TickResult result = graph.Tick(std::chrono::duration<double>{ 1.0 / 60.0 });

            switch (result.Status)
            {
            case loom::TickStatus::Submitted:
                for (int32_t pass = 0; pass < PassCount; ++pass)
                {
                    runs[pass] += graph.LastPlan().Decisions[pass].Reason == loom::SkipReason::Ran ? 1 : 0;
                }

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
                std::this_thread::yield();
                break;
            case loom::TickStatus::Invalid:
                [[fallthrough]];
            case loom::TickStatus::TargetNotReady:
                [[fallthrough]];
            case loom::TickStatus::Failed:
                std::cerr << "Tick failed\n" << graph.ExplainLastTick();
                return std::nullopt;
            }
        }

        if (submitted != k_Ticks)
        {
            std::cerr << "timed out before every Tick was submitted\n" << graph.ExplainLastTick();
            return std::nullopt;
        }

        return runs;
    }

    int32_t CheckResults(ResourceContext& resources, const loom::Graph& graph, const RunCounts& runs)
    {
        int32_t failures = 0;

        const auto sampled = [](size_t index)
        {
            const size_t texel = index / 4u;
            const int32_t x = static_cast<int32_t>(texel % k_Size);
            const int32_t y = static_cast<int32_t>(texel / k_Size);
            const int32_t component = static_cast<int32_t>(index % 4u);
            return FloatBits(PaintedValue(x, y, component) + LutValue(x, y, component));
        };

        const std::vector<std::byte> sampledBytes = ReadBack(resources, graph, graph.BufferOf(k_Sampled), size_t{ k_Texels } * 16u);
        failures += CheckWords(sampledBytes, "Sampled", size_t{ k_Texels } * 4u, sampled);

        const auto counted = [](uint32_t run_count)
        {
            return [run_count](size_t)
            {
                return run_count;
            };
        };

        const size_t wordBytes = size_t{ k_Texels } * 4u;
        const std::vector<std::byte> counterBytes = ReadBack(resources, graph, graph.BufferOf(k_Counter), wordBytes);
        failures += CheckWords(counterBytes, "Counter (history buffer)", k_Texels, counted(static_cast<uint32_t>(runs[Accumulate])));

        const std::vector<std::byte> countOutBytes = ReadBack(resources, graph, graph.BufferOf(k_CountOut), wordBytes);
        failures += CheckWords(countOutBytes, "CountImage (history image)", k_Texels,
                               counted(static_cast<uint32_t>(runs[AccumulateImage])));

        const std::vector<std::byte> changedBytes = ReadBack(resources, graph, graph.BufferOf(k_Changed), wordBytes);
        failures += CheckWords(changedBytes, "Changed", k_Texels, counted(3u));

        const std::vector<std::byte> triggeredBytes = ReadBack(resources, graph, graph.BufferOf(k_Triggered), wordBytes);
        failures += CheckWords(triggeredBytes, "Triggered", k_Texels, counted(2u));

        // every frame-driven pass ran once the resources existed; the policy passes ran exactly twice
        const bool counts = runs[ChangedPass] == 2 && runs[TriggeredPass] == 2 && runs[Accumulate] > 0 &&
                            runs[AccumulateImage] == runs[Accumulate];

        if (!counts)
        {
            std::cerr << std::format("run counts: Accumulate {}, AccumulateImage {}, Changed {} (want 2), Triggered {} (want 2)\n",
                                     runs[Accumulate], runs[AccumulateImage], runs[ChangedPass], runs[TriggeredPass]);
            ++failures;
        }

        std::cout << std::format("runs: Paint {}, Sample {}, Accumulate {}, AccumulateImage {}, CopyCount {}, Changed {}, Triggered {}\n",
                                 runs[Paint], runs[Sample], runs[Accumulate], runs[AccumulateImage], runs[CopyCount],
                                 runs[ChangedPass], runs[TriggeredPass]);
        return failures;
    }

    void DestroyImport(ResourceContext& resources, GraphicsResource resource)
    {
        if (!resource)
        {
            return;
        }

        rhi::OperationFuture destroyReply = resources.DestroyResource(resource);
        [[maybe_unused]] const rhi::FutureState destroyed = resources.Wait(destroyReply, k_TimeoutNs);
    }

    int32_t RunTest(HeadlessBoot& boot)
    {
        ResourceContext& resources = boot.Gpu.Resources();
        loom::CollectingSink sink;
        std::unique_ptr<loom::Graph> graph = BuildAndRealize(boot, sink);

        if (!graph)
        {
            return 1;
        }

        graph->Supply(k_Lut, CreateLut(resources), ImageUsageBits::ComputeShaderSRV);
        graph->Supply(k_Changed, CreateZeroBuffer(resources, "LoomImage.Changed"));
        graph->Supply(k_Triggered, CreateZeroBuffer(resources, "LoomImage.Triggered"));
        graph->Set(k_PaintParams, PaintParams{ .seed = k_Seed });
        graph->Set(k_SampleParams, SampleParams{ .width = static_cast<uint32_t>(k_Size) });
        graph->Set(k_AccumulateParams, AccumulateParams{ .count = k_Texels });
        graph->Set(k_CopyCountParams, CopyCountParams{ .width = static_cast<uint32_t>(k_Size) });
        graph->Set(k_TriggeredParams, TriggeredParams{ .count = k_Texels });

        const std::optional<RunCounts> runs = TickFrames(*graph, resources);
        int32_t failures = runs.has_value() ? CheckResults(resources, *graph, *runs) : 1;

        const GraphicsResource lut = graph->ImageOf(k_Lut);
        const GraphicsResource changed = graph->BufferOf(k_Changed);
        const GraphicsResource triggered = graph->BufferOf(k_Triggered);

        // the graph waits for its own work and frees what it made; the test owns the imports
        graph.reset();
        DestroyImport(resources, lut);
        DestroyImport(resources, changed);
        DestroyImport(resources, triggered);

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
        .ApplicationName = "LoomImageTest",
    };

    HeadlessBoot boot;

    if (!StartHeadlessBoot(boot, std::span{ argv, static_cast<size_t>(argc) }, k_Bundles, bootstrap::ShaderObjectNeeds(), config))
    {
        return 1;
    }

    const int32_t failures = RunTest(boot);

    if (failures != 0)
    {
        std::cerr << std::format("LoomImageTest: {} failure(s)\n", failures);
        return 1;
    }

    std::cout << "LoomImageTest: images, history rings and run policies match the CPU\n";
    return 0;
}
