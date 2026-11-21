#include "Tick.hpp"
#include "FramePlan.hpp"
#include "GpuTimers.hpp"
#include "GraphImpl.hpp"
#include "ResourceFuture.hpp"
#include "RhiHandle.hpp"
#include "RhiTypes.hpp"
#include "RhiFlags.hpp"
#include "RhiResourceFlags.hpp"
#include "VulkanMapping.hpp"
#include "CommandBuffer.hpp"
#include "CompiledGraph.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "LoomGraph.hpp"
#include "Queue.hpp"
#include "ResourceTypes.hpp"
#include "bind/BoundGraph.hpp"
#include "compile/CompiledData.hpp"
#include "freeze/FrozenGraph.hpp"
#include "layout/LaidOutGraph.hpp"
#include <cstddef>
#include <vulkan/vulkan_core.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <span>

// The backend's steps of one Tick (spec 5.1): poll, write the descriptor sets, record and submit. Graph::Tick
// (LoomGraph.cpp) runs them around the core's decisions. Every wait is a poll.
namespace loom
{

namespace
{

    // Q4: a Created future whose transfer is not submitted yet has nothing a submit may wait on, so it is Pending.
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

        if (code == DiagnosticCode::LM701)
        {
            diag.Expected = DiagnosticValue{ .FieldName = "Type", .Kind = DiagnosticValueKind::Text, .Text = "Buffer" };
        }

        graph.Sink.Report(diag);
    }

    rhi::BufferHandle BufferOfRow(const GraphImpl& graph, int32_t row)
    {
        return rhi::BufferHandle{ graph.Rows[row].Future.Get().VkHandle };
    }

    // One batch, one vkCmdPipelineBarrier2.
    void RecordBatch(GraphImpl& graph, rhi::CommandBuffer& command_buffer, const BarrierBatch& batch)
    {
        graph.BarrierScratch.clear();

        for (int32_t index = batch.FirstBarrier; index < batch.FirstBarrier + batch.Count; ++index)
        {
            const PlannedBarrier& planned = graph.Plan.Barriers[index];

            graph.BarrierScratch.push_back(rhi::BufferBarrier
            {
                .Buffer = BufferOfRow(graph, planned.Resource),
                .SrcStages = ToPipelineStages(planned.Before.Stages),
                .SrcAccess = ToAccess(planned.Before.Accesses),
                .DstStages = ToPipelineStages(planned.After.Stages),
                .DstAccess = ToAccess(planned.After.Accesses),
            });
        }

        command_buffer.Barrier(rhi::DependencyInfo{ {}, std::span<const rhi::BufferBarrier>{ graph.BarrierScratch }, {} });
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
            graph.SetScratch.clear();

            for (int32_t group = 0; group < pass.GroupCount; ++group)
            {
                const VkDescriptorSet set = pass.Sets[static_cast<size_t>(frame_slot) * pass.GroupCount + group];
                graph.SetScratch.push_back(rhi::DescriptorSetHandle{ reinterpret_cast<uint64_t>(set) });
            }

            command_buffer.BindDescriptorSets(rhi::PipelineBindPoint::Compute, pipeline.LayoutHandle, 0u,
                                              std::span<const rhi::DescriptorSetHandle>{ graph.SetScratch });
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

    // CC-R2: every row a running pass uses that is Created and not Complete waits for its completion, from any
    // timeline. One wait per semaphore, at the highest value.
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

}

    // Spec 5.1 step 2. LM701 and LM702 are reported once per epoch.
    void PollRows(GraphImpl& graph)
    {
        for (int32_t row = 0; row < static_cast<int32_t>(graph.Rows.size()); ++row)
        {
            RowRuntime& runtime = graph.Rows[row];
            ResourceState& state = graph.States[row];
            state.Readiness = ReadinessOf(runtime.Future);

            const bool exists = state.Readiness == ResourceReadiness::Created ||
                                state.Readiness == ResourceReadiness::Complete;

            // a supplied object must be a buffer: the graph only imports buffers
            if (exists && !runtime.Owned && !runtime.KindMismatch)
            {
                const ResourceType type = runtime.Future.Get().Type;
                runtime.KindMismatch = type != ResourceType::Buffer;

                if (runtime.KindMismatch)
                {
                    ReportRow(graph, DiagnosticCode::LM701, row);
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

        for (const int32_t passIndex : graph.Plan.Runs)
        {
            const BoundPass& bound = data.BoundPasses[passIndex];
            PassRuntime& pass = graph.Passes[passIndex];

            for (int32_t slotOffset = 0; slotOffset < bound.SlotCount; ++slotOffset)
            {
                const BindingSlot& slot = data.Slots[bound.FirstSlot + slotOffset];
                const int32_t row = slot.Resource.Value;
                uint32_t& written = pass.WrittenEpochs[static_cast<size_t>(frame_slot) * bound.SlotCount + slotOffset];

                if (written == graph.States[row].Epoch)
                {
                    continue;
                }

                written = graph.States[row].Epoch;
                const VkDescriptorSet set = pass.Sets[static_cast<size_t>(frame_slot) * pass.GroupCount + slot.Group];
                const bool writable = data.SlotWritable[bound.FirstSlot + slotOffset] != 0u;

                // the scratch was reserved for every slot, so these pointers stay valid
                const VkDescriptorBufferInfo& info = graph.BufferInfoScratch.emplace_back(VkDescriptorBufferInfo
                {
                    reinterpret_cast<VkBuffer>(graph.Rows[row].Future.Get().VkHandle),
                    0u,
                    VK_WHOLE_SIZE
                });

                graph.WriteScratch.push_back(VkWriteDescriptorSet
                {
                    VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    nullptr,
                    set,
                    slot.Binding,
                    0u,
                    1u,
                    *ToDescriptorType(slot.Kind, writable),
                    nullptr,
                    &info,
                    nullptr
                });
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

}
