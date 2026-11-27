#include "Tick.hpp"
#include "Declarations.hpp"
#include "FramePlan.hpp"
#include "GraphImpl.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "LoomFormats.hpp"
#include "LoomGraph.hpp"
#include "Records.hpp"
#include "ResourceContextWebGpu.hpp"
#include "ResourceFuture.hpp"
#include "ResourceTypes.hpp"
#include "RhiResourceFlags.hpp"
#include "ShaderLibraryTypes.hpp"
#include "WebGpuMapping.hpp"
#include "bind/BoundGraph.hpp"
#include "compile/CompiledData.hpp"
#include "freeze/FrozenGraph.hpp"
#include "layout/LaidOutGraph.hpp"
#include "size/SizedGraph.hpp"
#include "math/MathStorage.hpp"
#include <webgpu/webgpu_cpp.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

// The backend's steps of one Tick (spec 5.1): poll, upload the params, build the bind groups that changed, record and
// submit. Graph::Tick (LoomGraph.cpp) runs them around the core's decisions. Every wait is a poll.
namespace loom
{

namespace
{
    // Q4: a Created future whose transfer is not submitted yet has nothing a submit may wait on, so it is Pending.
    ResourceReadiness ReadinessOf(const rhi::ResourceFuture<GraphicsResource>& future);
    // A history ring is as ready as its least ready copy: Failed, then Pending, then Created, then Complete.
    ResourceReadiness ReadinessOfRow(const GraphImpl& graph, int32_t row);
    void ReportRow(GraphImpl& graph, DiagnosticCode code, int32_t row);
    // LM705 for a pipeline the browser could not make, against the pass whose slots gave its layout
    void ReportPipeline(GraphImpl& graph, int32_t pipeline);

    // What LM701 reports: the first field of the supplied object that breaks the import's promise. An empty
    // FieldName means the object keeps the promise.
    struct ImportMismatch
    {
        std::string_view FieldName;
        DiagnosticValueKind Kind{ DiagnosticValueKind::None };
        std::string_view ExpectedText;
        std::string_view ActualText;
        int64_t Expected{ 0 };
        int64_t Actual{ 0 };
    };

    ImportMismatch KindMismatchOf(std::string_view expected);
    ImportMismatch IntegerMismatch(std::string_view field_name, int64_t expected, int64_t actual);
    // A buffer: at least ByteSize bytes. An unknown size promises nothing.
    ImportMismatch CheckBufferImport(const CompiledData& data, int32_t row, const ResourceDescription& description);
    // An image: its format and layer count; for an Exact expectation, its extent and mip count too.
    ImportMismatch CheckImageImport(const CompiledData& data, int32_t row, const ResourceDescription& description);
    ImportMismatch CheckImport(const CompiledData& data, int32_t row, const ResourceDescription& description);
    void ReportMismatch(GraphImpl& graph, int32_t row, const ImportMismatch& mismatch);

