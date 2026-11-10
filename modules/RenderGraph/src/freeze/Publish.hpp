#pragma once
#ifndef DIAMOND_DOGS_LOOM_FREEZE_PUBLISH_HPP
#define DIAMOND_DOGS_LOOM_FREEZE_PUBLISH_HPP
#include "Freeze.hpp"
#include "Declarations.hpp"
#include "FrozenGraph.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "LoomKeys.hpp"
#include "Records.hpp"
#include <cstdint>
#include <ranges>
#include <source_location>
#include <utility>
#include <algorithm>
#include <vector>

// Step 1: enter every name into the key table (LM102, LM107), and create the rows that later steps fill in.
namespace loom::freeze
{

    inline bool Publish(FreezeContext& context, KeyRef key, KeyClass key_class, int32_t index, std::source_location where)
    {
        // initial publish, checking just the key first
        const auto found = context.Keys.find(key.Hash);
        if (found == context.Keys.end())
        {
            context.Keys.emplace(key.Hash, KeyEntry{ .Class = key_class, .Index = index, .Where = where });
            return true;
        }

        const bool duplicateNameOfClass = found->second.Class == key_class;
        // either duplicate name of the same class, or collision of having name assigned twice to different
        // types
        DiagnosticCode errorCode = duplicateNameOfClass ? DiagnosticCode::LM102 : DiagnosticCode::LM107;
        Diagnostic diag = MakeDiagnostic(errorCode, where);
        diag.OtherWhere = found->second.Where;
        diag.Resource = key;
        context.Sink.Report(diag);
        return false;
    }

    inline void PublishBuiltinAnchor(FreezeContext& context, AnchorKey anchor)
    {
        const int32_t anchorIndex = static_cast<int32_t>(context.Graph.Anchors.size());

        // A built-in has no call site, so a caller's DeclareAnchor of the same name reports against this one.
        if (!Publish(context, anchor.ToRef(), KeyClass::Anchor, anchorIndex, std::source_location{}))
        {
            return;
        }

        context.Graph.Anchors.emplace_back(anchor.ToRef(),
                                           AnchorOrigin::Builtin,
                                           SequenceIndex{},
                                           -1,
                                           std::source_location{});
    }

    // Runs first, on an empty graph, so the two anchors land at k_BeginFrameAnchorIndex and
    // k_EndFrameAnchorIndex.
    inline void PublishBuiltinAnchors(FreezeContext& context)
    {
        PublishBuiltinAnchor(context, BeginFrameAnchor);
        PublishBuiltinAnchor(context, EndFrameAnchor);
    }

    inline void PublishBuffers(FreezeContext& context)
    {
        for (const DeclarationRecord<BufferDeclaration>& decl : context.Record.Buffers)
        {
            const int32_t resourceIndex = static_cast<int32_t>(context.Graph.Resources.size());
            const KeyRef key = decl.Value.Key.ToRef();
            if (!Publish(context, key, KeyClass::Buffer, resourceIndex, decl.Where))
            {
                continue; // don't publish: diag sink on the context will have already reported the issue
            }

            FrozenResource resource
            {
                .Key=key,
                .Kind=ResourceKind::Buffer,
                .Origin=ResourceOrigin::Declared,
                .DeclarationIndex=static_cast<int32_t>(context.Graph.Buffers.size()),
                .Where=decl.Where
            };

            context.Graph.Resources.emplace_back(resource);
            context.Graph.Buffers.emplace_back(decl.Value);
        }
    }

    inline void PublishImages(FreezeContext& context)
    {
        for (const DeclarationRecord<ImageDeclaration>& decl : context.Record.Images)
        {
            const int32_t resourceIndex = static_cast<int32_t>(context.Graph.Resources.size());
            const KeyRef key = decl.Value.Key.ToRef();
            if (!Publish(context, key, KeyClass::Image, resourceIndex, decl.Where))
            {
                continue;
            }

            FrozenResource resource
            {
                .Key=key,
                .Kind=ResourceKind::Image,
                .Origin=ResourceOrigin::Declared,
                .DeclarationIndex=static_cast<int32_t>(context.Graph.Images.size()),
                .Where=decl.Where
            };

            context.Graph.Resources.emplace_back(resource);
            
            FrozenImage image
            {
                .Declaration=decl.Value,
                .ExtentTarget={}, // can't set extent target during the publish pass - leave it out
                .Where=decl.Where // copy Where to avoid walking the whole rsrc table to resolve this later
            };
            context.Graph.Images.emplace_back(image);
        }
    }

    inline void PublishSamplers(FreezeContext& context)
    {
        for (const DeclarationRecord<SamplerDeclaration>& decl : context.Record.Samplers)
        {
            const int32_t resourceIndex = static_cast<int32_t>(context.Graph.Resources.size());
            const KeyRef key = decl.Value.Key.ToRef();
            if (!Publish(context, key, KeyClass::Sampler, resourceIndex, decl.Where))
            {
                continue;
            }

            FrozenResource resource
            {
                .Key=key,
                .Kind=ResourceKind::Sampler,
                .Origin=ResourceOrigin::Declared,
                .DeclarationIndex=static_cast<int32_t>(context.Graph.Samplers.size()),
                .Where=decl.Where
            };

            context.Graph.Resources.emplace_back(resource);
            context.Graph.Samplers.emplace_back(decl.Value);
        }
    }

