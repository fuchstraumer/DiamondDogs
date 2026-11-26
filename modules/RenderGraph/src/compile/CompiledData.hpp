#pragma once
#ifndef DIAMOND_DOGS_LOOM_COMPILED_DATA_HPP
#define DIAMOND_DOGS_LOOM_COMPILED_DATA_HPP
#include "AliasLayout.hpp"
#include "FramePlan.hpp"
#include "Declarations.hpp"
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
        bool FrameStart{ false };   // BindFrameStart: every read of this use runs before this frame's writers
    };

    // A Params statement's key, with the size and alignment of its type (LM704), and the bytes the shader takes.
    struct CompiledParams
    {
        KeyRef Key;
        uint32_t ByteSize{ 0u };
        uint32_t Alignment{ 0u };
    };

    // What an attachment is to its pass (step 7 Q3). One table in Uses.cpp gives each role its layout, stages and
    // accesses, and each backend maps the roles to its API in one switch. Stencil, Resolve, ShadingRate and
    // InputAttachment come later as new values.
    enum class AttachmentRole : uint8_t
    {
        Invalid = 0,
        Color = 1,
        Depth = 2
    };

    enum class AttachmentLoad : uint8_t
    {
        Invalid = 0,
        Keep = 1,
        Clear = 2,
        DontCare = 3
    };

    // One attachment of a Draw pass, or of a Record pass that declares target uses (Loom begins rendering for it with
    // Keep and Store). Row is a resource row or a frame-target row (CompiledData::RowCount).
    struct CompiledAttachment
    {
        AttachmentRole Role{ AttachmentRole::Invalid };
        int32_t Row{ -1 };
        int32_t Slot{ -1 };                 // Color: the color slot. Depth: -1.
        AttachmentLoad Load{ AttachmentLoad::Invalid };
        AttachmentStore Store{ AttachmentStore::Invalid };
        math::Float4 ClearValue{ 0.0f, 0.0f, 0.0f, 0.0f };     // Clear only: the color, or the depth in x
    };

    struct CompiledPass
    {
        PassKind Kind{ PassKind::Invalid };
        int32_t FirstUse{ 0 };              // Uses[FirstUse, FirstUse + UseCount)
        int32_t UseCount{ 0 };
        int32_t FirstAttachment{ 0 };       // Attachments[FirstAttachment, FirstAttachment + AttachmentCount)
        int32_t AttachmentCount{ 0 };
        int32_t Params{ -1 };               // an index into CompiledData::Params, or -1
        RunPolicy Policy{ RunPolicy::Invalid };
        bool ReadsHistory{ false };         // a BindPrevious; history rings come in step 6
    };

    // An imported frame target. Its row follows the resource rows. Fallback is the Output declaration's; a target with
    // no Output keeps its last frame (no acquire, no present) when nothing writes it.
    struct CompiledFrameTarget
    {
        int32_t Row{ -1 };
        OutputFallback Fallback{ OutputFallback::KeepLast() };
    };

    // The levels where a resource row is first and last used, for aliasing (alias/Lifetimes.cpp). -1 when no pass
    // uses the row.
    struct ResourceSpan
    {
        int32_t FirstLevel{ -1 };
        int32_t LastLevel{ -1 };
    };

    struct CompiledData
    {
        lodestone::ShaderCodeFormat CodeFormat{ lodestone::ShaderCodeFormat::None };
        // if the graph contains passes that run when inputs to them change, this flag is true:
        // otherwise, it is false as it saves us quite a bit of work and overhead
        bool HasWhenInputsChangesPasses{ false };
        FrozenGraph Frozen;
        OrderedGraph Ordered;
        SizedGraph Sized;
        LaidOutGraph LaidOut;

        // Bind's slot table without its sources (they view into the bundle). SlotWritable is the reflected access,
        // before narrowing: a storage texel buffer narrowed to ReadOnly is still a storage texel buffer.
        std::vector<BindingSlot> Slots;
        std::vector<uint8_t> SlotWritable;
        std::vector<BoundPass> BoundPasses;

        // Rows: the resource rows (FrozenGraph::Resources), then one row per frame target (FrameTargets[t].Row). Every
        // table indexed [row] has RowCount entries; the history copies follow them (CopyRows).
        int32_t RowCount{ 0 };
        std::vector<CompiledFrameTarget> FrameTargets;  // [frame target], as FrozenGraph::FrameTargets

        std::vector<CompiledPipeline> Pipelines;    // [pipeline], as LaidOut.Pipelines
        std::vector<CompiledPass> Passes;           // [pass]
        std::vector<PassUse> Uses;
        std::vector<CompiledAttachment> Attachments;
        std::vector<CompiledParams> Params;
        std::vector<ResourceSpan> Spans;            // [resource]
        std::vector<AliasCandidate> AliasCandidates;    // the rows that may share memory (alias/Lifetimes.cpp)
        std::vector<rhi::ImageLayout> SlotLayouts;  // [slot]: the layout a descriptor names, the use's layout

        // History rings (spec R3): a row with HistoryDepth D has D + 1 copies, each its own object with its own barrier
        // state. Copy `row` is each row's first copy, so a row with no history is its own copy; the other copies of a
        // ring follow the rows, from ExtraCopyBase[row]. CopyRows[copy] is the copy's row.
        std::vector<int32_t> CopyCounts;            // [row]: 1, or HistoryDepth + 1
        std::vector<int32_t> ExtraCopyBase;         // [row]: -1 for a row with one copy
        std::vector<int32_t> CopyRows;              // [copy]

        std::vector<uint64_t> PassHashes; // [pass]
        std::vector<uint64_t> ResourceHashes; // [resource], resource rows only
    };

    // A copy index past RowCount is a history ring's extra copy, not a frame target
    [[nodiscard]] inline bool IsFrameTargetRow(const CompiledData& data, int32_t row) noexcept
    {
        return row >= static_cast<int32_t>(data.Frozen.Resources.size()) && row < data.RowCount;
    }

    // The frame target of a frame-target row
    [[nodiscard]] inline int32_t FrameTargetOfRow(const CompiledData& data, int32_t row) noexcept
    {
        return row - static_cast<int32_t>(data.Frozen.Resources.size());
    }

    [[nodiscard]] inline int32_t RowOfFrameTarget(const CompiledData& data, FrameTargetIndex target) noexcept
    {
        return static_cast<int32_t>(data.Frozen.Resources.size()) + target.Value;
    }

    // A frame target is an image row too
    [[nodiscard]] inline bool IsImageRow(const CompiledData& data, int32_t row) noexcept
    {
        return IsFrameTargetRow(data, row) || data.Frozen.Resources[row].Kind == ResourceKind::Image;
    }

    // The key of any row, for the explain text
    [[nodiscard]] inline KeyRef KeyOfRow(const CompiledData& data, int32_t row) noexcept
    {
        if (IsFrameTargetRow(data, row))
        {
            return data.Frozen.FrameTargets[FrameTargetOfRow(data, row)].Key;
        }

        return data.Frozen.Resources[row].Key;
    }

    // The copy that frame `frame_index` writes (previous false) or reads through BindPrevious (previous true).
    [[nodiscard]] inline int32_t CopyOf(const CompiledData& data, int32_t row, int64_t frame_index, bool previous) noexcept
    {
        const int64_t count = data.CopyCounts[row];
        if (count == 1)
        {
            return row;
        }

        const int64_t frame = previous ? frame_index - 1 : frame_index;
        int64_t ring = frame % count;
        if (ring < 0)
        {
            ring += count;
        }

        return ring == 0 ? row : data.ExtraCopyBase[row] + static_cast<int32_t>(ring) - 1;
    }

}

#endif // !DIAMOND_DOGS_LOOM_COMPILED_DATA_HPP
