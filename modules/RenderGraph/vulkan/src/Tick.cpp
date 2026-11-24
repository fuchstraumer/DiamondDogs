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
#include "DecideRuns.hpp"
#include "Device.hpp"
#include "FrameTarget.hpp"
#include "ResourceContext.hpp"
#include "Semaphore.hpp"
#include "LoomFrame.hpp"
#include "RasterRecording.hpp"
#include "Realize.hpp"
#include "RecordContext.hpp"
#include "TargetExtents.hpp"
#include <array>
#include <limits>
#include <memory>
#include <vector>
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
    ImportMismatch CheckBufferImport(const CompiledData& data,
                                     int32_t row,
                                     const ResourceDescription& description);
    // An image: its format and layer count; for an Exact expectation, its extent and mip count too.
    ImportMismatch CheckImageImport(const CompiledData& data,
                                    int32_t row,
                                    const ResourceDescription& description);
    ImportMismatch CheckImport(const CompiledData& data,
                               int32_t row,
                               const ResourceDescription& description);
    void ReportMismatch(GraphImpl& graph, int32_t row, const ImportMismatch& mismatch);
    rhi::BufferHandle BufferOfRow(const GraphImpl& graph, int32_t row);

    // One batch, one vkCmdPipelineBarrier2 with one global barrier (plan decision B5). Every buffer uses concurrent
    // sharing, so a memory barrier over the union of the rows' scopes orders the same work as one barrier per row.
    // Images (step 6) need their own barriers for layout changes. ExplainLastTick still lists each row.
    void RecordBatch(GraphImpl& graph, rhi::CommandBuffer& command_buffer, const BarrierBatch& batch);
    // The aspects of an image row: depth (and stencil) for a depth format, color for the rest and for frame targets
    rhi::ImageAspect AspectOfRow(const CompiledData& data, int32_t row);

    // DispatchThreadsOver an image whose extent was unknown at Compile (an AnyExtent import): the threads cover its
    // width, height, and depth or layers, as Layout counts a known extent.
    UInt3 GroupsOverImage(const GraphImpl& graph, int32_t row, const lodestone::WorkgroupSize& workgroup);

    // Zeroes each copy in plan.Clears: the whole buffer, or every mip and layer of an image (in General).
    void RecordClears(const GraphImpl& graph, rhi::CommandBuffer& command_buffer);
    bool RecordDispatch(GraphImpl& graph,
                        rhi::CommandBuffer& command_buffer,
                        int32_t pass_index,
                        int32_t frame_slot,
                        int32_t& bound_pipeline);
    bool RecordFrame(GraphImpl& graph,
                     rhi::CommandBuffer& command_buffer,
                     int32_t frame_slot,
                     uint64_t signal_value);
    // CC-R2: every row a running pass uses that is Created and not Complete waits for its completion, from any
    // timeline. One wait per semaphore, at the highest value.
    void GatherWaits(GraphImpl& graph);

}

void RetireObjects(GraphImpl& graph)
{
    const rhi::GpuWait after{ .Semaphore = graph.Timeline.Handle(), .Value = graph.LastSignaled, .Submitted = true };

    // an object can be destroyed once it exists; the GPU may still read it until the last submit passes
    const auto destroyIfMade = [&graph, &after](rhi::ResourceFuture<GraphicsResource>& future)
    {
        const rhi::FutureState state = future.State();

        if (state == rhi::FutureState::Pending)
        {
            return false;
        }

        if (state == rhi::FutureState::Complete || state == rhi::FutureState::Created)
        {
            graph.Destroys.push_back(graph.LastSignaled > 0u ? graph.Resources.DestroyResource(future.Get(), after)
                                                              : graph.Resources.DestroyResource(future.Get()));
        }

        return true;
    };

    std::erase_if(graph.Retired, destroyIfMade);

    const auto finished = [](const rhi::OperationFuture& destroy)
    {
        return destroy.State() != rhi::FutureState::Pending;
    };

    std::erase_if(graph.Destroys, finished);
}

namespace
{

    // A first allocation, a swap: the row's objects changed, so its barrier state, its descriptors and the
    // WhenInputsChange readers start again
    void StartRowEpoch(GraphImpl& graph, int32_t row)
    {
        ++graph.States[row].Epoch;
        graph.States[row].Readiness = ResourceReadiness::Pending;
        graph.States[row].InitialLayout = rhi::ImageLayout::Undefined;
        graph.AllRowsSettled = false;
        ++graph.SupplyGeneration;
        MarkRowChanged(graph.Compiled, row, graph.Controls);
    }