    inline void PublishImports(FreezeContext& context)
    {
        for (const DeclarationRecord<ImportDeclaration>& decl : context.Record.Imports)
        {
            const int32_t resourceIndex = static_cast<int32_t>(context.Graph.Resources.size());
            const KeyRef key = decl.Value.Key.ToRef();
            if (!Publish(context, key, KeyClass::Image, resourceIndex, decl.Where))
            {
                continue;
            }

            // Only images can be imported today; ImportDeclaration's key is a ResourceKey<Image>.
            FrozenResource resource
            {
                .Key=key,
                .Kind=ResourceKind::Image,
                .Origin=ResourceOrigin::Imported,
                .DeclarationIndex=static_cast<int32_t>(context.Graph.Imports.size()),
                .Where=decl.Where
            };

            context.Graph.Resources.emplace_back(resource);
            context.Graph.Imports.emplace_back(decl.Value);
        }
    }

    inline void PublishAnchors(FreezeContext& context)
    {
        for (const AnchorDeclarationRecord& decl : context.Record.Anchors)
        {
            const int32_t anchorIndex = static_cast<int32_t>(context.Graph.Anchors.size());

            if (!Publish(context, decl.Anchor, KeyClass::Anchor, anchorIndex, decl.Where))
            {
                continue;
            }

        context.Graph.Anchors.emplace_back(
            decl.Anchor, AnchorOrigin::Declared, SequenceIndex{}, -1, decl.Where);
        }
    }

    inline void PublishFrameTargets(FreezeContext& context)
    {
        for (const FrameTargetImportRecord& decl : context.Record.FrameTargets)
        {
            const int32_t targetIndex = static_cast<int32_t>(context.Graph.FrameTargets.size());

            if (!Publish(context, decl.Target, KeyClass::FrameTarget, targetIndex, decl.Where))
            {
                continue;
            }

            context.Graph.FrameTargets.emplace_back(decl.Target, decl.Where);
        }
    }

    // An exposed anchor belongs to its sequence, so it is published from the sequence walk, not with the
    // others.
    inline void PublishExposedAnchors(FreezeContext& context,
                               const SequenceRecord& sequence_record,
                               SequenceIndex sequence_index,
                               FrozenSequence& sequence)
    {
        for (const ExposedAnchorRecord& exposed : sequence_record.ExposedAnchors)
        {
            const AnchorIndex anchorIndex{ static_cast<int32_t>(context.Graph.Anchors.size()) };

            if (!Publish(context, exposed.Anchor, KeyClass::Anchor, anchorIndex.Value, exposed.Where))
            {
                continue;
            }

        context.Graph.Anchors.emplace_back(
            exposed.Anchor, AnchorOrigin::Exposed, sequence_index, exposed.Position, exposed.Where);
            sequence.ExposedAnchors.push_back(anchorIndex);
        }
    }

    // FirstPass, PassCount and the anchor uses are filled in after the passes are flattened.
    inline void PublishSequences(FreezeContext& context)
    {
        for (const SequenceRecord& decl : context.Record.Sequences)
        {
            const SequenceIndex sequenceIndex{ static_cast<int32_t>(context.Graph.Sequences.size()) };

            if (!Publish(context, decl.Key, KeyClass::Sequence, sequenceIndex.Value, decl.Where))
            {
                continue;
            }

            FrozenSequence sequence{};
            sequence.Key = decl.Key;
            sequence.Where = decl.Where;

            PublishExposedAnchors(context, decl, sequenceIndex, sequence);
            context.Graph.Sequences.push_back(std::move(sequence));
        }
    }


    constexpr bool EarlierDeclared(const PassSource& left, const PassSource& right) noexcept
    {
        return left.Pass->DeclarationOrder < right.Pass->DeclarationOrder;
    }

    inline std::vector<PassSource> CollectPasses(const GraphRecord& record)
    {
        // Collect all top-level passes first
        auto passSrcFromRecord =
        [&](const PassRecord& pass) -> PassSource
        {
            return PassSource{ .Pass = &pass, .Sequence = {}, .PositionInSequence = -1 };
        };

        std::vector<PassSource> sources = std::views::transform(record.Passes, passSrcFromRecord) |
                                            std::ranges::to<std::vector<PassSource>>();
        
        // Then, collect the nested passes - those within a sequence. Each nested pass is associated with it's
        // sequence via the index of the sequence within the GraphRecord::Sequences vector. This allows us to
        // later reconstruct ordering, as we also give each nested pass its position within the sequence. From
        // this, we will sort all passes on the declaration order, which has been set by `GraphBuilder` when
        // commit dispatches or sequences. (and we get that bc each `PassSource` contains a pointer to the
        // original `PassRecord`, which has its `DeclarationOrder` set correctly)
        for (int32_t sequence = 0; sequence < std::ssize(record.Sequences); ++sequence)
        {
            const std::vector<PassRecord>& passes = record.Sequences[sequence].Passes;

            for (int32_t position = 0; position < std::ssize(passes); ++position)
            {
                sources.push_back(PassSource{ .Pass = &passes[position],
                                                .Sequence = SequenceIndex{ sequence },
                                                .PositionInSequence = position });
            }
        }

        std::ranges::sort(sources, EarlierDeclared);
        return sources;
    }

    // A pass's index is its place in the sorted list, which is its DeclarationOrder. The FrozenPass rows are
    // written later, one per source in this same order, so the indices published here stay true.
    inline void PublishPasses(FreezeContext& context, const std::vector<PassSource>& sources)
    {
        for (int32_t passIndex = 0; passIndex < std::ssize(sources); ++passIndex)
        {
            const PassRecord& pass = *sources[passIndex].Pass;
            Publish(context, pass.Key, KeyClass::Pass, passIndex, pass.Where);
        }
    }

}

#endif // !DIAMOND_DOGS_LOOM_FREEZE_PUBLISH_HPP
