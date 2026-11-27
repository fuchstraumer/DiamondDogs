#include "LoomGraph.hpp"
#include "CompiledGraph.hpp"
#include "DecideRuns.hpp"
#include "FramePlan.hpp"
#include "GraphImpl.hpp"
#include "LoomKeys.hpp"
#include "Realize.hpp"
#include "Records.hpp"
#include "ResourceBarriers.hpp"
#include "ResourceFuture.hpp"
#include "ResourceTypes.hpp"
#include "RhiContext.hpp"
#include "RhiResourceFlags.hpp"
#include "Tick.hpp"
#include "compile/CompiledData.hpp"
#include "freeze/FrozenGraph.hpp"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace loom
{

namespace
{

    bool IsImported(const CompiledData& data, int32_t row, ResourceKind kind);
    void SupplyRow(GraphImpl& graph, int32_t row, rhi::ResourceFuture<GraphicsResource>&& future,
                   rhi::ImageLayout initial_layout);
    GraphicsResource ObjectOfRow(const GraphImpl& graph, int32_t row);

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
        case RealizeError::CodeNotWgsl:
            return "CodeNotWgsl";
        case RealizeError::PassKindNotYet:
            return "PassKindNotYet";
        case RealizeError::ResourceKindNotYet:
            return "ResourceKindNotYet";
        case RealizeError::PlacementNotBound:
            return "PlacementNotBound";
        case RealizeError::BindingNotYet:
            return "BindingNotYet";
        case RealizeError::BindGroupLayoutFailed:
            return "BindGroupLayoutFailed";
        case RealizeError::PipelineLayoutFailed:
            return "PipelineLayoutFailed";
        case RealizeError::ShaderCreateFailed:
            return "ShaderCreateFailed";
        case RealizeError::PipelineCreateFailed:
            return "PipelineCreateFailed";
        case RealizeError::ParamsBufferFailed:
            return "ParamsBufferFailed";
        case RealizeError::BindGroupMaxExceeded:
            return "BindGroupMaxExceeded";
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

        if (!inputs.Context.IsValid() || inputs.Context.Backend() != rhi::RhiBackend::WebGpu)
        {
            return std::unexpected(RealizeError::Invalid);
        }

        const RealizeError unsupported = CheckSupported(compiled.Data());

        if (unsupported != RealizeError::Invalid)
        {
            return std::unexpected(unsupported);
        }

        auto impl = std::make_unique<GraphImpl>(std::move(compiled), inputs);
        RealizeError error = CreatePipelines(*impl);

        if (error != RealizeError::Invalid)
        {
            return std::unexpected(error);
        }

        CreatePassRuntimes(*impl);
        error = CreateParamsBuffer(*impl);

        if (error != RealizeError::Invalid)
        {
            return std::unexpected(error);
        }

        CreateResources(*impl);
        PrepareCoreState(*impl);

        return std::unique_ptr<Graph>(new Graph(std::move(impl)));
    }

    Graph::Graph(std::unique_ptr<GraphImpl> _impl) noexcept : impl{ std::move(_impl) }
    {
    }

    Graph::~Graph() = default;

    TickResult Graph::Tick([[maybe_unused]] std::chrono::duration<double> delta_time)
    {
        GraphImpl& graph = *impl;

        PollRows(graph);
        PollPipelines(graph);
        RefreshSync(graph);
        DecideRuns(graph.Compiled, graph.States, graph.Results->States, graph.Controls, graph.Sync, graph.FrameIndex,
                   graph.Plan);

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

        UploadParams(graph);
        UpdateBindGroups(graph);

        if (!SubmitFrame(graph))
        {
            return TickResult{ .Status = TickStatus::Failed, .Ran = 0, .Skipped = counts.Skipped };
        }

        MarkContents(graph);
        CommitRuns(graph.Compiled, graph.Plan, graph.Controls);
        ++graph.FrameIndex;

        return TickResult{ .Status = TickStatus::Submitted, .Ran = counts.Ran, .Skipped = counts.Skipped };
    }

    bool Graph::Ready()
    {
        GraphImpl& graph = *impl;
        PollRows(graph);
        PollPipelines(graph);

        const auto isReady = [](PipelineReadiness state)
        {
            return state == PipelineReadiness::Ready;
        };

        const auto exists = [](const ResourceState& state)
        {
            return state.Readiness == ResourceReadiness::Created || state.Readiness == ResourceReadiness::Complete;
        };

        return std::ranges::all_of(graph.Results->States, isReady) && std::ranges::all_of(graph.States, exists);
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

    void SupplyRow(GraphImpl& graph,
                   int32_t row,
                   rhi::ResourceFuture<GraphicsResource>&& future,
                   rhi::ImageLayout initial_layout)
    {
        graph.Rows[row].Future = std::move(future);
        graph.Rows[row].KindMismatch = false;
        graph.Rows[row].ImportChecked = false;
        graph.States[row].Readiness = ResourceReadiness::Pending;
        ++graph.States[row].Epoch;
        graph.States[row].InitialLayout = initial_layout;
        ++graph.SupplyGeneration;
        graph.AllRowsSettled = false;
        MarkRowChanged(graph.Compiled, row, graph.Controls);
    }

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

}
