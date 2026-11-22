#include "Compile.hpp"
#include "CompiledGraph.hpp"
#include "Declarations.hpp"
#include "DeviceConfig.hpp"
#include "FramePlan.hpp"
#include "GraphBuilder.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomGraph.hpp"
#include "LoomKeys.hpp"
#include "PassBuilders.hpp"
#include "ResourceContext.hpp"
#include "ResourceContextWebGpu.hpp"
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

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

namespace host = webgpu_loom_compute_test_shaders;
using FillParams = host::LoomHashFill::HashFillCS::Params<host::CookProfile::Wgsl>;
using AgainParams = host::LoomHashFill::HashAgainCS::Params<host::CookProfile::Wgsl>;

constexpr uint32_t k_ElementCount = 4099u;
constexpr size_t k_Bytes = sizeof(uint32_t) * k_ElementCount;
constexpr uint32_t k_Seed = 0x9E3779B9u;
constexpr uint32_t k_Sentinel = 0xDEADBEEFu;
constexpr int32_t k_FrameSlots = 2;
constexpr int32_t k_Ticks = 3;
constexpr int32_t k_MaxWaitTicks = 6000;

constexpr loom::ResourceKey<loom::Buffer> k_Output{ "LoomCompute.Output" };
constexpr loom::ResourceKey<loom::Buffer> k_Output2{ "LoomCompute.Output2" };
constexpr loom::PassKey k_Fill{ "LoomCompute.Fill" };
constexpr loom::PassKey k_Again{ "LoomCompute.Again" };
constexpr loom::ParamsKey<FillParams> k_FillParams{ "LoomCompute.FillParams" };
constexpr loom::ParamsKey<AgainParams> k_AgainParams{ "LoomCompute.AgainParams" };

enum class TestPhase : uint8_t
{
    Invalid = 0,
    Booting = 1,
    StartVariant = 2,
    TickingVariant = 3,
    MappingReadback = 4,
    StartShortImport = 5,
    TickingShortImport = 6,
    Finished = 7,
};

struct ReadbackSlot
{
    wgpu::Buffer Buffer;
    bool Done{ false };
    bool Failed{ false };
};

struct AppState
{
    TestPhase Phase{ TestPhase::Booting };
    bootstrap::WebGpuDeviceStage DeviceStage;
    rhi::WebGpuRhiContextView View{};
    std::unique_ptr<ResourceContext> Resources;
    std::optional<rhi::ShaderBundle> Bundle;
    std::unique_ptr<loom::CollectingSink> Sink;
    std::unique_ptr<loom::Graph> Graph;
    GraphicsResource ImportedOutput;
    ReadbackSlot StagingOutput{};
    ReadbackSlot StagingOutput2{};
    int32_t VariantIndex{ 0 };
    int32_t SubmittedTicks{ 0 };
    int32_t TicksAfterReport{ 0 };
    int32_t WaitTicks{ 0 };
};

[[nodiscard]] uint32_t PcgHash(uint32_t value) noexcept;
[[nodiscard]] uint32_t ExpectedOutput(uint32_t index, bool hash_twice) noexcept;
[[nodiscard]] uint32_t ExpectedOutput2(uint32_t index, bool hash_twice) noexcept;
void BuildGraph(loom::GraphBuilder& graph, bool hash_twice);
void PrintDiagnostics(const loom::CollectingSink& sink, std::string_view variant_name);
[[nodiscard]] rhi::ResourceFuture<GraphicsResource> CreateOutput(ResourceContext& resources, uint64_t byte_count);
[[nodiscard]] std::unique_ptr<loom::Graph> BuildAndRealize(const AppState& state, loom::CollectingSink& sink,
                                                           bool hash_twice, std::string_view variant_name);
void FinishTest(AppState& state, bool passed, std::string_view summary);
void CleanupCurrentRun(AppState& state);
[[nodiscard]] bool StartReadback(AppState& state, std::string_view variant_name);
[[nodiscard]] int32_t VerifyStagingBuffer(const wgpu::Buffer& buffer, std::string_view name,
                                          std::string_view variant_name, uint32_t (*expected)(uint32_t, bool),
                                          bool hash_twice);
void StepBooting(AppState& state);
void StepStartVariant(AppState& state);
void StepTickingVariant(AppState& state);
void StepMappingReadback(AppState& state);
void StepStartShortImport(AppState& state);
void StepTickingShortImport(AppState& state);
void MainLoopTick(void* user_data);

} // namespace