    // True when the group was never built, or one of its slots' rows has a newer epoch than the build saw.
    bool GroupIsStale(const GraphImpl& graph, int32_t pass_index, int32_t phase, int32_t group);
    void BuildGroup(GraphImpl& graph, int32_t pass_index, int32_t phase, int32_t group);
    // DispatchThreadsOver an image whose extent was unknown at Compile (an AnyExtent import): the threads cover its
    // width, height, and depth or layers, as Layout counts a known extent.
    math::UInt3 GroupsOverImage(const GraphImpl& graph, int32_t row, const lodestone::WorkgroupSize& workgroup);
    // Zeroes each buffer copy in plan.Clears with the encoder. Image copies are cleared by ClearImages.
    void RecordBufferClears(const GraphImpl& graph, const wgpu::CommandEncoder& encoder);
    // Zeroes every mip and layer of each image copy in plan.Clears, with a queue write that precedes the submit.
    void ClearImages(GraphImpl& graph);
    void RecordDispatch(GraphImpl& graph,
                        const wgpu::ComputePassEncoder& compute_pass,
                        int32_t pass_index,
                        int32_t& bound_pipeline);
    bool WritesUse(const PassUse& use);
    uint32_t MipExtent(int32_t base, uint32_t mip);
    int32_t PhaseOf(int32_t phase, int64_t frame_index) noexcept;

}

// Spec 5.1 step 2. LM701 and LM702 are reported once per epoch.
// The async results (round 2 Q3): a made pipeline moves into its runtime once, and a failed one is reported once.
void PollPipelines(GraphImpl& graph)
{
    if (graph.AllPipelinesSettled)
    {
        return;
    }

    PipelineResults& results = *graph.Results;
    bool settled = true;

    for (size_t pipeline = 0u; pipeline < results.States.size(); ++pipeline)
    {
        const PipelineReadiness state = results.States[pipeline];

        if (state == PipelineReadiness::Pending)
        {
            settled = false;
            continue;
        }

        if (graph.PipelineReported[pipeline] != 0u)
        {
            continue;
        }

        if (state == PipelineReadiness::Ready)
        {
            graph.Pipelines[pipeline].Pipeline = std::move(results.Made[pipeline]);
        }
        else if (state == PipelineReadiness::Failed)
        {
            ReportPipeline(graph, static_cast<int32_t>(pipeline));
        }

        graph.PipelineReported[pipeline] = 1u;
    }

    graph.AllPipelinesSettled = settled;
}

void PollRows(GraphImpl& graph)
{
    if (graph.AllRowsSettled)
    {
        return;
    }

    uint32_t settledCount = 0u;

    for (int32_t row = 0; std::cmp_less(row, graph.Rows.size()); ++row)
    {
        RowRuntime& runtime = graph.Rows[row];
        ResourceState& state = graph.States[row];

        if (state.Readiness == ResourceReadiness::Complete ||
            state.Readiness == ResourceReadiness::Failed)
        {
            ++settledCount;
            continue;
        }

        state.Readiness = ReadinessOfRow(graph, row);
        const bool exists = state.Readiness == ResourceReadiness::Created ||
                            state.Readiness == ResourceReadiness::Complete;

        // a supplied object must keep its import's promise
        if (exists && !runtime.ImportChecked && !runtime.Owned && !runtime.KindMismatch)
        {
            const ImportMismatch mismatch = CheckImport(graph.Compiled.Data(), row, runtime.Future.Description());
            runtime.ImportChecked = true;
            runtime.KindMismatch = !mismatch.FieldName.empty();

            if (runtime.KindMismatch)
            {
                ReportMismatch(graph, row, mismatch);
                runtime.Reported = state.Epoch;
            }
        }

        if (runtime.KindMismatch)
        {
            state.Readiness = ResourceReadiness::Failed;
        }

        if (state.Readiness == ResourceReadiness::Failed && runtime.Reported != state.Epoch)
        {
            ReportRow(graph, DiagnosticCode::LM702, row);
            runtime.Reported = state.Epoch;
        }

        if (state.Readiness == ResourceReadiness::Complete ||
            state.Readiness == ResourceReadiness::Failed)
        {
            ++settledCount;
        }
    }

    graph.AllRowsSettled = std::cmp_equal(settledCount, graph.Rows.size());
}

void RefreshSync(GraphImpl& graph)
{
    if (graph.SyncedGeneration == graph.SupplyGeneration)
    {
        return;
    }

    graph.SyncedGeneration = graph.SupplyGeneration;
    const CompiledData& data = graph.Compiled.Data();

    for (size_t copy = 0u; copy < graph.Sync.size(); ++copy)
    {
        const ResourceState& state = graph.States[data.CopyRows[copy]];

        if (graph.Sync[copy].Epoch != state.Epoch)
        {
            if (graph.Sync[copy].HasContents)
            {
                ++graph.EmptyCopyCount;
            }

            graph.Sync[copy] = ResourceSync{ .Layout = state.InitialLayout, .Epoch = state.Epoch };
        }
    }
}

void UploadParams(GraphImpl& graph)
{
    const CompiledData& data = graph.Compiled.Data();

    for (const int32_t passIndex : graph.Plan.Runs)
    {
        const int32_t params = data.Passes[passIndex].Params;
        const PassRuntime& pass = graph.Passes[passIndex];
        const CompiledPipeline& pipeline = data.Pipelines[data.LaidOut.Passes[passIndex].Pipeline];

        if (pipeline.ConstantsDelivery == lodestone::EntryPointConstantsDelivery::UniformBuffer && params >= 0)
        {
            // a write is a whole number of 4-byte words, and the block is padded to the slice's size
            const std::span<const std::byte> bytes = graph.Params.Bytes(params);
            const size_t sliceSize = static_cast<size_t>(pass.ParamsSize);
            std::fill_n(graph.ParamsScratch.begin(), sliceSize, std::byte{ 0 });
            std::memcpy(graph.ParamsScratch.data(), bytes.data(), std::min(bytes.size(), sliceSize));
            graph.Queue.WriteBuffer(graph.ParamsBuffer, pass.ParamsOffset, graph.ParamsScratch.data(), sliceSize);
        }
    }
}

void UpdateBindGroups(GraphImpl& graph)
{
    for (const int32_t passIndex : graph.Plan.Runs)
    {
        PassRuntime& pass = graph.Passes[passIndex];
        if (pass.GroupCount == 0)
        {
            continue;
        }

        const int32_t phase = PhaseOf(pass.Phases, graph.FrameIndex);

        if (pass.PhaseGeneration[phase] == graph.SupplyGeneration)
        {
            continue;
        }

        for (int32_t group = 0; group < pass.GroupCount; ++group)
        {
            if (GroupIsStale(graph, passIndex, phase, group))
            {
                BuildGroup(graph, passIndex, phase, group);
            }
        }

        pass.PhaseGeneration[phase] = graph.SupplyGeneration;
    }
}

bool SubmitFrame(GraphImpl& graph)
{
    const CompiledData& data = graph.Compiled.Data();
    ClearImages(graph);

    wgpu::CommandEncoder encoder = graph.Device.CreateCommandEncoder();

    if (!encoder)
    {
        return false;
    }

    // OnNoHistory::Clear: the clears come before every run
    RecordBufferClears(graph, encoder);

    // Every pass is a compute dispatch, so one compute pass holds them all: the runs are in level order, so level k is
    // recorded back-to-back before level k + 1, and Dawn orders the storage writes and reads between dispatches.
    wgpu::ComputePassEncoder computePass = encoder.BeginComputePass();

    if (!computePass)
    {
        return false;
    }

    int32_t boundPipeline = -1;
    for (const int32_t passIndex : graph.Plan.Runs)
    {
        RecordDispatch(graph, computePass, passIndex, boundPipeline);
    }

    computePass.End();
    const wgpu::CommandBuffer commandBuffer = encoder.Finish();

    if (!commandBuffer)
    {
        return false;
    }

    graph.Queue.Submit(1u, &commandBuffer);
    static_cast<void>(data);
    return true;
}

void MarkContents(GraphImpl& graph)
{
    if (graph.EmptyCopyCount == 0u)
    {
        return;
    }

    const CompiledData& data = graph.Compiled.Data();

    for (const int32_t copy : graph.Plan.Clears)
    {
        graph.Sync[copy].HasContents = true;
        --graph.EmptyCopyCount;
    }

    for (const int32_t passIndex : graph.Plan.Runs)
    {
        const CompiledPass& pass = data.Passes[passIndex];

        for (int32_t useIndex = pass.FirstUse; useIndex < pass.FirstUse + pass.UseCount; ++useIndex)
        {
            const PassUse& use = data.Uses[useIndex];

            if (WritesUse(use))
            {
                const int32_t copyIdx = CopyOf(data, use.Resource, graph.FrameIndex, use.Previous);
                if (!graph.Sync[copyIdx].HasContents)
                {
                    graph.Sync[copyIdx].HasContents = true;
                    ++graph.EmptyCopyCount;
                }
            }
        }
    }
}

namespace
{

