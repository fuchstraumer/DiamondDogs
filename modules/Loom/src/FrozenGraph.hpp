#pragma once
#ifndef DIAMOND_DOGS_LOOM_FROZEN_GRAPH_HPP
#define DIAMOND_DOGS_LOOM_FROZEN_GRAPH_HPP
#include "Declarations.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomKeys.hpp"
#include "Records.hpp"
#include <cstdint>
#include <optional>
#include <source_location>
#include <string>
#include <vector>

// Freeze's output, and the Resolve stage's input. Freeze reads a GraphRecord, checks that it refers to itself
// correctly (LM101-LM109), and writes this. Rules for everything here:
//  - Every key a record named is resolved to an index into one of the tables below. Later stages compare integers,
//    never names or hashes.
//  - The tables own copies of what they need. A FrozenGraph does not point into the GraphRecord, so the builder can
//    be destroyed after Freeze.
//  - Statements that the records held as vectors so Freeze could count them (shader, dispatch) are single values here:
//    Freeze has checked that there was exactly one.
//  - Freeze does not order anything. An anchor cycle is the Order stage's error (LM501), not Freeze's.
namespace loom
{

    // An index into one table of a FrozenGraph. The tag stops a pass index from being used as a resource index.
    template<typename Tag>
    struct FrozenIndex
    {
        int32_t Value{ -1 };

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return Value >= 0;
        }

        constexpr bool operator==(const FrozenIndex&) const noexcept = default;
    };

    using ResourceIndex = FrozenIndex<struct ResourceIndexTag>;
    using FrameTargetIndex = FrozenIndex<struct FrameTargetIndexTag>;
    using AnchorIndex = FrozenIndex<struct AnchorIndexTag>;
    using PassIndex = FrozenIndex<struct PassIndexTag>;
    using SequenceIndex = FrozenIndex<struct SequenceIndexTag>;

    // Freeze puts the two built-in anchors first, so every graph has them at these indices.
    inline constexpr AnchorIndex k_BeginFrameAnchorIndex{ 0 };
    inline constexpr AnchorIndex k_EndFrameAnchorIndex{ 1 };

    // Resources

    enum class ResourceOrigin : uint8_t
    {
        Invalid = 0,
        Declared = 1,       // Declare(Buffer, Image or Sampler declaration): the graph creates it
        Imported = 2        // Import(ImportDeclaration): a future supplies it at Realize
    };

    // One row for every resource key, whatever its kind, so a bind needs one index only. DeclarationIndex points into
    // the table that Kind and Origin select: Buffers, Images, Samplers or Imports.
    struct FrozenResource
    {
        KeyRef Key;
        ResourceKind Kind{ ResourceKind::Invalid };
        ResourceOrigin Origin{ ResourceOrigin::Invalid };
        int32_t DeclarationIndex{ -1 };
        std::source_location Where;
    };

    struct FrozenImage
    {
        ImageDeclaration Declaration;
        FrameTargetIndex ExtentTarget;      // valid only when Declaration.Extent.Kind is Target
    };

    struct FrozenFrameTarget
    {
        KeyRef Key;
        std::source_location Where;
    };

    struct FrozenOutput
    {
        FrameTargetIndex Target;
        OutputFallback OnSkip;
        std::source_location Where;
    };

    // Anchors

    enum class AnchorOrigin : uint8_t
    {
        Invalid = 0,
        Builtin = 1,        // BeginFrameAnchor and EndFrameAnchor
        Declared = 2,       // GraphBuilder::DeclareAnchor
        Exposed = 3         // SequenceBuilder::ExposeAnchor
    };

    struct FrozenAnchor
    {
        KeyRef Key;
        AnchorOrigin Origin{ AnchorOrigin::Invalid };

        // Exposed anchors only: the sequence, and the anchor's position in it (see ExposedAnchorRecord::Position).
        SequenceIndex Sequence;
        int32_t Position{ -1 };

        std::source_location Where;         // empty for a built-in anchor
    };

    struct FrozenAnchorUse
    {
        AnchorIndex Anchor;
        std::source_location Where;
    };

    struct FrozenAnchorOrder
    {
        AnchorIndex Earlier;
        AnchorIndex Later;
        std::source_location Where;
    };

    // Passes

    struct FrozenBind
    {
        std::string BindingName;
        ResourceIndex Resource;
        ResourceKind Kind;
        BindNarrowing Narrowing;
        bool Previous{ false };
        std::source_location Where;
    };

    struct FrozenDispatch
    {
        DispatchForm Form;
        ResourceIndex Resource;             // ThreadsOver: the image. Indirect: the argument buffer. Threads: invalid.
        int32_t X{ -1 };
        int32_t Y{ -1 };
        int32_t Z{ -1 };
        std::source_location Where;
    };

    // Step 7 adds the draw statement and the raster state.
    struct FrozenPass
    {
        KeyRef Key;
        PassKind Kind;
        RunPolicy Policy;
        QueueAffinity Affinity;
        std::source_location Where;

        // Freeze sorts the passes by declaration order, so a pass's index in FrozenGraph::Passes equals its
        // DeclarationOrder. The Order stage breaks ties with it (Q3c).
        int32_t DeclarationOrder{ -1 };

        // A pass inside a sequence: the sequence, and the pass's place in it. A top-level pass: invalid and -1.
        SequenceIndex Sequence;
        int32_t PositionInSequence{ -1 };

        ShaderRecord Shader;
        std::vector<PinRecord> Pins;
        std::vector<FrozenAnchorUse> After;
        std::vector<FrozenAnchorUse> Before;
        std::vector<FrozenBind> Binds;
        std::vector<ParamsRecord> Params;
        FrozenDispatch Dispatch;
    };

    // A sequence's passes are contiguous in FrozenGraph::Passes, in source order, because the builder gives them
    // consecutive declaration orders when the sequence commits.
    struct FrozenSequence
    {
        KeyRef Key;
        std::source_location Where;

        std::vector<FrozenAnchorUse> After;
        std::vector<FrozenAnchorUse> Before;
        PassIndex FirstPass;
        int32_t PassCount{ -1 };
        std::vector<AnchorIndex> ExposedAnchors;
    };

    struct FrozenGraph
    {
        std::string Name;
        std::string ShaderLibraryName;

        std::vector<PinRecord> Pins;        // graph pins; Freeze has checked that no axis is pinned twice (LM108)

        std::vector<FrozenResource> Resources;
        std::vector<BufferDeclaration> Buffers;
        std::vector<FrozenImage> Images;
        std::vector<SamplerDeclaration> Samplers;
        std::vector<ImportDeclaration> Imports;
        std::vector<FrozenFrameTarget> FrameTargets;
        std::vector<FrozenOutput> Outputs;

        std::vector<FrozenAnchor> Anchors;  // the two built-in anchors first
        std::vector<FrozenAnchorOrder> AnchorOrders;

        std::vector<FrozenPass> Passes;     // every pass, top-level and nested, in declaration order
        std::vector<FrozenSequence> Sequences;
    };

    // Reports every Freeze error it finds, not only the first one. Returns no graph when it reported an error.
    [[nodiscard]] std::optional<FrozenGraph> Freeze(const GraphRecord& record, DiagnosticSink& sink);

}

#endif // !DIAMOND_DOGS_LOOM_FROZEN_GRAPH_HPP
