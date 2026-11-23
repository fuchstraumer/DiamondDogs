#include "Tick.hpp"
#include "FramePlan.hpp"
#include "GpuTimers.hpp"
#include "GraphImpl.hpp"
#include "Records.hpp"
#include "ResourceFuture.hpp"
#include "RhiHandle.hpp"
#include "RhiTypes.hpp"
#include "RhiFlags.hpp"
#include "RhiResourceFlags.hpp"
#include "ShaderLibraryTypes.hpp"
#include "VulkanMapping.hpp"
#include "CommandBuffer.hpp"
#include "CompiledGraph.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "LoomGraph.hpp"
#include "math/MathStorage.hpp"
#include "Queue.hpp"
#include "ResourceTypes.hpp"
#include "bind/BoundGraph.hpp"
#include "compile/CompiledData.hpp"
#include "freeze/FrozenGraph.hpp"
#include "layout/LaidOutGraph.hpp"
#include "size/SizedGraph.hpp"
#include "LoomFormats.hpp"
#include "Declarations.hpp"
#include <array>
#include <string_view>
#include <cstddef>
#include <vulkan/vulkan_core.h>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

using namespace math;

// The backend's steps of one Tick (spec 5.1): poll, write the descriptor sets, record and submit. Graph::Tick
// (LoomGraph.cpp) runs them around the core's decisions. Every wait is a poll.
namespace loom
{

namespace
{
    // Q4: a Created future whose transfer is not submitted yet has nothing a submit may wait on, so it is Pending.
    ResourceReadiness ReadinessOf(const rhi::ResourceFuture<GraphicsResource>& future);
    // A history ring is as ready as its least ready copy: Failed, then Pending, then Created, then Complete.
    ResourceReadiness ReadinessOfRow(const GraphImpl& graph, int32_t row);
    void ReportRow(GraphImpl& graph, DiagnosticCode code, int32_t row);

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
    rhi::BufferHandle BufferOfRow(const GraphImpl& graph, int32_t row);

    // One batch, one vkCmdPipelineBarrier2 with one global barrier (plan decision B5). Every buffer uses concurrent
    // sharing, so a memory barrier over the union of the rows' scopes orders the same work as one barrier per row.
    // Images (step 6) need their own barriers for layout changes. ExplainLastTick still lists each row.
    void RecordBatch(GraphImpl& graph, rhi::CommandBuffer& command_buffer, const BarrierBatch& batch);

    // DispatchThreadsOver an image whose extent was unknown at Compile (an AnyExtent import): the threads cover its
    // width, height, and depth or layers, as Layout counts a known extent.
    UInt3 GroupsOverImage(const GraphImpl& graph, int32_t row, const lodestone::WorkgroupSize& workgroup);