int main()
{
    EM_ASM(
        window['__ddTestDone'] = false;
        window['__ddTestPassed'] = false;
        window['__ddTestSummary'] = "";
    );

    AppState state{};
    bootstrap::DeviceConfig deviceConfig{};
    deviceConfig.Validation = rhi::ValidationLayers::BaseOnly;
    deviceConfig.ApplicationName = "WebGpuLoomComputeTest";

    state.DeviceStage.StartHeadless(deviceConfig);
    emscripten_set_main_loop_arg(&MainLoopTick, &state, 0, 1);
    return 0;
}

namespace
{

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
        .NoHistoryAction = loom::OnNoHistory::Invalid,
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
        std::println(stderr,
                     "[{}] {} {}: {} (pass '{}', resource '{}')",
                     variant_name,
                     loom::ToString(diagnostic.Code),
                     loom::ToString(diagnostic.Severity),
                     loom::SummaryOf(diagnostic.Code),
                     diagnostic.Pass.Name,
                     diagnostic.Resource.Name);
    }
}

rhi::ResourceFuture<GraphicsResource> CreateOutput(ResourceContext& resources, uint64_t byte_count)
{
    const std::vector<uint32_t> sentinel(byte_count / sizeof(uint32_t), k_Sentinel);
    const RhiBufferResourceData initial{ std::as_bytes(std::span{ sentinel }), 0u };

    BufferCreateInfo info{};
    info.Domain = ResourceDomain::GPUOnly;
    info.Usage = BufferUsageBits::ComputeShaderUAV | BufferUsageBits::TransferSource;
    info.Size = byte_count;
    info.Name = "LoomCompute.Output";
    return resources.CreateBuffer(info, std::span{ &initial, 1u });
}

std::unique_ptr<loom::Graph> BuildAndRealize(const AppState& state, loom::CollectingSink& sink,
                                             bool hash_twice, std::string_view variant_name)
{
    const rhi::RhiContext& context = state.DeviceStage.Context();
    loom::GraphBuilder builder{ "LoomCompute.Graph", "LoomCompute.Shaders" };
    BuildGraph(builder, hash_twice);

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
        std::println(stderr, "[{}] Compile failed", variant_name);
        PrintDiagnostics(sink, variant_name);
        return nullptr;
    }

    const loom::RealizeInputs realizeInputs
    {
        .Resources = *state.Resources,
        .Context = context,
        .FrameSlots = k_FrameSlots,
        .Timestamps = loom::PassTimestamps::On,
        .Sink = sink,
    };

    loom::RealizeResult realized = loom::Graph::Realize(std::move(*compiled), realizeInputs);

    if (!realized.has_value())
    {
        std::println(stderr, "[{}] Realize refused the graph: {}", variant_name, loom::ToString(realized.error()));
        return nullptr;
    }

    return std::move(*realized);
}