    ResourceReadiness ReadinessOf(const rhi::ResourceFuture<GraphicsResource>& future)
    {
        auto handleCreated = [&future]()->ResourceReadiness
        {
            const rhi::GpuWait completion = future.Completion();
            const bool unsubmitted = completion.Value != 0u && !completion.Submitted;
            return unsubmitted ? ResourceReadiness::Pending : ResourceReadiness::Created;
        };

        switch(future.State())
        {
        case rhi::FutureState::Created:
            return handleCreated();
        case rhi::FutureState::Complete:
            return ResourceReadiness::Complete;
        case rhi::FutureState::Failed:
            return ResourceReadiness::Failed;
        case rhi::FutureState::Pending:
            [[fallthrough]];
        case rhi::FutureState::Invalid:
            break;
        }

        return ResourceReadiness::Pending;
    }

    ResourceReadiness ReadinessOfRow(const GraphImpl& graph, int32_t row)
    {
        const CompiledData& data = graph.Compiled.Data();
        ResourceReadiness readiness = ReadinessOf(graph.Rows[row].Future);

        for (int32_t extra = 1; extra < data.CopyCounts[row]; ++extra)
        {
            const ResourceReadiness copy = ReadinessOf(FutureOfCopy(graph, data.ExtraCopyBase[row] + extra - 1));
            const bool worse = copy == ResourceReadiness::Failed ||
                               (readiness != ResourceReadiness::Failed && static_cast<uint8_t>(copy) < static_cast<uint8_t>(readiness));

            if (worse)
            {
                readiness = copy;
            }
        }

        return readiness;
    }

