#pragma once
#ifndef DIAMOND_DOGS_LOOM_COMPILED_DATA_HPP
#define DIAMOND_DOGS_LOOM_COMPILED_DATA_HPP
#include "FramePlan.hpp"
#include "LoomKeys.hpp"
#include "PassStatements.hpp"
#include "ShaderLibraryTypes.hpp"
#include "bind/BoundGraph.hpp"
#include "freeze/FrozenGraph.hpp"
#include "layout/LaidOutGraph.hpp"
#include "order/OrderedGraph.hpp"
#include "size/SizedGraph.hpp"
#include <cstdint>
#include <string>
#include <vector>

// What Compile hands to a backend. Every table owns its data: no view into the bundle and no Lodestone view type, so
// the bundle can close (G4). The stage outputs keep their own indices, and the tables added here are indexed the same
// way: by pass (FrozenGraph::Passes), by resource row (FrozenGraph::Resources), or by pipeline (LaidOutGraph::Pipelines).
namespace loom
{

    // One stage of a pipeline, with its own copy of the code.
    struct CompiledStage
    {
        lodestone::ShaderStageKind Stage{ lodestone::ShaderStageKind::Invalid };
        std::string EntryPoint;
        std::vector<uint32_t> Code;     // SPIR-V words when the profile's code format is Spirv
    };

    // One pipeline: its stages, its workgroup, its push constant range, and the pass whose slots give its descriptor
    // layout. Passes that share a pipeline (Layout's == on PipelineDescription) share the variant, so their layouts
    // are the same.
    struct CompiledPipeline
    {
        std::vector<CompiledStage> Stages;
        lodestone::WorkgroupSize Workgroup;
        lodestone::ShaderStageKind PushConstantStages{ lodestone::ShaderStageKind::Invalid };
        uint32_t PushConstantBytes{ 0u };
        int32_t LayoutPass{ -1 };
    };

    // One pass's use of one resource row, merged over its slots and statements. Scope is what the barriers need.
    // Initializes is true for a write with no read (Q3g: a skipped initializing writer skips its readers).
    struct PassUse
    {
        int32_t Resource{ -1 };
        ResourceAccess Access{ ResourceAccess::Invalid };
        SyncScope Scope;
        bool Initializes{ false };
    };

    // A Params statement's key, with the size and alignment of its type (LM704), and the bytes the shader takes.
    struct CompiledParams
    {
        KeyRef Key;
        uint32_t ByteSize{ 0u };
        uint32_t Alignment{ 0u };
    };

    struct CompiledPass
    {
        int32_t FirstUse{ 0 };              // Uses[FirstUse, FirstUse + UseCount)
        int32_t UseCount{ 0 };
        int32_t Params{ -1 };               // an index into CompiledData::Params, or -1
        bool ReadsHistory{ false };         // a BindPrevious; history rings come in step 6
    };

    // The levels where a resource row is first and last used, for the aliasing stage after step 6. -1 when no pass
    // uses the row.
    struct ResourceSpan
    {
        int32_t FirstLevel{ -1 };
        int32_t LastLevel{ -1 };
    };

    struct CompiledData
    {
        lodestone::ShaderCodeFormat CodeFormat{ lodestone::ShaderCodeFormat::None };

        FrozenGraph Frozen;
        OrderedGraph Ordered;
        SizedGraph Sized;
        LaidOutGraph LaidOut;

        // Bind's slot table without its sources (they view into the bundle). SlotWritable is the reflected access,
        // before narrowing: a storage texel buffer narrowed to ReadOnly is still a storage texel buffer.
        std::vector<BindingSlot> Slots;
        std::vector<uint8_t> SlotWritable;
        std::vector<BoundPass> BoundPasses;

        std::vector<CompiledPipeline> Pipelines;    // [pipeline], as LaidOut.Pipelines
        std::vector<CompiledPass> Passes;           // [pass]
        std::vector<PassUse> Uses;
        std::vector<CompiledParams> Params;
        std::vector<ResourceSpan> Spans;            // [resource]
    };

}

#endif // !DIAMOND_DOGS_LOOM_COMPILED_DATA_HPP