    // Zeroes each copy in plan.Clears: the whole buffer, or every mip and layer of an image (in General).
    void RecordClears(const GraphImpl& graph, rhi::CommandBuffer& command_buffer);
    bool RecordDispatch(GraphImpl& graph, rhi::CommandBuffer& command_buffer, int32_t pass_index, int32_t frame_slot);
    bool RecordFrame(GraphImpl& graph, rhi::CommandBuffer& command_buffer, int32_t frame_slot, uint64_t signal_value);
    // CC-R2: every row a running pass uses that is Created and not Complete waits for its completion, from any
    // timeline. One wait per semaphore, at the highest value.
    void GatherWaits(GraphImpl& graph);

}

// Spec 5.1 step 2. LM701 and LM702 are reported once per epoch.
void PollRows(GraphImpl& graph)
{
    for (int32_t row = 0; std::cmp_less(row,graph.Rows.size()); ++row)
    {
        RowRuntime& runtime = graph.Rows[row];
        ResourceState& state = graph.States[row];
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
    }
}

// A frame slot's sets are rewritten only where a row changed epoch since the last write. The GPU is done with the
// slot (Tick checked its timeline value), so its sets may change.
void WriteDescriptors(GraphImpl& graph, int32_t frame_slot)
{
    const CompiledData& data = graph.Compiled.Data();
    graph.WriteScratch.clear();
    graph.BufferInfoScratch.clear();
    graph.ImageInfoScratch.clear();

    for (const int32_t passIndex : graph.Plan.Runs)
    {
        const BoundPass& bound = data.BoundPasses[passIndex];
        PassRuntime& pass = graph.Passes[passIndex];

        for (int32_t slotOffset = 0; slotOffset < bound.SlotCount; ++slotOffset)
        {
            const BindingSlot& slot = data.Slots[bound.FirstSlot + slotOffset];
            const int32_t row = slot.Resource.Value;
            const int32_t instance = SetInstanceOf(pass, frame_slot, graph.FrameIndex);
            uint32_t& written = pass.WrittenEpochs[static_cast<size_t>(instance) * bound.SlotCount + slotOffset];

            if (written == graph.States[row].Epoch)
            {
                continue;
            }

            written = graph.States[row].Epoch;
            const int32_t slotIndex = bound.FirstSlot + slotOffset;
            const VkDescriptorSet set = pass.Sets[static_cast<size_t>(instance) * pass.GroupCount + slot.Group];
            const bool writable = data.SlotWritable[slotIndex] != 0u;
            // a history ring's copy for this frame; an instance always meets the same copies (Phases)
            const GraphicsResource& object = FutureOfCopy(graph, CopyOf(data, row, graph.FrameIndex, slot.Previous)).Get();

            VkWriteDescriptorSet write
            {
                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                nullptr,
                set,
                slot.Binding,
                0u,
                1u,
                *ToDescriptorType(slot.Kind, writable),
                nullptr,
                nullptr,
                nullptr
            };

            // the scratch was reserved for every slot, so these pointers stay valid
            switch (slot.Kind)
            {
            case lodestone::BindingKind::Texture:
                [[fallthrough]];
            case lodestone::BindingKind::StorageTexture:
                write.pImageInfo = &graph.ImageInfoScratch.emplace_back(VkDescriptorImageInfo
                {
                    VK_NULL_HANDLE,
                    reinterpret_cast<VkImageView>(object.VkViewHandle),
                    static_cast<VkImageLayout>(data.SlotLayouts[slotIndex])
                });

                break;
            case lodestone::BindingKind::Sampler:
                write.pImageInfo = &graph.ImageInfoScratch.emplace_back(VkDescriptorImageInfo
                {
                    reinterpret_cast<VkSampler>(object.VkSamplerHandle),
                    VK_NULL_HANDLE,
                    VK_IMAGE_LAYOUT_UNDEFINED
                });

                break;
            default:
                write.pBufferInfo = &graph.BufferInfoScratch.emplace_back(VkDescriptorBufferInfo
                {
                    reinterpret_cast<VkBuffer>(object.VkHandle),
                    0u,
                    VK_WHOLE_SIZE
                });

                break;
            }

            graph.WriteScratch.push_back(write);
        }
    }

    if (!graph.WriteScratch.empty())
    {
        vkUpdateDescriptorSets(graph.VulkanDevice, static_cast<uint32_t>(graph.WriteScratch.size()),
                                graph.WriteScratch.data(), 0u, nullptr);
    }
}

bool SubmitFrame(GraphImpl& graph, int32_t frame_slot, uint64_t signal_value)
{
    FrameSlotRuntime& slot = graph.FrameSlots[frame_slot];
    GatherWaits(graph);

    if (slot.Pool.Reset().IsFailure())
    {
        return false;
    }

    rhi::CommandBuffer commandBuffer(slot.Pool.GetCommandBuffer(0u));

    if (commandBuffer.Begin().IsFailure())
    {
        return false;
    }

    const bool recorded = RecordFrame(graph, commandBuffer, frame_slot, signal_value);

    if (commandBuffer.End().IsFailure() || !recorded)
    {
        return false;
    }

    const rhi::SemaphoreSubmitInfo signal{ graph.Timeline.Handle(), signal_value, rhi::PipelineStage::AllCommands };
    const rhi::CommandBufferHandle commandBufferHandle = commandBuffer.Handle();

    const rhi::SubmitInfo submit
    {
        std::span<const rhi::SemaphoreSubmitInfo>{ graph.WaitScratch },
        std::span<const rhi::CommandBufferHandle>{ &commandBufferHandle, 1u },
        std::span<const rhi::SemaphoreSubmitInfo>{ &signal, 1u }
    };

    return rhi::QueueSubmit(graph.Queue, submit).IsSuccess();
}

namespace
{