    void ReportRow(GraphImpl& graph, DiagnosticCode code, int32_t row)
    {
        const FrozenResource& frozen = graph.Compiled.Data().Frozen.Resources[row];

        Diagnostic diag
        {
            .Code = code,
            .Severity = DefaultSeverityOf(code),
            .Where = frozen.Where,
            .Resource = frozen.Key,
        };

        graph.Sink.Report(diag);
    }

    void ReportPipeline(GraphImpl& graph, int32_t pipeline)
    {
        const CompiledData& data = graph.Compiled.Data();
        const FrozenPass& pass = data.Frozen.Passes[data.Pipelines[pipeline].LayoutPass];

        Diagnostic diag
        {
            .Code = DiagnosticCode::LM705,
            .Severity = DefaultSeverityOf(DiagnosticCode::LM705),
            .Where = pass.Where,
            .Pass = pass.Key,
            .Actual = DiagnosticValue
            {
                .FieldName = "Message",
                .Kind = DiagnosticValueKind::Text,
                .Text = graph.Results->Messages[pipeline],
                .Integer = 0,
                .Real = 0.0,
            },
        };

        graph.Sink.Report(diag);
    }

    ImportMismatch KindMismatchOf(std::string_view expected)
    {
        return ImportMismatch{ .FieldName = "Type",
                               .Kind = DiagnosticValueKind::Text,
                               .ExpectedText = expected,
                               .ActualText = "another kind" };
    }

    ImportMismatch IntegerMismatch(std::string_view field_name, int64_t expected, int64_t actual)
    {
        return ImportMismatch{ .FieldName = field_name,
                               .Kind = DiagnosticValueKind::Integer,
                               .Expected = expected,
                               .Actual = actual };
    }

    ImportMismatch CheckBufferImport(const CompiledData& data, int32_t row, const ResourceDescription& description)
    {
        if (description.Type != ResourceType::Buffer)
        {
            return KindMismatchOf("Buffer");
        }

        const int64_t promised = data.Sized.Resources[row].ByteSize;

        if (promised != k_UnknownByteSize && std::cmp_less(description.ByteSize, promised))
        {
            return IntegerMismatch("ByteSize", promised, static_cast<int64_t>(description.ByteSize));
        }

        return ImportMismatch{};
    }