    // Makes every copy of every OfTarget image of `target` at the new allocation extent. A copy that has no object
    // yet (the first allocation) takes it at once; the others wait in NextImages for one swap.
    void ReallocateTarget(GraphImpl& graph, int32_t target)
    {
        const CompiledData& data = graph.Compiled.Data();
        const Extent2D allocation = graph.Targets[target].Extents.Allocation;
        int32_t lastFirstAllocated = -1;

        for (int32_t copy = 0; copy < static_cast<int32_t>(data.CopyRows.size()); ++copy)
        {
            const int32_t row = data.CopyRows[copy];

            if (IsFrameTargetRow(data, row) || TargetOfImage(data, row) != target)
            {
                continue;
            }

            rhi::ResourceFuture<GraphicsResource> image = CreateTargetImage(graph, row, copy, allocation);
            rhi::ResourceFuture<GraphicsResource>& current = FutureOfCopy(graph, copy);

            if (current.State() == rhi::FutureState::Invalid)
            {
                current = std::move(image);

                if (row != lastFirstAllocated)
                {
                    StartRowEpoch(graph, row);
                    lastFirstAllocated = row;
                }

                continue;
            }

            // a resize that settles again before its images swapped in: its images are not needed
            const auto sameCopy = [copy](const NextImage& next)
            {
                return next.Copy == copy;
            };

            const auto pending = std::ranges::find_if(graph.NextImages, sameCopy);

            if (pending != graph.NextImages.end())
            {
                graph.Retired.push_back(std::move(pending->Future));
                pending->Future = std::move(image);
                continue;
            }

            graph.NextImages.push_back(NextImage{ .Copy = copy, .Future = std::move(image) });
        }
    }

    // One Tick swaps every new image of `target` in, once all of them exist (step 7, round 2 Q1)
    void SwapTargetImages(GraphImpl& graph, int32_t target)
    {
        const CompiledData& data = graph.Compiled.Data();

        const auto ofTarget = [&data, target](const NextImage& next)
        {
            return TargetOfImage(data, data.CopyRows[next.Copy]) == target;
        };

        bool any = false;
        bool failed = false;

        for (const NextImage& next : graph.NextImages)
        {
            if (!ofTarget(next))
            {
                continue;
            }

            const rhi::FutureState state = next.Future.State();
            any = true;

            if (state == rhi::FutureState::Pending || state == rhi::FutureState::Invalid)
            {
                return;
            }

            failed = failed || state == rhi::FutureState::Failed;
        }

        if (!any)
        {
            return;
        }

        int32_t lastRow = -1;

        for (NextImage& next : graph.NextImages)
        {
            if (!ofTarget(next))
            {
                continue;
            }

            // a failed create keeps the old images: the next settled resize tries again
            if (failed)
            {
                graph.Retired.push_back(std::move(next.Future));
                continue;
            }

            rhi::ResourceFuture<GraphicsResource>& current = FutureOfCopy(graph, next.Copy);
            graph.Retired.push_back(std::move(current));
            current = std::move(next.Future);
            const int32_t row = data.CopyRows[next.Copy];

            if (row != lastRow)
            {
                StartRowEpoch(graph, row);
                lastRow = row;
            }
        }

        std::erase_if(graph.NextImages, ofTarget);
    }

}

void UpdateTargets(GraphImpl& graph, double delta_seconds)
{
    for (int32_t target = 0; target < static_cast<int32_t>(graph.Targets.size()); ++target)
    {
        TargetRuntime& runtime = graph.Targets[target];
        const Extent2D reported = runtime.Target->CurrentExtent();
        const ExtentChange change = UpdateTargetExtents(runtime.Extents, reported, delta_seconds, graph.SettleSeconds,
                                                        runtime.Target->ResizeFinished());

        if (change == ExtentChange::Reallocate)
        {
            ReallocateTarget(graph, target);
        }

        SwapTargetImages(graph, target);
    }
}

bool AcquireTargets(GraphImpl& graph)
{
    const CompiledData& data = graph.Compiled.Data();
    const rhi::GpuWait lastSubmit{ .Semaphore = graph.Timeline.Handle(), .Value = graph.LastSignaled,
                                   .Submitted = graph.LastSignaled > 0u };

    for (size_t target = 0u; target < graph.Targets.size(); ++target)
    {
        TargetRuntime& runtime = graph.Targets[target];
        const TargetAction action = graph.Plan.TargetActions[target];

        if (action == TargetAction::Skip || runtime.Acquired.has_value())
        {
            continue;
        }

        runtime.Acquired = runtime.Target->TryAcquire(lastSubmit);

        if (!runtime.Acquired.has_value())
        {
            return false;
        }

        // a present may not reuse a semaphore that is still pending, so each image has its own
        while (runtime.RenderDone.size() < runtime.Acquired->ImageCount)
        {
            runtime.RenderDone.push_back(std::make_unique<rhi::BinarySemaphore>(graph.Device.Handle()));
        }

        // the acquired image owes nothing to earlier frames, and starts Undefined
        ResourceState& state = graph.States[data.FrameTargets[target].Row];
        ++state.Epoch;
        state.InitialLayout = rhi::ImageLayout::Undefined;
        state.FinalLayout = runtime.Target->FinalLayout();
    }

    return true;
}