    ResourceReadiness ReadinessOf(const rhi::ResourceFuture<GraphicsResource>& future)
    {
        switch (future.State())
        {
        case rhi::FutureState::Created:
        {
            const rhi::GpuWait completion = future.Completion();
            const bool unsubmitted = completion.Value != 0u && !completion.Submitted;
            return unsubmitted ? ResourceReadiness::Pending : ResourceReadiness::Created;
        }
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

    // A history ring is as ready as its least ready copy: Failed, then Pending, then Created, then Complete.
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

        if (promised != k_UnknownByteSize && std::cmp_less(description.ByteSize,promised))
        {
            return IntegerMismatch("ByteSize", promised, static_cast<int64_t>(description.ByteSize));
        }

        return ImportMismatch{};
    }

    // An image: its format and layer count; for an Exact expectation, its extent and mip count too.
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

    rhi::BufferHandle BufferOfRow(const GraphImpl& graph, int32_t row)
    {
        return rhi::BufferHandle{ graph.Rows[row].Future.Get().VkHandle };
    }

    void RecordBatch(GraphImpl& graph, rhi::CommandBuffer& command_buffer, const BarrierBatch& batch)
    {
        rhi::GlobalBarrier merged{};
        bool anyGlobal = false;
        graph.ImageBarrierScratch.clear();

        for (int32_t index = batch.FirstBarrier; index < batch.FirstBarrier + batch.Count; ++index)
        {
            const PlannedBarrier& planned = graph.Plan.Barriers[index];

            // every mip and layer; step 6 binds color images only
            if (planned.ChangesLayout())
            {
                graph.ImageBarrierScratch.push_back(rhi::ImageBarrier
                {
                    .Image = rhi::ImageHandle{ FutureOfCopy(graph, planned.Resource).Get().VkHandle },
                    .SrcStages = planned.Before.Stages,
                    .SrcAccess = planned.Before.Accesses,
                    .DstStages = planned.After.Stages,
                    .DstAccess = planned.After.Accesses,
                    .OldLayout = planned.OldLayout,
                    .NewLayout = planned.NewLayout,
                });

                continue;
            }

            merged.SrcStages |= planned.Before.Stages;
            merged.SrcAccess |= planned.Before.Accesses;
            merged.DstStages |= planned.After.Stages;
            merged.DstAccess |= planned.After.Accesses;
            anyGlobal = true;
        }

        const std::span<const rhi::GlobalBarrier> globals{ &merged, anyGlobal ? 1u : 0u };
        const std::span<const rhi::ImageBarrier> images{ graph.ImageBarrierScratch };
        command_buffer.Barrier(rhi::DependencyInfo{ globals, {}, images });
    }

    UInt3 GroupsOverImage(const GraphImpl& graph, int32_t row, const lodestone::WorkgroupSize& workgroup)
    {
        constexpr static UInt3 k_One{ 1u, 1u, 1u };
        const ResourceDescription& description = graph.Rows[row].Future.Description();

        const bool volume = description.Dimensions == ImageType::Type3D;
        const uint32_t threadZ = volume ? std::max(static_cast<uint32_t>(description.Depth), 1u) :
                                          std::max(static_cast<uint32_t>(description.ArrayLayers), 1u);
        UInt3 threadsInt
        {
            static_cast<uint32_t>(description.Width),
            static_cast<uint32_t>(description.Height),
            threadZ
        };
        
        // make sure x,y,z are at least 1
        threadsInt = threadsInt.Max(k_One);

        const UInt3 sizesInt{ workgroup.X, workgroup.Y, workgroup.Z };
        
        return (threadsInt + sizesInt - k_One) / sizesInt;
    }

    void RecordClears(const GraphImpl& graph, rhi::CommandBuffer& command_buffer)
    {
        const CompiledData& data = graph.Compiled.Data();
        const VkCommandBuffer vulkanCommandBuffer = command_buffer.Handle().As<VkCommandBuffer>();

        for (const int32_t copy : graph.Plan.Clears)
        {
            const GraphicsResource& object = FutureOfCopy(graph, copy).Get();

            if (data.Frozen.Resources[data.CopyRows[copy]].Kind != ResourceKind::Image)
            {
                command_buffer.FillBuffer(rhi::BufferHandle{ object.VkHandle }, 0u, VK_WHOLE_SIZE, 0u);
                continue;
            }

            const VkClearColorValue zero{};
            const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0u, VK_REMAINING_MIP_LEVELS, 0u, VK_REMAINING_ARRAY_LAYERS };
            vkCmdClearColorImage(vulkanCommandBuffer, reinterpret_cast<VkImage>(object.VkHandle), VK_IMAGE_LAYOUT_GENERAL,
                                 &zero, 1u, &range);
        }
    }