    ImportMismatch CheckImageImport(const CompiledData& data, int32_t row, const ResourceDescription& description)
    {
        if (description.Type != ResourceType::Image)
        {
            return KindMismatchOf("Image");
        }

        const ImageExpectation& expected = data.Frozen.Imports[data.Frozen.Resources[row].DeclarationIndex].Expectation;
        const std::optional<lodestone::TextureFormat> format = ToTextureFormat(description.Format);

        if (format != expected.Format)
        {
            return ImportMismatch{ .FieldName = "Format", .Kind = DiagnosticValueKind::Text,
                                   .ExpectedText = TextureFormatName(expected.Format),
                                   .ActualText = format.has_value() ? TextureFormatName(*format) : "no Lodestone format" };
        }

        if (description.ArrayLayers != expected.Layers)
        {
            return IntegerMismatch("Layers", expected.Layers, description.ArrayLayers);
        }

        if (expected.Kind != ImageExpectation::Kind::Exact)
        {
            return ImportMismatch{};
        }

        const std::array<ImportMismatch, 4u> exact
        {
            IntegerMismatch("Width", expected.Width, description.Width),
            IntegerMismatch("Height", expected.Height, description.Height),
            IntegerMismatch("Depth", expected.Depth, description.Depth),
            IntegerMismatch("MipLevels", expected.MipLevels, description.MipLevels),
        };

        for (const ImportMismatch& axis : exact)
        {
            if (axis.Expected != axis.Actual)
            {
                return axis;
            }
        }

        return ImportMismatch{};
    }

    ImportMismatch CheckImport(const CompiledData& data, int32_t row, const ResourceDescription& description)
    {
        if (data.Frozen.Resources[row].Kind == ResourceKind::Image)
        {
            return CheckImageImport(data, row, description);
        }

        return CheckBufferImport(data, row, description);
    }

    void ReportMismatch(GraphImpl& graph, int32_t row, const ImportMismatch& mismatch)
    {
        const FrozenResource& frozen = graph.Compiled.Data().Frozen.Resources[row];

        Diagnostic diag
        {
            .Code = DiagnosticCode::LM701,
            .Severity = DefaultSeverityOf(DiagnosticCode::LM701),
            .Where = frozen.Where,
            .Resource = frozen.Key,
        };

        diag.Expected = DiagnosticValue{ .FieldName = mismatch.FieldName, .Kind = mismatch.Kind,
                                         .Text = mismatch.ExpectedText, .Integer = mismatch.Expected };
        diag.Actual = DiagnosticValue{ .FieldName = mismatch.FieldName, .Kind = mismatch.Kind,
                                       .Text = mismatch.ActualText, .Integer = mismatch.Actual };
        graph.Sink.Report(diag);
    }

    bool GroupIsStale(const GraphImpl& graph, int32_t pass_index, int32_t phase, int32_t group)
    {
        const CompiledData& data = graph.Compiled.Data();
        const BoundPass& bound = data.BoundPasses[pass_index];
        const PassRuntime& pass = graph.Passes[pass_index];

        if (!pass.BindGroups[(static_cast<size_t>(phase) * pass.GroupCount) + group])
        {
            return true;
        }

        for (int32_t slotOffset = 0; slotOffset < bound.SlotCount; ++slotOffset)
        {
            const BindingSlot& slot = data.Slots[bound.FirstSlot + slotOffset];

            if (std::cmp_equal(slot.Group, group) && slot.Resource.IsValid())
            {
                const uint32_t written = pass.WrittenEpochs[(static_cast<size_t>(phase) * bound.SlotCount) + slotOffset];

                if (written != graph.States[slot.Resource.Value].Epoch)
                {
                    return true;
                }
            }
        }

        return false;
    }