void PresentTargets(GraphImpl& graph)
{
    for (TargetRuntime& runtime : graph.Targets)
    {
        if (!runtime.Acquired.has_value())
        {
            continue;
        }

        const bool swapchain = runtime.Acquired->ImageCount > 0u;
        const rhi::BinarySemaphoreHandle renderDone = swapchain ? runtime.RenderDone[runtime.Acquired->ImageIndex]->Handle()
                                                                : rhi::BinarySemaphoreHandle{};
        runtime.Target->Present(renderDone);
        runtime.Acquired.reset();
    }
}

void UpdateFrameConstants(GraphImpl& graph)
{
    LoomFrameConstants& constants = graph.FrameConstants;
    constants.Time = static_cast<float>(graph.TimeSeconds);
    constants.DeltaTime = graph.DeltaSeconds;
    constants.FrameIndex = static_cast<uint32_t>(graph.FrameIndex);
    constants.TargetCount = static_cast<uint32_t>(graph.Targets.size());

    for (size_t target = 0u; target < graph.Targets.size(); ++target)
    {
        const TargetExtents& extents = graph.Targets[target].Extents;
        LoomTargetInfo& info = constants.Targets[target];
        info.TargetExtent = { static_cast<uint32_t>(extents.Target.Width), static_cast<uint32_t>(extents.Target.Height) };
        info.AllocationExtent = { static_cast<uint32_t>(extents.Allocation.Width), static_cast<uint32_t>(extents.Allocation.Height) };
        info.RenderExtent = { static_cast<uint32_t>(extents.Render.Width), static_cast<uint32_t>(extents.Render.Height) };

        const float width = static_cast<float>(std::max(extents.Allocation.Width, 1));
        const float height = static_cast<float>(std::max(extents.Allocation.Height, 1));
        info.RenderToAllocation = { static_cast<float>(extents.Render.Width) / width,
                                    static_cast<float>(extents.Render.Height) / height };
    }
}

