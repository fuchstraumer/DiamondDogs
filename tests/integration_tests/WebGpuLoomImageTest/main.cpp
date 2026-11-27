#include "Compile.hpp"
#include "CompiledGraph.hpp"
#include "Declarations.hpp"
#include "DeviceConfig.hpp"
#include "FramePlan.hpp"
#include "GraphBuilder.hpp"
#include "ImageDataFormats.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomGraph.hpp"
#include "LoomKeys.hpp"
#include "PassBuilders.hpp"
#include "ResourceContext.hpp"
#include "ResourceContextWebGpu.hpp"
#include "ResourceFlags.hpp"
#include "ResourceFuture.hpp"
#include "ResourceTypes.hpp"
#include "RhiContextWebGpu.hpp"
#include "RhiFlags.hpp"
#include "RhiResourceFlags.hpp"
#include "ShaderBundle.hpp"
#include "ShaderLibrary.host.hpp"
#include "WebGpuDeviceStage.hpp"

#include <emscripten/emscripten.h>
#include <webgpu/webgpu_cpp.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// LoomImageTest on WebGPU, in a browser, against the same CPU values. One graph of seven passes, ticked k_Ticks times:
//  - Paint writes Painted, a storage image; Sample samples it in the next level. Sample also samples Lut, an imported
//    image the test uploads, with a point sampler, and its dispatch covers Lut's extent, which Loom learns only from
//    the supplied image.
//  - Accumulate adds 1 to last frame's Counter (a history buffer, OnNoHistory::Clear); AccumulateImage does the same on
//    CountImage (a history image), and CopyCount copies this frame's CountImage to a buffer. Each count must equal the
//    number of times its pass ran.
//  - Changed (WhenInputsChange) adds its params value to Changed: the same value every Tick, and a new one once, so it
//    must run twice. Triggered (OnTrigger) adds 1 to Triggered: two triggers before one Tick and one later make two runs.
// The device's uncaptured-error callback prints every WebGPU error, so a clean console is part of a pass.
namespace
{
    namespace host = webgpu_loom_image_test_shaders;
    using PaintParams = host::LoomImages::PaintCS::Params<host::CookProfile::Wgsl>;
    using SampleParams = host::LoomImages::SampleCS::Params<host::CookProfile::Wgsl>;
    using AccumulateParams = host::LoomImages::AccumulateCS::Params<host::CookProfile::Wgsl>;
    using CopyCountParams = host::LoomImages::CopyCountCS::Params<host::CookProfile::Wgsl>;
    using ChangedParams = host::LoomImages::ChangedCS::Params<host::CookProfile::Wgsl>;
    using TriggeredParams = host::LoomImages::TriggeredCS::Params<host::CookProfile::Wgsl>;

    constexpr int32_t k_Size = 32;
    constexpr uint32_t k_Texels = static_cast<uint32_t>(k_Size * k_Size);
    constexpr uint64_t k_WordBytes = uint64_t{ k_Texels } * 4u;
    constexpr uint64_t k_Float4Bytes = uint64_t{ k_Texels } * 16u;
    constexpr uint32_t k_Seed = 7u;
    constexpr int32_t k_FrameSlots = 2;
    constexpr int32_t k_Ticks = 8;
    constexpr int32_t k_MaxWaitTicks = 6000;

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

    // The buffers the test reads back, in the order of AppState::Readbacks
    enum ReadbackIndex : int32_t
    {
        SampledReadback = 0,
        CounterReadback = 1,
        CountOutReadback = 2,
        ChangedReadback = 3,
        TriggeredReadback = 4,
        ReadbackCount = 5
    };

    enum class TestPhase : uint8_t
    {
        Invalid = 0,
        Booting = 1,
        Start = 2,
        Ticking = 3,
        MappingReadback = 4,
        Finished = 5
    };

    struct ReadbackSlot
    {
        wgpu::Buffer Buffer;
        uint64_t Bytes{ 0u };
        bool Done{ false };
        bool Failed{ false };
    };

    // How often each pass ran, from each submitted Tick's plan
    using RunCounts = std::array<int32_t, PassCount>;

    struct AppState
    {
        TestPhase Phase{ TestPhase::Booting };
        bootstrap::WebGpuDeviceStage DeviceStage;
        rhi::WebGpuRhiContextView View{};
        std::unique_ptr<ResourceContext> Resources;
        std::optional<rhi::ShaderBundle> Bundle;
        std::unique_ptr<loom::CollectingSink> Sink;
        std::unique_ptr<loom::Graph> Graph;
        std::array<GraphicsResource, 3u> Imports{};
        std::array<ReadbackSlot, ReadbackCount> Readbacks{};
        RunCounts Runs{};
        int32_t SubmittedTicks{ 0 };
        int32_t WaitTicks{ 0 };
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