    void BuildGroup(GraphImpl& graph, int32_t pass_index, int32_t phase, int32_t group)
    {
        const CompiledData& data = graph.Compiled.Data();
        const BoundPass& bound = data.BoundPasses[pass_index];
        const CompiledPipeline& compiled = data.Pipelines[data.LaidOut.Passes[pass_index].Pipeline];
        const PipelineRuntime& pipeline = graph.Pipelines[data.LaidOut.Passes[pass_index].Pipeline];
        PassRuntime& pass = graph.Passes[pass_index];

        // the scratch was reserved for every slot and the constants block, so it does not grow
        graph.EntryScratch.clear();

        for (int32_t slotOffset = 0; slotOffset < bound.SlotCount; ++slotOffset)
        {
            const BindingSlot& slot = data.Slots[bound.FirstSlot + slotOffset];

            // the constants block has a slot with no row: its entry is below
            if (std::cmp_equal(slot.Group, group) && slot.Resource.IsValid())
            {
                const int32_t row = slot.Resource.Value;
                // a history ring's copy for this frame; a phase always meets the same copies
                const GraphicsResource& object = FutureOfCopy(graph, CopyOf(data, row, graph.FrameIndex, slot.Previous)).Get();

                wgpu::BindGroupEntry entry{};
                entry.binding = slot.Binding;

                if (slot.Kind == lodestone::BindingKind::Texture ||
                    slot.Kind == lodestone::BindingKind::StorageTexture)
                {
                    entry.textureView = rhi::UnwrapWebGpuTextureView(object);
                }
                else if (slot.Kind == lodestone::BindingKind::Sampler)
                {
                    entry.sampler = rhi::UnwrapWebGpuSampler(object);
                }
                else
                {
                    entry.buffer = rhi::UnwrapWebGpuBuffer(object);
                    entry.offset = 0u;
                    entry.size = wgpu::kWholeSize;
                }

                graph.EntryScratch.push_back(entry);
                pass.WrittenEpochs[(static_cast<size_t>(phase) * bound.SlotCount) + slotOffset] = graph.States[row].Epoch;
            }
        }

        const bool hasConstants = compiled.ConstantsDelivery == lodestone::EntryPointConstantsDelivery::UniformBuffer &&
                                  std::cmp_equal(compiled.ConstantsGroup, group);

        if (hasConstants)
        {
            wgpu::BindGroupEntry entry{};
            entry.binding = compiled.ConstantsBinding;
            entry.buffer = graph.ParamsBuffer;
            entry.offset = pass.ParamsOffset;
            entry.size = pass.ParamsSize;
            graph.EntryScratch.push_back(entry);
        }

        wgpu::BindGroupDescriptor descriptor{};
        descriptor.layout = pipeline.GroupLayouts[group];
        descriptor.entryCount = graph.EntryScratch.size();
        descriptor.entries = graph.EntryScratch.data();
        pass.BindGroups[(static_cast<size_t>(phase) * pass.GroupCount) + group] = graph.Device.CreateBindGroup(&descriptor);
    }

    math::UInt3 GroupsOverImage(const GraphImpl& graph, int32_t row, const lodestone::WorkgroupSize& workgroup)
    {
        using namespace math;
        constexpr static UInt3 k_OneGroup{ 1u, 1u, 1u };
        const ResourceDescription& description = graph.Rows[row].Future.Description();
        const bool volume = description.Dimensions == ImageType::Type3D;
        const uint32_t threadZ = volume ? static_cast<uint32_t>(description.Depth) :
                                          static_cast<uint32_t>(description.ArrayLayers);
        UInt3 threads
        {
            static_cast<uint32_t>(description.Width),
            static_cast<uint32_t>(description.Height),
            threadZ
        };
        // ensure that each thread dim is at least 1
        threads = threads.Max(k_OneGroup);

        UInt3 workGroups
        {
            workgroup.X,
            workgroup.Y,
            workgroup.Z
        };
        workGroups = workGroups.Max(k_OneGroup);

        return (threads + workGroups - k_OneGroup) / workGroups;
    }

    void RecordBufferClears(const GraphImpl& graph, const wgpu::CommandEncoder& encoder)
    {
        const CompiledData& data = graph.Compiled.Data();

        for (const int32_t copy : graph.Plan.Clears)
        {
            if (data.Frozen.Resources[data.CopyRows[copy]].Kind != ResourceKind::Image)
            {
                encoder.ClearBuffer(rhi::UnwrapWebGpuBuffer(FutureOfCopy(graph, copy).Get()), 0u, wgpu::kWholeSize);
            }
        }
    }