// Spec 5.1 step 2. LM701 and LM702 are reported once per epoch.
void PollRows(GraphImpl& graph)
{
    // If nothing has changed since the last tick and poll, don't scan each row again!
    if (graph.AllRowsSettled)
    {
        return;
    }

    uint32_t createdCount{ 0u };
    uint32_t settledCount{ 0u };

    for (int32_t row = 0; std::cmp_less(row,graph.Rows.size()); ++row)
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

        if (state.Readiness == ResourceReadiness::Created)
        {
            ++createdCount;
        }
        else if (state.Readiness == ResourceReadiness::Complete ||
                 state.Readiness == ResourceReadiness::Failed)
        {
            ++settledCount;
        }
    }

    graph.CreatedRowCount = createdCount;
    graph.AllRowsSettled = std::cmp_equal(settledCount, graph.Rows.size());
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

        if (bound.SlotCount == 0)
        {
            continue;
        }
        
        const int32_t instance = SetInstanceOf(pass, frame_slot, graph.FrameIndex);

        if (pass.InstanceGeneration[instance] == graph.SupplyGeneration)
        {
            continue;
        }

        pass.InstanceGeneration[instance] = graph.SupplyGeneration;

        for (int32_t slotOffset = 0; slotOffset < bound.SlotCount; ++slotOffset)
        {
            const BindingSlot& slot = data.Slots[bound.FirstSlot + slotOffset];
            const int32_t slotIndex = bound.FirstSlot + slotOffset;
            uint32_t& written = pass.WrittenEpochs[static_cast<size_t>(instance) * bound.SlotCount + slotOffset];

            // LoomFrame: one buffer for the graph's life, so each set is written once
            if (slot.BuiltIn == BuiltInBinding::LoomFrame)
            {
                if (written != 0u)
                {
                    continue;
                }

                written = 1u;
                const VkWriteDescriptorSet frame
                {
                    VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    nullptr,
                    pass.Sets[static_cast<size_t>(instance) * pass.GroupCount + slot.Group],
                    slot.Binding,
                    0u,
                    1u,
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    nullptr,
                    &graph.BufferInfoScratch.emplace_back(VkDescriptorBufferInfo
                    {
                        reinterpret_cast<VkBuffer>(graph.LoomFrameBuffer.Get().VkHandle),
                        0u,
                        VK_WHOLE_SIZE
                    }),
                    nullptr
                };

                graph.WriteScratch.push_back(frame);
                continue;
            }

            const int32_t row = slot.Resource.Value;

            if (written == graph.States[row].Epoch)
            {
                continue;
            }

            written = graph.States[row].Epoch;
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

    // the timeline, and each acquired swapchain image's render-done semaphore for its present
    std::array<rhi::SemaphoreSubmitInfo, k_LoomMaxFrameTargets + 1u> signals
    {
        rhi::SemaphoreSubmitInfo{ graph.Timeline.Handle(), signal_value, rhi::PipelineStage::AllCommands },
        rhi::SemaphoreSubmitInfo{ rhi::BinarySemaphoreHandle{}, rhi::PipelineStage::AllCommands },
        rhi::SemaphoreSubmitInfo{ rhi::BinarySemaphoreHandle{}, rhi::PipelineStage::AllCommands },
        rhi::SemaphoreSubmitInfo{ rhi::BinarySemaphoreHandle{}, rhi::PipelineStage::AllCommands },
        rhi::SemaphoreSubmitInfo{ rhi::BinarySemaphoreHandle{}, rhi::PipelineStage::AllCommands },
    };

    size_t signalCount = 1u;

    for (const TargetRuntime& target : graph.Targets)
    {
        if (target.Acquired.has_value() && target.Acquired->ImageCount > 0u)
        {
            signals[signalCount] = rhi::SemaphoreSubmitInfo{ target.RenderDone[target.Acquired->ImageIndex]->Handle(),
                                                             rhi::PipelineStage::AllCommands };
            ++signalCount;
        }
    }

    const rhi::CommandBufferHandle commandBufferHandle = commandBuffer.Handle();

    const rhi::SubmitInfo submit
    {
        std::span<const rhi::SemaphoreSubmitInfo>{ graph.WaitScratch },
        std::span<const rhi::CommandBufferHandle>{ &commandBufferHandle, 1u },
        std::span<const rhi::SemaphoreSubmitInfo>{ signals.data(), signalCount }
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

            // every mip and layer, of the aspects the image has
            if (planned.ChangesLayout())
            {
                graph.ImageBarrierScratch.push_back(rhi::ImageBarrier
                {
                    .Image = rhi::ImageHandle{ reinterpret_cast<uint64_t>(ImageOfCopy(graph, planned.Resource)) },
                    .SrcStages = planned.Before.Stages,
                    .SrcAccess = planned.Before.Accesses,
                    .DstStages = planned.After.Stages,
                    .DstAccess = planned.After.Accesses,
                    .OldLayout = planned.OldLayout,
                    .NewLayout = planned.NewLayout,
                    .Range = rhi::ImageSubresourceRange{ .Aspects = AspectOfRow(graph.Compiled.Data(), graph.Compiled.Data().CopyRows[planned.Resource]) },
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
            const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0u, VK_REMAINING_MIP_LEVELS, 0u, VK_REMAINING_ARRAY_LAYERS };

            // a frame target whose writers skipped: its fallback color (step 7)
            if (IsFrameTargetRow(data, copy))
            {
                const math::Float4 color = data.FrameTargets[FrameTargetOfRow(data, copy)].Fallback.ClearColor;
                const VkClearColorValue fallback{ .float32 = { color.x, color.y, color.z, color.w } };
                vkCmdClearColorImage(vulkanCommandBuffer, ImageOfCopy(graph, copy), VK_IMAGE_LAYOUT_GENERAL, &fallback, 1u, &range);
                continue;
            }

            const GraphicsResource& object = FutureOfCopy(graph, copy).Get();

            if (data.Frozen.Resources[data.CopyRows[copy]].Kind != ResourceKind::Image)
            {
                command_buffer.FillBuffer(rhi::BufferHandle{ object.VkHandle }, 0u, VK_WHOLE_SIZE, 0u);
                continue;
            }

            const VkClearColorValue zero{};
            vkCmdClearColorImage(vulkanCommandBuffer, reinterpret_cast<VkImage>(object.VkHandle), VK_IMAGE_LAYOUT_GENERAL,
                                 &zero, 1u, &range);
        }
    }

    rhi::ImageAspect AspectOfRow(const CompiledData& data, int32_t row)
    {
        if (IsFrameTargetRow(data, row) || data.Frozen.Resources[row].Kind != ResourceKind::Image)
        {
            return rhi::ImageAspect::Color;
        }

        const lodestone::TextureFormat format = ImageFormatOf(data.Frozen, data.Frozen.Resources[row]);

        switch (format)
        {
        case lodestone::TextureFormat::Depth24PlusStencil8:
            return rhi::ImageAspect::Depth | rhi::ImageAspect::Stencil;
        case lodestone::TextureFormat::Depth16Unorm:
            [[fallthrough]];
        case lodestone::TextureFormat::Depth24Plus:
            [[fallthrough]];
        case lodestone::TextureFormat::Depth32Float:
            return rhi::ImageAspect::Depth;
        default:
            break;
        }

        return rhi::ImageAspect::Color;
    }

    // The layout this pass's use of `row` needs (Compile may have made it General for its level)
    rhi::ImageLayout LayoutOfUse(const CompiledData& data, int32_t pass_index, int32_t row)
    {
        const CompiledPass& pass = data.Passes[pass_index];

        for (int32_t useIndex = pass.FirstUse; useIndex < pass.FirstUse + pass.UseCount; ++useIndex)
        {
            if (data.Uses[useIndex].Resource == row && !data.Uses[useIndex].Previous)
            {
                return data.Uses[useIndex].Layout;
            }
        }

        return rhi::ImageLayout::Undefined;
    }

    // The extent one attachment can draw into: a frame target's acquired image; an OfTarget image's part that the
    // render extent covers (step 7 Q4); any other image's own extent
    Extent2D ExtentOfAttachment(const GraphImpl& graph, int32_t row)
    {
        const CompiledData& data = graph.Compiled.Data();

        if (IsFrameTargetRow(data, row))
        {
            const TargetRuntime& target = graph.Targets[FrameTargetOfRow(data, row)];
            return target.Acquired.has_value() ? target.Acquired->Extent : Extent2D{};
        }

        const int32_t target = TargetOfImage(data, row);

        if (target >= 0)
        {
            const float scale = data.Frozen.Images[data.Frozen.Resources[row].DeclarationIndex].Declaration.Extent.Scale;
            return ScaledExtent(graph.Targets[target].Extents.Render, scale);
        }

        const ImageExtent& extent = data.Sized.Resources[row].Extent;
        return Extent2D{ .Width = extent.Width, .Height = extent.Height };
    }

    // The render area of a pass: what every attachment covers
    Extent2D RenderAreaOf(const GraphImpl& graph, const CompiledPass& pass)
    {
        const CompiledData& data = graph.Compiled.Data();
        Extent2D area{ .Width = std::numeric_limits<int32_t>::max(), .Height = std::numeric_limits<int32_t>::max() };

        for (int32_t index = pass.FirstAttachment; index < pass.FirstAttachment + pass.AttachmentCount; ++index)
        {
            const Extent2D extent = ExtentOfAttachment(graph, data.Attachments[index].Row);
            area.Width = std::min(area.Width, extent.Width);
            area.Height = std::min(area.Height, extent.Height);
        }

        return pass.AttachmentCount > 0 ? area : Extent2D{};
    }

    rhi::LoadOp ToLoadOp(AttachmentLoad load)
    {
        switch (load)
        {
        case AttachmentLoad::Clear:
            return rhi::LoadOp::Clear;
        case AttachmentLoad::DontCare:
            return rhi::LoadOp::DontCare;
        case AttachmentLoad::Keep:
            [[fallthrough]];
        case AttachmentLoad::Invalid:
            break;
        }

        return rhi::LoadOp::Load;
    }

    // Begins rendering into the pass's attachments (the role switch of step 7 Q3), and sets the viewport (negative
    // height: Y-up NDC on Vulkan as on WebGPU, step 7 round 2 Q4) and the scissor
    void BeginPassRendering(const GraphImpl& graph, rhi::CommandBuffer& command_buffer, int32_t pass_index, Extent2D area)
    {
        const CompiledData& data = graph.Compiled.Data();
        const CompiledPass& pass = data.Passes[pass_index];
        std::array<rhi::ColorAttachment, 8u> colors{};
        uint32_t colorCount = 0u;
        std::optional<rhi::DepthStencilAttachment> depth;

        for (int32_t index = pass.FirstAttachment; index < pass.FirstAttachment + pass.AttachmentCount; ++index)
        {
            const CompiledAttachment& attachment = data.Attachments[index];
            const rhi::ImageViewHandle view{ reinterpret_cast<uint64_t>(ViewOfCopy(graph, CopyOf(data, attachment.Row, graph.FrameIndex, false))) };
            const rhi::ImageLayout layout = LayoutOfUse(data, pass_index, attachment.Row);
            const rhi::StoreOp store = attachment.Store == AttachmentStore::DontCare ? rhi::StoreOp::DontCare : rhi::StoreOp::Store;

            switch (attachment.Role)
            {
            case AttachmentRole::Color:
            {
                const uint32_t slot = static_cast<uint32_t>(attachment.Slot);

                // Layout checked the slots against maxColorAttachments (LM601)
                if (slot >= colors.size())
                {
                    break;
                }

                colors[slot] = rhi::ColorAttachment
                {
                    .ImageView = view,
                    .Layout = layout,
                    .LoadOp = ToLoadOp(attachment.Load),
                    .StoreOp = store,
                    .ClearValue = attachment.ClearValue,
                };

                colorCount = std::max(colorCount, slot + 1u);
                break;
            }
            case AttachmentRole::Depth:
            {
                const bool stencil = AspectOfRow(data, attachment.Row) != rhi::ImageAspect::Depth;
                rhi::DepthStencilAttachment target{};
                target.ImageView = view;
                target.Layout = layout;
                target.HasStencil = stencil;
                target.DepthLoadOp = ToLoadOp(attachment.Load);
                target.DepthStoreOp = store;
                target.ClearValue = rhi::ClearDepthStencilValue{ attachment.ClearValue.x, 0u };
                depth = target;
                break;
            }
            case AttachmentRole::Invalid:
                break;
            }
        }

        const uint32_t width = static_cast<uint32_t>(std::max(area.Width, 1));
        const uint32_t height = static_cast<uint32_t>(std::max(area.Height, 1));

        rhi::RenderingInfo rendering{};
        rendering.ColorAttachments = std::span<const rhi::ColorAttachment>{ colors.data(), colorCount };
        rendering.DepthStencilAttachment = depth;
        rendering.RenderArea = rhi::Rect2D{ 0, 0, width, height };
        command_buffer.BeginRendering(rendering);

        const rhi::Viewport viewport{ 0.0f, static_cast<float>(height), static_cast<float>(width), -static_cast<float>(height), 0.0f, 1.0f };
        command_buffer.SetViewports(std::span{ &viewport, 1u });
        command_buffer.SetScissors(std::span{ &rendering.RenderArea, 1u });
    }

    // The descriptor sets and the params of a pass that has a pipeline
    void BindPassResources(GraphImpl& graph, rhi::CommandBuffer& command_buffer, int32_t pass_index, int32_t frame_slot,
                           rhi::PipelineBindPoint bind_point)
    {
        const CompiledData& data = graph.Compiled.Data();
        const PipelineRuntime& pipeline = graph.Pipelines[data.LaidOut.Passes[pass_index].Pipeline];
        const PassRuntime& pass = graph.Passes[pass_index];

        if (pass.GroupCount > 0)
        {
            const int32_t instance = SetInstanceOf(pass, frame_slot, graph.FrameIndex);
            const VkDescriptorSet* setsStart = &pass.Sets[(static_cast<size_t>(instance) * pass.GroupCount)];

            const std::span<const rhi::DescriptorSetHandle> descriptorSets
            {
                reinterpret_cast<const rhi::DescriptorSetHandle*>(setsStart),
                static_cast<size_t>(pass.GroupCount)
            };

            command_buffer.BindDescriptorSets(bind_point, pipeline.LayoutHandle, 0u, descriptorSets);
        }

        const int32_t params = data.Passes[pass_index].Params;

        if (pipeline.PushBytes > 0u && params >= 0)
        {
            command_buffer.PushConstants(pipeline.LayoutHandle, pipeline.PushStages, 0u, graph.Params.Bytes(params));
        }
    }

    VkBuffer BufferOfStatement(const GraphImpl& graph, ResourceIndex resource)
    {
        if (!resource.IsValid())
        {
            return VK_NULL_HANDLE;
        }

        return reinterpret_cast<VkBuffer>(graph.Rows[resource.Value].Future.Get().VkHandle);
    }

    bool RecordDraw(GraphImpl& graph, rhi::CommandBuffer& command_buffer, int32_t pass_index, int32_t frame_slot,
                    int32_t& bound_pipeline)
    {
        const CompiledData& data = graph.Compiled.Data();
        const int32_t pipelineIndex = data.LaidOut.Passes[pass_index].Pipeline;
        const PipelineRuntime& pipeline = graph.Pipelines[pipelineIndex];
        const PipelineDescription& description = data.LaidOut.Pipelines[pipelineIndex];
        const VkCommandBuffer vulkanCommandBuffer = command_buffer.Handle().As<VkCommandBuffer>();

        // the draw state belongs to the pipeline, so it is set again only when the pipeline changes
        if (pipelineIndex != bound_pipeline)
        {
            if (command_buffer.BindShaders(*pipeline.Program).IsFailure())
            {
                return false;
            }

            uint32_t colorSlots = 0u;

            for (const LaidOutColor& color : description.Colors)
            {
                colorSlots = std::max(colorSlots, static_cast<uint32_t>(color.Slot) + 1u);
            }

            RecordDrawState(graph.Raster, vulkanCommandBuffer, description, colorSlots);
            bound_pipeline = pipelineIndex;
        }

        BindPassResources(graph, command_buffer, pass_index, frame_slot, rhi::PipelineBindPoint::Graphics);

        const CompiledPass& pass = data.Passes[pass_index];
        const FrozenPass& frozen = data.Frozen.Passes[pass_index];
        BeginPassRendering(graph, command_buffer, pass_index, RenderAreaOf(graph, pass));
        RecordDrawCommand(vulkanCommandBuffer, frozen, BufferOfStatement(graph, frozen.Draw.Arguments),
                          BufferOfStatement(graph, frozen.Draw.Count), BufferOfStatement(graph, frozen.Index.Buffer));
        command_buffer.EndRendering();
        return true;
    }

    // A Record pass: a rendering scope around the callback when it declared targets (step 7 Q7), then every piece of
    // command buffer state counts as unknown (round 2 Q5)
    void RecordCallback(GraphImpl& graph, rhi::CommandBuffer& command_buffer, int32_t pass_index, int32_t& bound_pipeline)
    {
        const CompiledData& data = graph.Compiled.Data();
        const CompiledPass& pass = data.Passes[pass_index];
        const Extent2D area = RenderAreaOf(graph, pass);
        const bool rendering = pass.AttachmentCount > 0;

        if (rendering)
        {
            BeginPassRendering(graph, command_buffer, pass_index, area);
        }

        RecordContext context{ graph, command_buffer, pass_index, area };
        data.Frozen.Passes[pass_index].Recorder(context);

        if (rendering)
        {
            command_buffer.EndRendering();
        }

        bound_pipeline = -1;
    }

    // LoomFrame: this Tick's values, written by the GPU in order with the frames around it
    void RecordFrameConstants(const GraphImpl& graph, rhi::CommandBuffer& command_buffer)
    {
        const VkBuffer buffer = reinterpret_cast<VkBuffer>(graph.LoomFrameBuffer.Get().VkHandle);

        // the last frame's reads finish before the write; the write is visible to this frame's reads
        const rhi::GlobalBarrier beforeWrite
        {
            .SrcStages = rhi::PipelineStage::AllCommands,
            .SrcAccess = rhi::Access::None,
            .DstStages = rhi::PipelineStage::AllTransfer,
            .DstAccess = rhi::Access::TransferWrite,
        };

        command_buffer.Barrier(rhi::DependencyInfo{ std::span{ &beforeWrite, 1u }, {}, {} });
        vkCmdUpdateBuffer(command_buffer.Handle().As<VkCommandBuffer>(), buffer, 0u, sizeof(LoomFrameConstants),
                          &graph.FrameConstants);

        const rhi::GlobalBarrier afterWrite
        {
            .SrcStages = rhi::PipelineStage::AllTransfer,
            .SrcAccess = rhi::Access::TransferWrite,
            .DstStages = rhi::PipelineStage::AllCommands,
            .DstAccess = rhi::Access::UniformRead,
        };

        command_buffer.Barrier(rhi::DependencyInfo{ std::span{ &afterWrite, 1u }, {}, {} });
    }

    bool RecordDispatch(GraphImpl& graph,
                        rhi::CommandBuffer& command_buffer,
                        int32_t pass_index,
                        int32_t frame_slot,
                        int32_t& bound_pipeline)
    {
        const CompiledData& data = graph.Compiled.Data();
        const LaidOutPass& laidOut = data.LaidOut.Passes[pass_index];
        const PipelineRuntime& pipeline = graph.Pipelines[laidOut.Pipeline];

        // Because our pipelines are flattened and deduplicated tables, if the indices
        // are the same we can skip binding the pipeline again!
        if (laidOut.Pipeline != bound_pipeline)
        {
            if (command_buffer.BindShaders(*pipeline.Program).IsFailure())
            {
                return false;
            }

            bound_pipeline = laidOut.Pipeline;
        }

        BindPassResources(graph, command_buffer, pass_index, frame_slot, rhi::PipelineBindPoint::Compute);
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
            const UInt3 groups = GroupsOverImage(graph, image, dispatch.Workgroup);
            command_buffer.Dispatch(groups.x, groups.y, groups.z);
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

        if (graph.HasLoomFrame)
        {
            RecordFrameConstants(graph, command_buffer);
        }

        size_t batch = 0u;
        const int32_t runCount = static_cast<int32_t>(graph.Plan.Runs.size());

        // OnNoHistory::Clear and a target's Clear fallback: the batch before the clears, then the clears, before
        // every run
        if (!graph.Plan.Batches.empty() && graph.Plan.Batches.front().BeforeRun == k_BeforeClears)
        {
            RecordBatch(graph, command_buffer, graph.Plan.Batches.front());
            RecordClears(graph, command_buffer);
            ++batch;
        }

        int32_t boundPipeline = -1;

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

            bool recorded = true;

            switch (data.Passes[passIndex].Kind)
            {
            case PassKind::Dispatch:
                recorded = RecordDispatch(graph, command_buffer, passIndex, frame_slot, boundPipeline);
                break;
            case PassKind::Draw:
                recorded = RecordDraw(graph, command_buffer, passIndex, frame_slot, boundPipeline);
                break;
            case PassKind::Record:
                RecordCallback(graph, command_buffer, passIndex, boundPipeline);
                break;
            case PassKind::Invalid:
                break;
            }

            if (!recorded)
            {
                return false;
            }
        }

        // the frame-end batch: each acquired target goes to the layout it hands back in
        if (batch < graph.Plan.Batches.size() && graph.Plan.Batches[batch].BeforeRun == k_AfterRuns)
        {
            RecordBatch(graph, command_buffer, graph.Plan.Batches[batch]);
        }

        return true;
    }

    void GatherWaits(GraphImpl& graph)
    {
        graph.WaitScratch.clear();

        // an acquired image is ready only when its acquire semaphore signals
        for (const TargetRuntime& target : graph.Targets)
        {
            if (target.Acquired.has_value() && target.Acquired->Acquired.IsValid())
            {
                graph.WaitScratch.push_back(rhi::SemaphoreSubmitInfo{ target.Acquired->Acquired, rhi::PipelineStage::AllCommands });
            }
        }

        if (graph.CreatedRowCount == 0u)
        {
            return;
        }

        const CompiledData& data = graph.Compiled.Data();

        for (const int32_t passIndex : graph.Plan.Runs)
        {
            const CompiledPass& pass = data.Passes[passIndex];

            for (int32_t useIndex = pass.FirstUse; useIndex < pass.FirstUse + pass.UseCount; ++useIndex)
            {
                const int32_t row = data.Uses[useIndex].Resource;

                // a frame target's wait is its acquire semaphore, above
                if (IsFrameTargetRow(data, row) || graph.States[row].Readiness != ResourceReadiness::Created)
                {
                    continue;
                }

                const rhi::GpuWait completion = graph.Rows[row].Future.Completion();

                if (completion.Value == 0u || !completion.Submitted)
                {
                    continue;
                }

                // Build the semaphore wait that we'll fire on the GPU to ensure this resource is ready before the next pass uses it
                const rhi::SemaphoreSubmitInfo candidate
                {
                    completion.Semaphore,
                    completion.Value,
                    rhi::PipelineStage::AllCommands
                };

                // If a semaphore with the same handle already exists, take the max of the two values:
                // since tickets are monotonically increasing, taking the max ensures we wait for the
                // latest signal without dumping the previous
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

    RecordContext::RecordContext(GraphImpl& _graph, rhi::CommandBuffer& command_buffer, int32_t _pass,
                                 Extent2D render_extent) noexcept :
        graph{ _graph },
        commandBuffer{ command_buffer },
        pass{ _pass },
        renderExtent{ render_extent }
    {
    }

    rhi::CommandBuffer& RecordContext::CommandBuffer() noexcept
    {
        return commandBuffer;
    }

    GraphicsResource RecordContext::BufferOf(ResourceKey<Buffer> resource) const noexcept
    {
        const int32_t row = graph.Compiled.ResourceIndexOf(resource.ToRef());

        if (!declares(row) || graph.Compiled.Data().Frozen.Resources[row].Kind != ResourceKind::Buffer)
        {
            return GraphicsResource::Null();
        }

        return FutureOfCopy(graph, CopyOf(graph.Compiled.Data(), row, graph.FrameIndex, false)).Get();
    }

    GraphicsResource RecordContext::ImageOf(ResourceKey<Image> resource) const noexcept
    {
        const int32_t row = graph.Compiled.ResourceIndexOf(resource.ToRef());

        if (!declares(row) || graph.Compiled.Data().Frozen.Resources[row].Kind != ResourceKind::Image)
        {
            return GraphicsResource::Null();
        }

        return FutureOfCopy(graph, CopyOf(graph.Compiled.Data(), row, graph.FrameIndex, false)).Get();
    }

    std::optional<AcquiredTarget> RecordContext::TargetOf(FrameTargetKey target) const noexcept
    {
        const CompiledData& data = graph.Compiled.Data();

        for (size_t index = 0u; index < data.Frozen.FrameTargets.size(); ++index)
        {
            if (data.Frozen.FrameTargets[index].Key.Hash == target.Hash && declares(data.FrameTargets[index].Row))
            {
                return graph.Targets[index].Acquired;
            }
        }

        return std::nullopt;
    }

    Extent2D RecordContext::RenderExtent() const noexcept
    {
        return renderExtent;
    }

    bool RecordContext::declares(int32_t row) const noexcept
    {
        if (row < 0)
        {
            return false;
        }

        const CompiledData& data = graph.Compiled.Data();
        const CompiledPass& compiled = data.Passes[pass];

        for (int32_t useIndex = compiled.FirstUse; useIndex < compiled.FirstUse + compiled.UseCount; ++useIndex)
        {
            if (data.Uses[useIndex].Resource == row)
            {
                return true;
            }
        }

        return false;
    }

} // namespace loom