        graph.Declare(ReadbackBuffer(k_Sampled, static_cast<int64_t>(k_Float4Bytes), 0, loom::OnNoHistory::Invalid));
        graph.Declare(ReadbackBuffer(k_Counter, static_cast<int64_t>(k_WordBytes), 1, loom::OnNoHistory::Clear));
        graph.Declare(FixedImage(k_CountImage, lodestone::TextureFormat::R32Uint, 1, loom::OnNoHistory::Clear));
        graph.Declare(ReadbackBuffer(k_CountOut, static_cast<int64_t>(k_WordBytes), 0, loom::OnNoHistory::Invalid));
        graph.Import(loom::BufferImportDeclaration{ .Key = k_Changed, .ByteSize = static_cast<int64_t>(k_WordBytes) });
        graph.Import(loom::BufferImportDeclaration{ .Key = k_Triggered, .ByteSize = static_cast<int64_t>(k_WordBytes) });
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
            // a text value is the backend's own words, for example the browser's message in LM705
            const bool hasText = diagnostic.Actual.Kind == loom::DiagnosticValueKind::Text;

            std::println(stderr, "{} {}: {} (pass '{}', resource '{}'){}{}", loom::ToString(diagnostic.Code),
                         loom::ToString(diagnostic.Severity), loom::SummaryOf(diagnostic.Code), diagnostic.Pass.Name,
                         diagnostic.Resource.Name, hasText ? ": " : "", hasText ? diagnostic.Actual.Text : std::string_view{});
        }
    }

    std::unique_ptr<loom::Graph> BuildAndRealize(const AppState& state, loom::CollectingSink& sink)
    {
        const rhi::RhiContext& context = state.DeviceStage.Context();
        loom::GraphBuilder builder{ "LoomImage.Graph", "LoomImage.Shaders" };
        DeclareResources(builder);
        AddPasses(builder);

        const loom::CompileInputs inputs
        {
            .Bundle = *state.Bundle,
            .Supports = nullptr,
            .SupportsUser = nullptr,
            .ShaderLimits = loom::ShaderLimitsOf(context),
            .Limits = loom::LayoutLimitsOf(context),
        };

        std::optional<loom::CompiledGraph> compiled = loom::Compile(builder, inputs, sink);

        if (!compiled.has_value())
        {
            std::println(stderr, "Compile failed");
            PrintDiagnostics(sink);
            return nullptr;
        }

        const loom::RealizeInputs realizeInputs
        {
            .Resources = *state.Resources,
            .Context = context,
            .FrameSlots = k_FrameSlots,
            .Timestamps = loom::PassTimestamps::Off,
            .Sink = sink,
        };

        loom::RealizeResult realized = loom::Graph::Realize(std::move(*compiled), realizeInputs);

        if (!realized.has_value())
        {
            std::println(stderr, "Realize refused the graph: {}", loom::ToString(realized.error()));
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
        info.Size = k_WordBytes;
        info.Name = name;
        return resources.CreateBuffer(info, std::span{ &initial, 1u });
    }

    uint32_t FloatBits(float value) noexcept
    {
        uint32_t bits = 0u;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    }

    // Counts the words of `name` that differ from `expected(index)`. Prints the first few.
    int32_t CheckWords(std::span<const std::byte> bytes, std::string_view name, size_t count,
                       const std::function<uint32_t(size_t)>& expected)
    {
        if (bytes.size() < count * sizeof(uint32_t))
        {
            std::println(stderr, "{}: the readback failed", name);
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
                std::println(stderr, "{} word {}: expected 0x{:08X}, got 0x{:08X}", name, index, want, word);
            }

            ++mismatches;
        }

        if (mismatches != 0)
        {
            std::println(stderr, "{}: {} of {} words differ from the CPU", name, mismatches, count);
            return 1;
        }

        std::println("{}: {} words match the CPU", name, count);
        return 0;
    }

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

    void CleanupRun(AppState& state)
    {
        state.Readbacks = {};
        state.Graph.reset();

        // the graph frees what it made; the test owns the imports
        for (GraphicsResource& resource : state.Imports)
        {
            if (resource && state.Resources)
            {
                [[maybe_unused]] const rhi::OperationFuture destroyReply = state.Resources->DestroyResource(resource);
            }

            resource = GraphicsResource{};
        }

        if (state.Resources)
        {
            state.Resources->Tick({});
        }
    }

    void FinishTest(AppState& state, bool passed, std::string_view summary)
    {
        CleanupRun(state);
        state.Phase = TestPhase::Finished;

        const std::string text{ summary };

        if (passed)
        {
            std::println("{}", text);
        }
        else
        {
            std::println(stderr, "{}", text);
        }

        EM_ASM(
            {
                var summaryText = UTF8ToString($1);
                var statusElement = document['getElementById']('status');

                if (statusElement)
                {
                    statusElement['textContent'] = summaryText;
                }

                window['__ddTestDone'] = true;
                window['__ddTestPassed'] = Boolean($0);
                window['__ddTestSummary'] = summaryText;
            },
            passed ? 1 : 0,
            text.c_str());

        emscripten_cancel_main_loop();
    }

    // Copies each checked buffer to a staging buffer, behind the graph's submits on the same queue, and maps it.
    bool StartReadback(AppState& state)
    {
        const std::array<GraphicsResource, ReadbackCount> sources
        {
            state.Graph->BufferOf(k_Sampled),
            state.Graph->BufferOf(k_Counter),
            state.Graph->BufferOf(k_CountOut),
            state.Graph->BufferOf(k_Changed),
            state.Graph->BufferOf(k_Triggered),
        };

        const wgpu::CommandEncoder encoder = state.View.Device.CreateCommandEncoder();

        for (int32_t index = 0; index < ReadbackCount; ++index)
        {
            const wgpu::Buffer source = rhi::UnwrapWebGpuBuffer(sources[index]);
            ReadbackSlot& slot = state.Readbacks[index];
            slot = ReadbackSlot{ .Bytes = index == SampledReadback ? k_Float4Bytes : k_WordBytes };

            wgpu::BufferDescriptor stagingInfo{};
            stagingInfo.size = slot.Bytes;
            stagingInfo.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
            stagingInfo.label = "LoomImage.Staging";
            slot.Buffer = state.View.Device.CreateBuffer(&stagingInfo);

            if (!source || !slot.Buffer)
            {
                std::println(stderr, "readback {}: a buffer is null", index);
                return false;
            }

            encoder.CopyBufferToBuffer(source, 0u, slot.Buffer, 0u, slot.Bytes);
        }

        const wgpu::CommandBuffer commands = encoder.Finish();
        state.View.Queue.Submit(1u, &commands);

        for (ReadbackSlot& slot : state.Readbacks)
        {
            ReadbackSlot* const target = &slot;

            const auto onMapped = [target](wgpu::MapAsyncStatus status, wgpu::StringView)
            {
                target->Failed = status != wgpu::MapAsyncStatus::Success;
                target->Done = true;
            };

            slot.Buffer.MapAsync(wgpu::MapMode::Read, 0u, slot.Bytes, wgpu::CallbackMode::AllowSpontaneous, onMapped);
        }

        return true;
    }

    std::span<const std::byte> MappedBytes(const ReadbackSlot& slot)
    {
        const void* const mapped = slot.Buffer.GetConstMappedRange(0u, slot.Bytes);

        if (mapped == nullptr)
        {
            return {};
        }

        return std::span<const std::byte>{ static_cast<const std::byte*>(mapped), static_cast<size_t>(slot.Bytes) };
    }

    int32_t CheckResults(const AppState& state)
    {
        const RunCounts& runs = state.Runs;
        int32_t failures = 0;

        const auto sampled = [](size_t index)
        {
            const size_t texel = index / 4u;
            const int32_t x = static_cast<int32_t>(texel % k_Size);
            const int32_t y = static_cast<int32_t>(texel / k_Size);
            const int32_t component = static_cast<int32_t>(index % 4u);
            return FloatBits(PaintedValue(x, y, component) + LutValue(x, y, component));
        };

        const auto counted = [](uint32_t run_count)
        {
            return [run_count](size_t)
            {
                return run_count;
            };
        };

        const auto& slots = state.Readbacks;
        failures += CheckWords(MappedBytes(slots[SampledReadback]), "Sampled", size_t{ k_Texels } * 4u, sampled);
        failures += CheckWords(MappedBytes(slots[CounterReadback]), "Counter (history buffer)", k_Texels,
                               counted(static_cast<uint32_t>(runs[Accumulate])));
        failures += CheckWords(MappedBytes(slots[CountOutReadback]), "CountImage (history image)", k_Texels,
                               counted(static_cast<uint32_t>(runs[AccumulateImage])));
        failures += CheckWords(MappedBytes(slots[ChangedReadback]), "Changed", k_Texels, counted(3u));
        failures += CheckWords(MappedBytes(slots[TriggeredReadback]), "Triggered", k_Texels, counted(2u));

        // every frame-driven pass ran once the resources existed; the policy passes ran exactly twice
        const bool counts = runs[ChangedPass] == 2 && runs[TriggeredPass] == 2 && runs[Accumulate] > 0 &&
                            runs[AccumulateImage] == runs[Accumulate];

        if (!counts)
        {
            std::println(stderr, "run counts: Accumulate {}, AccumulateImage {}, Changed {} (want 2), Triggered {} (want 2)",
                         runs[Accumulate], runs[AccumulateImage], runs[ChangedPass], runs[TriggeredPass]);
            ++failures;
        }

        std::println("runs: Paint {}, Sample {}, Accumulate {}, AccumulateImage {}, CopyCount {}, Changed {}, Triggered {}",
                     runs[Paint], runs[Sample], runs[Accumulate], runs[AccumulateImage], runs[CopyCount],
                     runs[ChangedPass], runs[TriggeredPass]);
        return failures;
    }

    void StepBooting(AppState& state)
    {
        state.DeviceStage.Step();

        if (state.DeviceStage.HasFailed())
        {
            FinishTest(state, false, std::format("WebGpuLoomImageTest: device boot failed ({}): {}",
                                                 static_cast<uint32_t>(state.DeviceStage.FailureCode()),
                                                 state.DeviceStage.FailureMessage()));
            return;
        }

        if (!state.DeviceStage.IsReady())
        {
            return;
        }

        state.View = rhi::UnwrapWebGpuRhiContext(state.DeviceStage.Context());

        if (!state.View.Instance || !state.View.Device || !state.View.Queue)
        {
            FinishTest(state, false, "WebGpuLoomImageTest: WebGPU context handles are missing");
            return;
        }

        state.Resources = std::make_unique<ResourceContext>();
        const ResourceContextCreateInfo resourceInfo{ .Context = &state.DeviceStage.Context() };

        if (state.Resources->Initialize(resourceInfo) != ResourceContextError::Success)
        {
            FinishTest(state, false, "WebGpuLoomImageTest: ResourceContext::Initialize failed");
            return;
        }

        rhi::ShaderBundleResult<rhi::ShaderBundle> bundleResult = rhi::ShaderBundle::Open("/ShaderLibrary.ldmanifest", "wgsl");

        if (!bundleResult.has_value())
        {
            FinishTest(state, false, std::format("WebGpuLoomImageTest: ShaderBundle::Open failed: {}",
                                                 rhi::ToString(bundleResult.error())));
            return;
        }

        state.Bundle = std::move(*bundleResult);
        state.Phase = TestPhase::Start;
    }

    void StepStart(AppState& state)
    {
        state.Sink = std::make_unique<loom::CollectingSink>();
        state.Graph = BuildAndRealize(state, *state.Sink);

        if (!state.Graph)
        {
            FinishTest(state, false, "WebGpuLoomImageTest: BuildAndRealize failed");
            return;
        }

        loom::Graph& graph = *state.Graph;
        ResourceContext& resources = *state.Resources;
        graph.Supply(k_Lut, CreateLut(resources), ImageUsageBits::ComputeShaderSRV);
        graph.Supply(k_Changed, CreateZeroBuffer(resources, "LoomImage.Changed"));
        graph.Supply(k_Triggered, CreateZeroBuffer(resources, "LoomImage.Triggered"));
        graph.Set(k_PaintParams, PaintParams{ .seed = k_Seed });
        graph.Set(k_SampleParams, SampleParams{ .width = static_cast<uint32_t>(k_Size) });
        graph.Set(k_AccumulateParams, AccumulateParams{ .count = k_Texels });
        graph.Set(k_CopyCountParams, CopyCountParams{ .width = static_cast<uint32_t>(k_Size) });
        graph.Set(k_TriggeredParams, TriggeredParams{ .count = k_Texels });

        state.Runs = {};
        state.SubmittedTicks = 0;
        state.WaitTicks = 0;
        state.Phase = TestPhase::Ticking;
    }

    void StepTicking(AppState& state)
    {
        loom::Graph& graph = *state.Graph;
        state.Resources->Tick({});

        // the run counts must be whole frames: no Tick until every row and every pipeline exists
        if (state.SubmittedTicks == 0 && !graph.Ready())
        {
            ++state.WaitTicks;

            if (state.WaitTicks > k_MaxWaitTicks || state.Sink->ErrorCount() != 0)
            {
                PrintDiagnostics(*state.Sink);
                FinishTest(state, false, "WebGpuLoomImageTest: the graph did not become ready");
            }

            return;
        }

        SetInputs(graph, state.SubmittedTicks);

        const loom::TickResult result = graph.Tick(std::chrono::duration<double>{ 1.0 / 60.0 });

        switch (result.Status)
        {
        case loom::TickStatus::Submitted:
            for (int32_t pass = 0; pass < PassCount; ++pass)
            {
                state.Runs[pass] += graph.LastPlan().Decisions[pass].Reason == loom::SkipReason::Ran ? 1 : 0;
            }

            ++state.SubmittedTicks;
            state.WaitTicks = 0;
            break;
        case loom::TickStatus::NoFrameSlot:
            [[fallthrough]];
        case loom::TickStatus::NothingToRun:
            ++state.WaitTicks;
            break;
        case loom::TickStatus::Invalid:
            [[fallthrough]];
        case loom::TickStatus::TargetNotReady:
            [[fallthrough]];
        case loom::TickStatus::Failed:
            std::println(stderr, "{}", graph.ExplainLastTick());
            FinishTest(state, false, "WebGpuLoomImageTest: Tick failed");
            return;
        }

        if (state.WaitTicks > k_MaxWaitTicks)
        {
            std::println(stderr, "{}", graph.ExplainLastTick());
            FinishTest(state, false, "WebGpuLoomImageTest: Tick timed out");
            return;
        }

        if (state.SubmittedTicks < k_Ticks)
        {
            return;
        }

        // the imports are the test's: keep them for the cleanup
        state.Imports = { graph.ImageOf(k_Lut), graph.BufferOf(k_Changed), graph.BufferOf(k_Triggered) };

        if (!StartReadback(state))
        {
            FinishTest(state, false, "WebGpuLoomImageTest: StartReadback failed");
            return;
        }

        state.WaitTicks = 0;
        state.Phase = TestPhase::MappingReadback;
    }

    void StepMappingReadback(AppState& state)
    {
        state.View.Instance.ProcessEvents();

        const auto isDone = [](const ReadbackSlot& slot)
        {
            return slot.Done;
        };

        if (!std::ranges::all_of(state.Readbacks, isDone))
        {
            ++state.WaitTicks;

            if (state.WaitTicks > k_MaxWaitTicks)
            {
                FinishTest(state, false, "WebGpuLoomImageTest: MapAsync timed out");
            }

            return;
        }

        const auto hasFailed = [](const ReadbackSlot& slot)
        {
            return slot.Failed;
        };

        if (std::ranges::any_of(state.Readbacks, hasFailed))
        {
            FinishTest(state, false, "WebGpuLoomImageTest: MapAsync failed");
            return;
        }

        int32_t failures = CheckResults(state);

        for (ReadbackSlot& slot : state.Readbacks)
        {
            slot.Buffer.Unmap();
        }

        if (state.Sink->ErrorCount() != 0)
        {
            PrintDiagnostics(*state.Sink);
            ++failures;
        }

        if (failures != 0)
        {
            FinishTest(state, false, std::format("WebGpuLoomImageTest: {} failure(s)", failures));
            return;
        }

        FinishTest(state, true, "WebGpuLoomImageTest: images, history rings and run policies match the CPU");
    }

    void MainLoopTick(void* user_data)
    {
        AppState& state = *static_cast<AppState*>(user_data);

        switch (state.Phase)
        {
        case TestPhase::Booting:
            StepBooting(state);
            break;
        case TestPhase::Start:
            StepStart(state);
            break;
        case TestPhase::Ticking:
            StepTicking(state);
            break;
        case TestPhase::MappingReadback:
            StepMappingReadback(state);
            break;
        case TestPhase::Invalid:
            [[fallthrough]];
        case TestPhase::Finished:
            break;
        }
    }

}

int main()
{
    EM_ASM(
        window['__ddTestDone'] = false;
        window['__ddTestPassed'] = false;
        window['__ddTestSummary'] = "";
    );

    // the main loop never returns (simulate_infinite_loop), so the state lives for the whole run
    static AppState state{};
    bootstrap::DeviceConfig deviceConfig{};
    deviceConfig.Validation = rhi::ValidationLayers::BaseOnly;
    deviceConfig.ApplicationName = "WebGpuLoomImageTest";

    state.DeviceStage.StartHeadless(deviceConfig);
    emscripten_set_main_loop_arg(&MainLoopTick, &state, 0, 1);
    return 0;
}