    void ClearImages(GraphImpl& graph)
    {
        const CompiledData& data = graph.Compiled.Data();

        for (const int32_t copy : graph.Plan.Clears)
        {
            if (data.Frozen.Resources[data.CopyRows[copy]].Kind == ResourceKind::Image)
            {
                const GraphicsResource& object = FutureOfCopy(graph, copy).Get();
                const ResourceDescription& description = FutureOfCopy(graph, copy).Description();
                const uint32_t texelBytes = TexelBytesOf(description.Format);

                if (texelBytes > 0u)
                {
                    const bool volume = description.Dimensions == ImageType::Type3D;
                    const uint32_t depthOrLayers = volume ? static_cast<uint32_t>(std::max(description.Depth, 1)) :
                                                            static_cast<uint32_t>(std::max(description.ArrayLayers, 1));

                    for (uint32_t mip = 0u; mip < static_cast<uint32_t>(description.MipLevels); ++mip)
                    {
                        const uint32_t width = MipExtent(description.Width, mip);
                        const uint32_t height = MipExtent(description.Height, mip);
                        const uint32_t depth = volume ? MipExtent(description.Depth, mip) : depthOrLayers;
                        const size_t bytes = static_cast<size_t>(width) * texelBytes * height * depth;

                        // zeroes are shared by every clear: the scratch only grows, and the first clear is the one that grows it
                        if (graph.ZeroScratch.size() < bytes)
                        {
                            graph.ZeroScratch.assign(bytes, std::byte{ 0 });
                        }

                        wgpu::TexelCopyTextureInfo destination{};
                        destination.texture = rhi::UnwrapWebGpuTexture(object);
                        destination.mipLevel = mip;
                        destination.aspect = wgpu::TextureAspect::All;

                        wgpu::TexelCopyBufferLayout layout{};
                        layout.offset = 0u;
                        layout.bytesPerRow = width * texelBytes;
                        layout.rowsPerImage = height;

                        const wgpu::Extent3D extent{ width, height, depth };
                        graph.Queue.WriteTexture(&destination, graph.ZeroScratch.data(), bytes, &layout, &extent);
                    }
                }
            }
        }
    }

    void RecordDispatch(GraphImpl& graph,
                        const wgpu::ComputePassEncoder& compute_pass,
                        int32_t pass_index,
                        int32_t& bound_pipeline)
    {
        const CompiledData& data = graph.Compiled.Data();
        const LaidOutPass& laidOut = data.LaidOut.Passes[pass_index];
        const PipelineRuntime& pipeline = graph.Pipelines[laidOut.Pipeline];
        const PassRuntime& pass = graph.Passes[pass_index];
        const int32_t phase = PhaseOf(pass.Phases, graph.FrameIndex);

        if (laidOut.Pipeline != bound_pipeline)
        {
            compute_pass.SetPipeline(pipeline.Pipeline);
            bound_pipeline = laidOut.Pipeline;
        }

        for (int32_t group = 0; group < pass.GroupCount; ++group)
        {
            compute_pass.SetBindGroup(static_cast<uint32_t>(group), pass.BindGroups[static_cast<size_t>(phase) * pass.GroupCount + group]);
        }

        const LaidOutDispatch& dispatch = laidOut.Dispatch;

        if (dispatch.Source == GroupCountSource::Indirect)
        {
            const FrozenDispatch& frozen = data.Frozen.Passes[pass_index].Dispatch;
            const wgpu::Buffer buffer = rhi::UnwrapWebGpuBuffer(graph.Rows[frozen.Resource.Value].Future.Get());
            compute_pass.DispatchWorkgroupsIndirect(buffer, static_cast<uint64_t>(frozen.ArgumentOffset));
            return;
        }

        if (dispatch.Source == GroupCountSource::AtRealize)
        {
            const int32_t image = data.Frozen.Passes[pass_index].Dispatch.Resource.Value;
            const math::UInt3 groups = GroupsOverImage(graph, image, dispatch.Workgroup);
            compute_pass.DispatchWorkgroups(groups[0], groups[1], groups[2]);
            return;
        }

        compute_pass.DispatchWorkgroups(static_cast<uint32_t>(dispatch.Groups.X),
                                        static_cast<uint32_t>(dispatch.Groups.Y),
                                        static_cast<uint32_t>(dispatch.Groups.Z));
    }

    // Read is bit 0 and Write bit 1 of ResourceAccess
    bool WritesUse(const PassUse& use)
    {
        return (static_cast<uint8_t>(use.Access) & 2u) != 0u;
    }

    uint32_t MipExtent(int32_t base, uint32_t mip)
    {
        return std::max(static_cast<uint32_t>(std::max(base, 1)) >> mip, 1u);
    }

    int32_t PhaseOf(int32_t phases, int64_t frame_index) noexcept
    {
        return phases == 1 ? 0 : static_cast<int32_t>(frame_index % phases);
    }

}

}
