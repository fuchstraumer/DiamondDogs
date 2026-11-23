#include "LoomGraph.hpp"
#include "GpuTimers.hpp"
#include "FramePlan.hpp"
#include "LoomKeys.hpp"
#include "LoomDeviceLimits.hpp"
#include "Realize.hpp"
#include "GraphImpl.hpp"
#include "Records.hpp"
#include "ResourceFuture.hpp"
#include "ResourceTypes.hpp"
#include "RhiHandle.hpp"
#include "Tick.hpp"
#include "DecideRuns.hpp"
#include "PlanBarriers.hpp"
#include "ParamsStore.hpp"
#include "ResourceBarriers.hpp"
#include "RhiResourceFlags.hpp"
#include "RhiFlags.hpp"
#include "CompiledGraph.hpp"
#include "Device.hpp"
#include "ResourceContext.hpp"
#include "compile/CompiledData.hpp"
#include "freeze/FrozenGraph.hpp"
#include <span>
#include <cstddef>
#include <source_location>
#include <vulkan/vulkan_core.h>
#include <chrono>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace loom
{

namespace
{

    bool IsImported(const CompiledData& data, int32_t row, ResourceKind kind)
    {
        if (row < 0)
        {
            return false;
        }

        const FrozenResource& frozen = data.Frozen.Resources[row];
        return frozen.Kind == kind && frozen.Origin == ResourceOrigin::Imported;
    }

    // A new object for an imported row: a new epoch, so its barrier state starts again from `initial_layout`.
    void SupplyRow(GraphImpl& graph, int32_t row, rhi::ResourceFuture<GraphicsResource>&& future,
                   rhi::ImageLayout initial_layout)
    {
        graph.Rows[row].Future = std::move(future);
        graph.Rows[row].KindMismatch = false;
        graph.Rows[row].ImportChecked = false;
        ++graph.States[row].Epoch;
        graph.States[row].InitialLayout = initial_layout;
        MarkRowChanged(graph.Compiled, row, graph.Controls);
    }

    // For a history ring: the copy that the last submitted frame wrote.
    GraphicsResource ObjectOfRow(const GraphImpl& graph, int32_t row)
    {
        if (row < 0)
        {
            return GraphicsResource::Null();
        }

        const int32_t copy = CopyOf(graph.Compiled.Data(), row, graph.FrameIndex, true);
        const rhi::ResourceFuture<GraphicsResource>& future = FutureOfCopy(graph, copy);
        const rhi::FutureState state = future.State();
        const bool exists = state == rhi::FutureState::Created || state == rhi::FutureState::Complete;
        return exists ? future.Get() : GraphicsResource::Null();
    }

}

    std::string_view ToString(RealizeError error) noexcept
    {
        switch (error)
        {
        case RealizeError::Invalid:
            return "Invalid";
        case RealizeError::FrameSlotCount:
            return "FrameSlotCount";
        case RealizeError::TimestampsUnset:
            return "TimestampsUnset";
        case RealizeError::CodeNotSpirv:
            return "CodeNotSpirv";
        case RealizeError::PassKindNotYet:
            return "PassKindNotYet";
        case RealizeError::ResourceKindNotYet:
            return "ResourceKindNotYet";
        case RealizeError::PlacementNotBound:
            return "PlacementNotBound";
        case RealizeError::BindingNotYet:
            return "BindingNotYet";
        case RealizeError::DescriptorLayoutFailed:
            return "DescriptorLayoutFailed";
        case RealizeError::PipelineLayoutFailed:
            return "PipelineLayoutFailed";
        case RealizeError::ShaderCreateFailed:
            return "ShaderCreateFailed";
        case RealizeError::DescriptorPoolFailed:
            return "DescriptorPoolFailed";
        case RealizeError::CommandPoolFailed:
            return "CommandPoolFailed";
        }

        return "Invalid";
    }

    RealizeResult Graph::Realize(CompiledGraph&& compiled, const RealizeInputs& inputs)
    {
        if (inputs.FrameSlots < 1)
        {
            return std::unexpected(RealizeError::FrameSlotCount);
        }

        if (inputs.Timestamps == PassTimestamps::Invalid)
        {
            return std::unexpected(RealizeError::TimestampsUnset);
        }

        const RealizeError unsupported = CheckSupported(compiled.Data());

        if (unsupported != RealizeError::Invalid)
        {
            return std::unexpected(unsupported);
        }

        // on a failure below, GraphImpl's destructor frees what was made so far
        auto impl = std::make_unique<GraphImpl>(std::move(compiled), inputs);
        RealizeError error = CreatePipelines(*impl);

        if (error == RealizeError::Invalid)
        {
            error = CreateDescriptors(*impl, inputs.FrameSlots);
        }

        if (error == RealizeError::Invalid)
        {
            error = CreateFrameSlots(*impl, inputs.FrameSlots);
        }

        if (error != RealizeError::Invalid)
        {
            return std::unexpected(error);
        }

        CreateResources(*impl);
        PrepareCoreState(*impl);

        if (inputs.Timestamps == PassTimestamps::On)
        {
            const rhi::GpuTimersCreateInfo timersInfo
            {
                .QueueFamily = inputs.Device.GetQueueFamilyIndices().Graphics,
                .BlockCount = static_cast<uint32_t>(inputs.FrameSlots) + 1u,
                .MaxScopesPerBlock = static_cast<uint32_t>(impl->Compiled.PassCount()),
            };

            impl->Timers.emplace(inputs.Device, timersInfo);
        }

        impl->TimelineRegistration = inputs.Resources.RegisterConsumerTimeline(impl->Timeline.Handle());
        return std::unique_ptr<Graph>(new Graph(std::move(impl)));
    }

    Graph::Graph(std::unique_ptr<GraphImpl> _impl) noexcept : impl{ std::move(_impl) }
    {
    }

    Graph::~Graph() = default;

    TickResult Graph::Tick([[maybe_unused]] std::chrono::duration<double> delta_time)
    {
        GraphImpl& graph = *impl;
        const int32_t frameSlot = static_cast<int32_t>(graph.FrameIndex % static_cast<int64_t>(graph.FrameSlots.size()));
        FrameSlotRuntime& slot = graph.FrameSlots[frameSlot];
        uint64_t completed = 0u;

        if (graph.Timeline.GetValue(completed).IsFailure())
        {
            return TickResult{ .Status = TickStatus::Failed };
        }

        if (completed < slot.SignaledValue)
        {
            return TickResult{ .Status = TickStatus::NoFrameSlot };
        }

        if (graph.Timers.has_value())
        {
            graph.Timers->Collect(completed + 1u);
        }

        PollRows(graph);
        DecideRuns(graph.Compiled, graph.States, graph.Controls, graph.Sync, graph.FrameIndex, graph.Plan);

        const int32_t ran = static_cast<int32_t>(graph.Plan.Runs.size());
        const TickResult counts{ .Status = TickStatus::Invalid, .Ran = ran, .Skipped = graph.Compiled.PassCount() - ran };

        if (ran == 0)
        {
            return TickResult{ .Status = TickStatus::NothingToRun, .Ran = 0, .Skipped = counts.Skipped };
        }

        if (!graph.Params.CheckRuns(graph.Compiled, graph.Plan, graph.Sink))
        {
            return TickResult{ .Status = TickStatus::Failed, .Ran = 0, .Skipped = counts.Skipped };
        }

        PlanBarriers(graph.Compiled, graph.States, graph.Sync, graph.FrameIndex, graph.Plan);
        WriteDescriptors(graph, frameSlot);

        const uint64_t signalValue = graph.LastSignaled + 1u;

        if (!SubmitFrame(graph, frameSlot, signalValue))
        {
            return TickResult{ .Status = TickStatus::Failed, .Ran = 0, .Skipped = counts.Skipped };
        }

        slot.SignaledValue = signalValue;
        graph.LastSignaled = signalValue;
        CommitRuns(graph.Compiled, graph.Plan, graph.Controls);
        ++graph.FrameIndex;

        return TickResult{ .Status = TickStatus::Submitted, .Ran = counts.Ran, .Skipped = counts.Skipped };
    }

    bool Graph::WaitForFrameSlot(std::chrono::nanoseconds timeout)
    {
        const int64_t slotCount = static_cast<int64_t>(impl->FrameSlots.size());
        const FrameSlotRuntime& slot = impl->FrameSlots[impl->FrameIndex % slotCount];

        if (slot.SignaledValue == 0u)
        {
            return true;
        }

        return WaitForTimeline(*impl, slot.SignaledValue, static_cast<uint64_t>(timeout.count()));
    }

    void Graph::Supply(ResourceKey<Buffer> resource, rhi::ResourceFuture<GraphicsResource>&& future)
    {
        const int32_t row = impl->Compiled.ResourceIndexOf(resource.ToRef());

        if (!IsImported(impl->Compiled.Data(), row, ResourceKind::Buffer))
        {
            return;
        }

        SupplyRow(*impl, row, std::move(future), rhi::ImageLayout::Undefined);
    }

    void Graph::Supply(ResourceKey<Image> resource, rhi::ResourceFuture<GraphicsResource>&& future,
                       ImageUsageBits usage_at_handoff)
    {
        const int32_t row = impl->Compiled.ResourceIndexOf(resource.ToRef());

        if (!IsImported(impl->Compiled.Data(), row, ResourceKind::Image))
        {
            return;
        }

        SupplyRow(*impl, row, std::move(future), ImageLayoutOfUsage(usage_at_handoff));
    }

    void Graph::Enable(PassKey pass, bool enabled)
    {
        const int32_t index = impl->Compiled.PassIndexOf(pass);

        if (index >= 0)
        {
            impl->Controls[index].Enabled = enabled;
        }
    }

    rhi::TimelineSemaphoreHandle Graph::Timeline() const noexcept
    {
        return impl->Timeline.Handle();
    }

    uint64_t Graph::PendingValue() const noexcept
    {
        return impl->LastSignaled + 1u;
    }

    rhi::GpuWait Graph::LastSubmit() const noexcept
    {
        return rhi::GpuWait{ .Semaphore = impl->Timeline.Handle(), .Value = impl->LastSignaled, .Submitted = impl->LastSignaled > 0u };
    }

    GraphicsResource Graph::BufferOf(ResourceKey<Buffer> resource) const noexcept
    {
        return ObjectOfRow(*impl, impl->Compiled.ResourceIndexOf(resource.ToRef()));
    }

    GraphicsResource Graph::ImageOf(ResourceKey<Image> resource) const noexcept
    {
        return ObjectOfRow(*impl, impl->Compiled.ResourceIndexOf(resource.ToRef()));
    }

    const FramePlan& Graph::LastPlan() const noexcept
    {
        return impl->Plan;
    }

    const CompiledGraph& Graph::Compiled() const noexcept
    {
        return impl->Compiled;
    }

    std::optional<rhi::GpuTimingBlock> Graph::LatestTimings() const
    {
        if (!impl->Timers.has_value())
        {
            return std::nullopt;
        }

        return impl->Timers->Latest();
    }

    void Graph::setParams(KeyRef params, std::span<const std::byte> bytes, uint32_t alignment, std::source_location where)
    {
        const int32_t changed = impl->Params.Set(params, bytes, alignment, where, impl->Sink);

        if (changed != k_ParamsUnchanged)
        {
            MarkParamsChanged(impl->Compiled, changed, impl->Controls);
        }
    }

    void Graph::Trigger(PassKey pass)
    {
        const int32_t index = impl->Compiled.PassIndexOf(pass);

        if (index >= 0)
        {
            impl->Controls[index].Triggered = true;
        }
    }

    std::string Graph::ExplainLastTick() const
    {
        return loom::ExplainLastTick(impl->Compiled, impl->Plan);
    }

    ShaderDeviceLimits ShaderLimitsOf(const rhi::Device& device)
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device.GetPhysicalDevice().As<VkPhysicalDevice>(), &properties);
        const VkPhysicalDeviceLimits& limits = properties.limits;

        return ShaderDeviceLimits
        {
            .MaxWorkgroupMemoryBytes = limits.maxComputeSharedMemorySize,
            .MaxInvocationsPerWorkgroup = limits.maxComputeWorkGroupInvocations,
            .MaxWorkgroupSizeX = limits.maxComputeWorkGroupSize[0],
            .MaxWorkgroupSizeY = limits.maxComputeWorkGroupSize[1],
            .MaxWorkgroupSizeZ = limits.maxComputeWorkGroupSize[2],
            .MaxPushConstantBytes = limits.maxPushConstantsSize,
        };
    }

    LayoutLimits LayoutLimitsOf(const rhi::Device& device)
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device.GetPhysicalDevice().As<VkPhysicalDevice>(), &properties);
        const VkPhysicalDeviceLimits& limits = properties.limits;

        return LayoutLimits
        {
            .MaxComputeWorkGroupCountX = limits.maxComputeWorkGroupCount[0],
            .MaxComputeWorkGroupCountY = limits.maxComputeWorkGroupCount[1],
            .MaxComputeWorkGroupCountZ = limits.maxComputeWorkGroupCount[2],
            .MaxColorAttachments = limits.maxColorAttachments,
            .MaxBoundDescriptorSets = limits.maxBoundDescriptorSets,
            .MaxPerStageDescriptorSamplers = limits.maxPerStageDescriptorSamplers,
            .MaxPerStageDescriptorUniformBuffers = limits.maxPerStageDescriptorUniformBuffers,
            .MaxPerStageDescriptorStorageBuffers = limits.maxPerStageDescriptorStorageBuffers,
            .MaxPerStageDescriptorSampledImages = limits.maxPerStageDescriptorSampledImages,
            .MaxPerStageDescriptorStorageImages = limits.maxPerStageDescriptorStorageImages,
            .MaxPerStageResources = limits.maxPerStageResources,
            .MaxUniformBufferRange = limits.maxUniformBufferRange,
            .MaxStorageBufferRange = limits.maxStorageBufferRange,
            .MaxImageDimension1D = limits.maxImageDimension1D,
            .MaxImageDimension2D = limits.maxImageDimension2D,
            .MaxImageDimension3D = limits.maxImageDimension3D,
        };
    }

}
