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
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
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
        std::vector<std::byte> ShaderCode;

        struct CodeCompat
        {
            [[nodiscard]] bool empty() const noexcept
            {
                const auto* parent = reinterpret_cast<const CompiledStage*>(
                    reinterpret_cast<const std::byte*>(this) - offsetof(CompiledStage, Code));
                return parent->ShaderCode.empty();
            }

            [[nodiscard]] size_t size() const noexcept
            {
                const auto* parent = reinterpret_cast<const CompiledStage*>(
                    reinterpret_cast<const std::byte*>(this) - offsetof(CompiledStage, Code));
                return parent->ShaderCode.size();
            }
        };
        CodeCompat Code{};
    };

    [[nodiscard]] std::span<const uint32_t> SpirvWords(const CompiledStage& stage) noexcept;
    [[nodiscard]] std::string_view WgslSource(const CompiledStage& stage) noexcept;

    // One pipeline: its stages, its workgroup, its push constant range, and the pass whose slots give its descriptor
    // layout. Passes that share a pipeline (Layout's == on PipelineDescription) share the variant, so their layouts
    // are the same.
    struct CompiledPipeline
    {
        std::vector<CompiledStage> Stages;
        lodestone::WorkgroupSize Workgroup;
        lodestone::ShaderStageKind PushConstantStages{ lodestone::ShaderStageKind::Invalid };
        uint32_t PushConstantBytes{ 0u };
        lodestone::EntryPointConstantsDelivery ConstantsDelivery{ lodestone::EntryPointConstantsDelivery::None };
        uint32_t ConstantsGroup{ 0u };      // UniformBuffer delivery: where the constants block binds
        uint32_t ConstantsBinding{ 0u };
        uint32_t ConstantsBytes{ 0u };
        int32_t LayoutPass{ -1 };
    };

    // One pass's use of one resource row, merged over its slots and statements. Scope is what the barriers need.
    // Initializes is true for a write with no read (Q3g: a skipped initializing writer skips its readers).
    struct PassUse
    {
        int32_t Resource{ -1 };
        ResourceAccess Access{ ResourceAccess::Invalid };
        SyncScope Scope;
        rhi::ImageLayout Layout{ rhi::ImageLayout::Undefined };   // images: the layout this use needs; buffers: Undefined
        bool Initializes{ false };
        bool Previous{ false };     // BindPrevious: reads last frame's copy, never this frame's
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
        std::vector<rhi::ImageLayout> SlotLayouts;  // [slot]: the layout a descriptor names, the use's layout

        // History rings (spec R3): a row with HistoryDepth D has D + 1 copies, each its own object with its own barrier
        // state. Copy `row` is each row's first copy, so a row with no history is its own copy; the other copies of a
        // ring follow the rows, from ExtraCopyBase[row]. CopyRows[copy] is the copy's row.
        std::vector<int32_t> CopyCounts;            // [resource]: 1, or HistoryDepth + 1
        std::vector<int32_t> ExtraCopyBase;         // [resource]: -1 for a row with one copy
        std::vector<int32_t> CopyRows;              // [copy]
    };

    // The copy that frame `frame_index` writes (previous false) or reads through BindPrevious (previous true).
    [[nodiscard]] inline int32_t CopyOf(const CompiledData& data, int32_t row, int64_t frame_index, bool previous) noexcept
    {
        const int64_t count = data.CopyCounts[row];
        const int64_t frame = previous ? frame_index - 1 : frame_index;
        const int64_t ring = ((frame % count) + count) % count;
        return ring == 0 ? row : data.ExtraCopyBase[row] + static_cast<int32_t>(ring) - 1;
    }

}

#endif // !DIAMOND_DOGS_LOOM_COMPILED_DATA_HPP