void FinishTest(AppState& state, bool passed, std::string_view summary)
{
    CleanupCurrentRun(state);
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

void CleanupCurrentRun(AppState& state)
{
    state.StagingOutput = ReadbackSlot{};
    state.StagingOutput2 = ReadbackSlot{};
    state.Graph.reset();

    if (state.ImportedOutput && state.Resources)
    {
        [[maybe_unused]] const rhi::OperationFuture destroyReply = state.Resources->DestroyResource(state.ImportedOutput);
        state.Resources->Tick({});
        state.ImportedOutput = GraphicsResource{};
    }
}

bool StartReadback(AppState& state, std::string_view variant_name)
{
    const GraphicsResource output = state.Graph->BufferOf(k_Output);
    const GraphicsResource output2 = state.Graph->BufferOf(k_Output2);
    state.ImportedOutput = output;

    const wgpu::Buffer wgpuOutput = rhi::UnwrapWebGpuBuffer(output);
    const wgpu::Buffer wgpuOutput2 = rhi::UnwrapWebGpuBuffer(output2);

    if (!wgpuOutput || !wgpuOutput2)
    {
        std::println(stderr, "[{}] graph output WebGPU buffer is null", variant_name);
        return false;
    }

    wgpu::BufferDescriptor stagingDesc{};
    stagingDesc.size = k_Bytes;
    stagingDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
    stagingDesc.label = "LoomCompute.StagingOutput";

    state.StagingOutput = ReadbackSlot{};
    state.StagingOutput.Buffer = state.View.Device.CreateBuffer(&stagingDesc);
    stagingDesc.label = "LoomCompute.StagingOutput2";
    state.StagingOutput2 = ReadbackSlot{};
    state.StagingOutput2.Buffer = state.View.Device.CreateBuffer(&stagingDesc);

    if (!state.StagingOutput.Buffer || !state.StagingOutput2.Buffer)
    {
        std::println(stderr, "[{}] CreateBuffer for readback staging failed", variant_name);
        return false;
    }

    const wgpu::CommandEncoder encoder = state.View.Device.CreateCommandEncoder();
    encoder.CopyBufferToBuffer(wgpuOutput, 0u, state.StagingOutput.Buffer, 0u, k_Bytes);
    encoder.CopyBufferToBuffer(wgpuOutput2, 0u, state.StagingOutput2.Buffer, 0u, k_Bytes);

    const wgpu::CommandBuffer commands = encoder.Finish();
    state.View.Queue.Submit(1u, &commands);

    ReadbackSlot* const outputSlot = &state.StagingOutput;
    ReadbackSlot* const output2Slot = &state.StagingOutput2;

    state.StagingOutput.Buffer.MapAsync(
        wgpu::MapMode::Read,
        0u,
        k_Bytes,
        wgpu::CallbackMode::AllowSpontaneous,
        [outputSlot](wgpu::MapAsyncStatus status, wgpu::StringView)
        {
            outputSlot->Failed = (status != wgpu::MapAsyncStatus::Success);
            outputSlot->Done = true;
        });

    state.StagingOutput2.Buffer.MapAsync(
        wgpu::MapMode::Read,
        0u,
        k_Bytes,
        wgpu::CallbackMode::AllowSpontaneous,
        [output2Slot](wgpu::MapAsyncStatus status, wgpu::StringView)
        {
            output2Slot->Failed = (status != wgpu::MapAsyncStatus::Success);
            output2Slot->Done = true;
        });

    return true;
}

int32_t VerifyStagingBuffer(const wgpu::Buffer& buffer, std::string_view name, std::string_view variant_name,
                              uint32_t (*expected)(uint32_t, bool), bool hash_twice)
{
    const void* const mapped = buffer.GetConstMappedRange(0u, k_Bytes);

    if (!mapped)
    {
        std::println(stderr, "[{}] {} GetConstMappedRange returned null", variant_name, name);
        return 1;
    }

    const std::span<const uint32_t> words{ static_cast<const uint32_t*>(mapped), k_ElementCount };
    int32_t mismatches = 0;

    for (int32_t index = 0; index < static_cast<int32_t>(k_ElementCount); ++index)
    {
        const uint32_t want = expected(static_cast<uint32_t>(index), hash_twice);

        if (words[static_cast<size_t>(index)] != want)
        {
            if (mismatches < 8)
            {
                std::println(stderr,
                             "[{}] {} word {}: expected 0x{:08X}, got 0x{:08X}",
                             variant_name,
                             name,
                             index,
                             want,
                             words[static_cast<size_t>(index)]);
            }

            ++mismatches;
        }
    }

    if (mismatches != 0)
    {
        std::println(stderr, "[{}] {}: {} of {} words differ from the CPU", variant_name, name, mismatches, k_ElementCount);
        return 1;
    }

    std::println("[{}] {}: {} words match the CPU", variant_name, name, k_ElementCount);
    return 0;
}

void StepBooting(AppState& state)
{
    state.DeviceStage.Step();

    if (state.DeviceStage.HasFailed())
    {
        FinishTest(state,
                   false,
                   std::format("WebGpuLoomComputeTest: device boot failed ({}): {}",
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
        FinishTest(state, false, "WebGpuLoomComputeTest: WebGPU context handles are missing");
        return;
    }

    state.Resources = std::make_unique<ResourceContext>();
    const ResourceContextCreateInfo resourceInfo
    {
        .Context = &state.DeviceStage.Context(),
    };

    if (state.Resources->Initialize(resourceInfo) != ResourceContextError::Success)
    {
        FinishTest(state, false, "WebGpuLoomComputeTest: ResourceContext::Initialize failed");
        return;
    }

    rhi::ShaderBundleResult<rhi::ShaderBundle> bundleResult = rhi::ShaderBundle::Open("/ShaderLibrary.ldmanifest", "wgsl");

    if (!bundleResult.has_value())
    {
        FinishTest(state,
                   false,
                   std::format("WebGpuLoomComputeTest: ShaderBundle::Open failed: {}",
                               rhi::ToString(bundleResult.error())));
        return;
    }

    state.Bundle = std::move(*bundleResult);
    state.VariantIndex = 0;
    state.Phase = TestPhase::StartVariant;
}

void StepStartVariant(AppState& state)
{
    const bool hashTwice = (state.VariantIndex == 1);
    const std::string variantName = std::format("HASH_TWICE={}", hashTwice);

    state.Sink = std::make_unique<loom::CollectingSink>();
    state.Graph = BuildAndRealize(state, *state.Sink, hashTwice, variantName);

    if (!state.Graph)
    {
        FinishTest(state, false, std::format("WebGpuLoomComputeTest: [{}] BuildAndRealize failed", variantName));
        return;
    }

    rhi::ResourceFuture<GraphicsResource> outputFuture = CreateOutput(*state.Resources, k_Bytes);
    state.Graph->Supply(k_Output, std::move(outputFuture));
    state.Graph->Set(k_FillParams, FillParams{ .seed = k_Seed, .count = k_ElementCount });
    state.Graph->Set(k_AgainParams, AgainParams{ .count = k_ElementCount });

    state.SubmittedTicks = 0;
    state.WaitTicks = 0;
    state.Phase = TestPhase::TickingVariant;
}

void StepTickingVariant(AppState& state)
{
    const bool hashTwice = (state.VariantIndex == 1);
    const std::string variantName = std::format("HASH_TWICE={}", hashTwice);

    state.Resources->Tick({});
    const loom::TickResult result = state.Graph->Tick(std::chrono::duration<double>{ 1.0 / 60.0 });

    switch (result.Status)
    {
    case loom::TickStatus::Submitted:
        ++state.SubmittedTicks;
        state.WaitTicks = 0;
        break;
    case loom::TickStatus::NoFrameSlot:
    case loom::TickStatus::NothingToRun:
        ++state.WaitTicks;
        break;
    case loom::TickStatus::Invalid:
    case loom::TickStatus::TargetNotReady:
    case loom::TickStatus::Failed:
        FinishTest(state, false, std::format("WebGpuLoomComputeTest: [{}] Tick failed", variantName));
        return;
    }

    if (state.WaitTicks > k_MaxWaitTicks)
    {
        FinishTest(state, false, std::format("WebGpuLoomComputeTest: [{}] Tick timed out", variantName));
        return;
    }

    if (state.SubmittedTicks < k_Ticks)
    {
        return;
    }

    if (!StartReadback(state, variantName))
    {
        FinishTest(state, false, std::format("WebGpuLoomComputeTest: [{}] StartReadback failed", variantName));
        return;
    }

    state.WaitTicks = 0;
    state.Phase = TestPhase::MappingReadback;
}

void StepMappingReadback(AppState& state)
{
    state.View.Instance.ProcessEvents();

    if (!state.StagingOutput.Done || !state.StagingOutput2.Done)
    {
        ++state.WaitTicks;

        if (state.WaitTicks > k_MaxWaitTicks)
        {
            FinishTest(state, false, "WebGpuLoomComputeTest: MapAsync timed out");
        }

        return;
    }

    const bool hashTwice = (state.VariantIndex == 1);
    const std::string variantName = std::format("HASH_TWICE={}", hashTwice);

    if (state.StagingOutput.Failed || state.StagingOutput2.Failed)
    {
        FinishTest(state, false, std::format("WebGpuLoomComputeTest: [{}] MapAsync failed", variantName));
        return;
    }

    int32_t failures = 0;
    failures += VerifyStagingBuffer(state.StagingOutput.Buffer, "Output", variantName, &ExpectedOutput, hashTwice);
    failures += VerifyStagingBuffer(state.StagingOutput2.Buffer, "Output2", variantName, &ExpectedOutput2, hashTwice);

    state.StagingOutput.Buffer.Unmap();
    state.StagingOutput2.Buffer.Unmap();

    if (state.Sink->ErrorCount() != 0)
    {
        PrintDiagnostics(*state.Sink, variantName);
        ++failures;
    }

    CleanupCurrentRun(state);

    if (failures != 0)
    {
        FinishTest(state, false, std::format("WebGpuLoomComputeTest: [{}] verification failed ({} error(s))", variantName, failures));
        return;
    }

    if (state.VariantIndex == 0)
    {
        state.VariantIndex = 1;
        state.Phase = TestPhase::StartVariant;
        return;
    }

    state.Phase = TestPhase::StartShortImport;
}

void StepStartShortImport(AppState& state)
{
    constexpr std::string_view k_Name = "SHORT_IMPORT";
    constexpr uint64_t k_ShortBytes = k_Bytes - sizeof(uint32_t);

    state.Sink = std::make_unique<loom::CollectingSink>();
    state.Graph = BuildAndRealize(state, *state.Sink, true, k_Name);

    if (!state.Graph)
    {
        FinishTest(state, false, "WebGpuLoomComputeTest: [SHORT_IMPORT] BuildAndRealize failed");
        return;
    }

    rhi::ResourceFuture<GraphicsResource> shortOutputFuture = CreateOutput(*state.Resources, k_ShortBytes);
    state.Graph->Supply(k_Output, std::move(shortOutputFuture));
    state.Graph->Set(k_FillParams, FillParams{ .seed = k_Seed, .count = k_ElementCount });
    state.Graph->Set(k_AgainParams, AgainParams{ .count = k_ElementCount });

    state.TicksAfterReport = 0;
    state.WaitTicks = 0;
    state.Phase = TestPhase::TickingShortImport;
}

void StepTickingShortImport(AppState& state)
{
    constexpr std::string_view k_Name = "SHORT_IMPORT";
    constexpr uint64_t k_ShortBytes = k_Bytes - sizeof(uint32_t);

    state.Resources->Tick({});
    const loom::TickResult result = state.Graph->Tick(std::chrono::duration<double>{ 1.0 / 60.0 });

    if (result.Status == loom::TickStatus::Submitted)
    {
        FinishTest(state, false, "WebGpuLoomComputeTest: [SHORT_IMPORT] Tick submitted work against the short import");
        return;
    }

    if (result.Status == loom::TickStatus::Invalid ||
        result.Status == loom::TickStatus::TargetNotReady ||
        result.Status == loom::TickStatus::Failed)
    {
        FinishTest(state, false, "WebGpuLoomComputeTest: [SHORT_IMPORT] Tick returned an error status");
        return;
    }

    if (state.Sink->ErrorCount() != 0)
    {
        ++state.TicksAfterReport;
        state.WaitTicks = 0;
    }
    else
    {
        ++state.WaitTicks;
    }

    if (state.WaitTicks > k_MaxWaitTicks)
    {
        FinishTest(state, false, "WebGpuLoomComputeTest: [SHORT_IMPORT] timed out waiting for LM701");
        return;
    }

    if (state.TicksAfterReport < k_Ticks)
    {
        return;
    }

    state.ImportedOutput = state.Graph->BufferOf(k_Output);
    const std::span<const loom::Diagnostic> diagnostics = state.Sink->Diagnostics();
    const bool one = diagnostics.size() == 1u && diagnostics.front().Code == loom::DiagnosticCode::LM701;

    if (!one)
    {
        PrintDiagnostics(*state.Sink, k_Name);
        FinishTest(state,
                   false,
                   std::format("WebGpuLoomComputeTest: [SHORT_IMPORT] expected 1 LM701, got {} diagnostic(s)",
                               diagnostics.size()));
        return;
    }

    const loom::Diagnostic& lm701 = diagnostics.front();
    const bool sizes = lm701.Expected.Integer == static_cast<int64_t>(k_Bytes) &&
                       lm701.Actual.Integer == static_cast<int64_t>(k_ShortBytes);

    if (!sizes)
    {
        FinishTest(state,
                   false,
                   std::format("WebGpuLoomComputeTest: [SHORT_IMPORT] LM701 sizes: expected {} and {}, got {} and {}",
                               k_Bytes,
                               k_ShortBytes,
                               lm701.Expected.Integer,
                               lm701.Actual.Integer));
        return;
    }

    std::println("[{}] LM701 reported once: promised {} bytes, supplied {}", k_Name, k_Bytes, k_ShortBytes);
    FinishTest(state,
               true,
               "WebGpuLoomComputeTest: both variants match the CPU reference, and LM701 caught the short import");
}

void MainLoopTick(void* user_data)
{
    AppState& state = *static_cast<AppState*>(user_data);

    switch (state.Phase)
    {
    case TestPhase::Booting:
        StepBooting(state);
        break;
    case TestPhase::StartVariant:
        StepStartVariant(state);
        break;
    case TestPhase::TickingVariant:
        StepTickingVariant(state);
        break;
    case TestPhase::MappingReadback:
        StepMappingReadback(state);
        break;
    case TestPhase::StartShortImport:
        StepStartShortImport(state);
        break;
    case TestPhase::TickingShortImport:
        StepTickingShortImport(state);
        break;
    case TestPhase::Invalid:
    case TestPhase::Finished:
        break;
    }
}

} // namespace