    bool RecordDispatch(GraphImpl& graph, rhi::CommandBuffer& command_buffer, int32_t pass_index, int32_t frame_slot)
    {
        const CompiledData& data = graph.Compiled.Data();
        const LaidOutPass& laidOut = data.LaidOut.Passes[pass_index];
        const PipelineRuntime& pipeline = graph.Pipelines[laidOut.Pipeline];
        const PassRuntime& pass = graph.Passes[pass_index];

        if (command_buffer.BindShaders(*pipeline.Program).IsFailure())
        {
            return false;
        }

        if (pass.GroupCount > 0)
        {
            const int32_t instance = SetInstanceOf(pass, frame_slot, graph.FrameIndex);
            const VkDescriptorSet* setsStart = &pass.Sets[(static_cast<size_t>(instance) * pass.GroupCount)];

            const std::span<const rhi::DescriptorSetHandle> descriptorSets
            { 
                reinterpret_cast<const rhi::DescriptorSetHandle*>(setsStart),
                static_cast<size_t>(pass.GroupCount)
            };
    
            command_buffer.BindDescriptorSets(rhi::PipelineBindPoint::Compute,
                                              pipeline.LayoutHandle,
                                              0u,
                                              descriptorSets);
        }

        const int32_t params = data.Passes[pass_index].Params;

        if (pipeline.PushBytes > 0u && params >= 0)
        {
            command_buffer.PushConstants(pipeline.LayoutHandle, pipeline.PushStages, 0u, graph.Params.Bytes(params));
        }

        const LaidOutDispatch& dispatch = laidOut.Dispatch;

        if (dispatch.Source == GroupCountSource::Indirect)
        {
            const FrozenDispatch& frozen = data.Frozen.Passes[pass_index].Dispatch;
            command_buffer.DispatchIndirect(BufferOfRow(graph, frozen.Resource.Value), static_cast<uint64_t>(frozen.ArgumentOffset));
            return true;
        }

        if (dispatch.Source == GroupCountSource::AtRealize)
        {
            const int32_t image = data.Frozen.Passes[pass_index].Dispatch.Resource.Value;
            const std::array<uint32_t, 3u> groups = GroupsOverImage(graph, image, dispatch.Workgroup);
            command_buffer.Dispatch(groups[0], groups[1], groups[2]);
            return true;
        }

        command_buffer.Dispatch(static_cast<uint32_t>(dispatch.Groups.X), static_cast<uint32_t>(dispatch.Groups.Y),
                                static_cast<uint32_t>(dispatch.Groups.Z));
        return true;
    }

    bool RecordFrame(GraphImpl& graph, rhi::CommandBuffer& command_buffer, int32_t frame_slot, uint64_t signal_value)
    {
        const CompiledData& data = graph.Compiled.Data();
        std::optional<rhi::GpuTimerBlock> timerBlock;

        if (graph.Timers.has_value())
        {
            timerBlock.emplace(*graph.Timers, command_buffer, signal_value);
        }

        size_t batch = 0u;
        const int32_t runCount = static_cast<int32_t>(graph.Plan.Runs.size());

        // OnNoHistory::Clear: the batch before the clears, then the clears, before every run
        if (!graph.Plan.Batches.empty() && graph.Plan.Batches.front().BeforeRun == k_BeforeClears)
        {
            RecordBatch(graph, command_buffer, graph.Plan.Batches.front());
            RecordClears(graph, command_buffer);
            ++batch;
        }

        for (int32_t runIndex = 0; runIndex < runCount; ++runIndex)
        {
            const bool batchHere = batch < graph.Plan.Batches.size() && graph.Plan.Batches[batch].BeforeRun == runIndex;

            if (batchHere)
            {
                RecordBatch(graph, command_buffer, graph.Plan.Batches[batch]);
                ++batch;
            }

            const int32_t passIndex = graph.Plan.Runs[runIndex];
            std::optional<rhi::ScopedGpuTimer> scope;

            if (timerBlock.has_value())
            {
                scope.emplace(*timerBlock, data.Frozen.Passes[passIndex].Key.Name);
            }

            if (!RecordDispatch(graph, command_buffer, passIndex, frame_slot))
            {
                return false;
            }
        }

        return true;
    }

    void GatherWaits(GraphImpl& graph)
    {
        const CompiledData& data = graph.Compiled.Data();
        graph.WaitScratch.clear();

        for (const int32_t passIndex : graph.Plan.Runs)
        {
            const CompiledPass& pass = data.Passes[passIndex];

            for (int32_t useIndex = pass.FirstUse; useIndex < pass.FirstUse + pass.UseCount; ++useIndex)
            {
                const int32_t row = data.Uses[useIndex].Resource;

                if (graph.States[row].Readiness != ResourceReadiness::Created)
                {
                    continue;
                }

                const rhi::GpuWait completion = graph.Rows[row].Future.Completion();

                if (completion.Value == 0u || !completion.Submitted)
                {
                    continue;
                }

                const rhi::SemaphoreSubmitInfo candidate{ completion.Semaphore, completion.Value, rhi::PipelineStage::AllCommands };
                bool merged = false;

                for (rhi::SemaphoreSubmitInfo& wait : graph.WaitScratch)
                {
                    if (wait.Semaphore == candidate.Semaphore)
                    {
                        wait.Value = std::max(wait.Value, candidate.Value);
                        merged = true;
                    }
                }

                if (!merged)
                {
                    graph.WaitScratch.push_back(candidate);
                }
            }
        }
    }

} // namespace

} // namespace loom
