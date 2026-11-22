#pragma once
#ifndef DIAMOND_DOGS_LOOM_WEBGPU_GRAPH_IMPL_HPP
#define DIAMOND_DOGS_LOOM_WEBGPU_GRAPH_IMPL_HPP
#include "CompiledGraph.hpp"
#include "FramePlan.hpp"
#include "LoomGraph.hpp"
#include "ParamsStore.hpp"
#include "ResourceFuture.hpp"
#include "ResourceTypes.hpp"
#include <webgpu/webgpu_cpp.h>
#include <cstddef>
#include <cstdint>
#include <vector>

class ResourceContext;

// The WebGPU backend's state. Realize.cpp builds it, Tick.cpp runs it, GraphImpl.cpp tears it down. Every array is
// sized at Realize, so a steady-state Tick allocates nothing.
namespace loom
{
    class DiagnosticSink;

    // One resource row. Future is the graph's own create, or the last Supply; it is empty for an import that nothing
    // supplied yet. Reported holds the epoch whose failure (LM701, LM702) the sink already has, so each is reported once.
    struct RowRuntime
    {
        rhi::ResourceFuture<GraphicsResource> Future;
        bool Owned{ false };
        bool KindMismatch{ false };     // LM701 for this epoch: the passes treat the row as Failed
        uint32_t Reported{ 0u };
    };

    // The WebGPU objects of one pipeline. The layouts are one per group, 0 to the highest group the pipeline uses.
    struct PipelineRuntime
    {
        wgpu::ShaderModule Module;
        std::vector<wgpu::BindGroupLayout> GroupLayouts;
        wgpu::PipelineLayout Layout;
        wgpu::ComputePipeline Pipeline;
    };

    // One pass's cached bind groups, one set of groups per ring phase (spec R3): a pass that binds history rings has
    // Phases = the least common multiple of their copy counts, else 1. With phase = frame_index % Phases:
    // BindGroups[phase * GroupCount + group], and WrittenEpochs[phase * SlotCount + slot] is the epoch of the slot's
    // row when that group was last built; 0 means never. ParamsOffset and ParamsSize are the pass's slice of the
    // shared params buffer, and 0 and 0 for a pass with no constants block.
    struct PassRuntime
    {
        int32_t GroupCount{ 0 };
        int32_t Phases{ 1 };
        uint64_t ParamsOffset{ 0u };
        uint64_t ParamsSize{ 0u };
        std::vector<wgpu::BindGroup> BindGroups;
        std::vector<uint32_t> WrittenEpochs;
    };

    struct GraphImpl;

    // The object of one copy (CompiledData::CopyRows): a row's first copy is the row's own future.
    [[nodiscard]] const rhi::ResourceFuture<GraphicsResource>& FutureOfCopy(const GraphImpl& graph, int32_t copy) noexcept;

    struct GraphImpl
    {
        GraphImpl(CompiledGraph&& _compiled, const RealizeInputs& inputs);
        ~GraphImpl();

        CompiledGraph Compiled;
        ResourceContext& Resources;
        DiagnosticSink& Sink;
        wgpu::Device Device;
        wgpu::Queue Queue;

        std::vector<RowRuntime> Rows;
        // the history copies after each row's first: ExtraCopies[copy - Rows.size()] (CompiledData::ExtraCopyBase)
        std::vector<rhi::ResourceFuture<GraphicsResource>> ExtraCopies;
        std::vector<PipelineRuntime> Pipelines;
        std::vector<PassRuntime> Passes;
        // one buffer for the constants of every pass that takes them in a uniform buffer
        wgpu::Buffer ParamsBuffer;
        int64_t FrameIndex{ 0 };

        // the core's state. States[row] is what the last poll saw. Sync has one entry per copy: here it only tells
        // whether a copy has contents, since Dawn tracks hazards and layouts.
        std::vector<ResourceState> States;
        std::vector<ResourceSync> Sync;
        std::vector<PassControl> Controls;
        ParamsStore Params;
        FramePlan Plan;

        // Tick's scratch, sized at Realize
        std::vector<wgpu::BindGroupEntry> EntryScratch;
        std::vector<std::byte> ParamsScratch;
        std::vector<std::byte> ZeroScratch;
    };

}

#endif // !DIAMOND_DOGS_LOOM_WEBGPU_GRAPH_IMPL_HPP
