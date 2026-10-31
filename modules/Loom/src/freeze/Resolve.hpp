#pragma once
#ifndef DIAMOND_DOGS_LOOM_FREEZE_RESOLVE_HPP
#define DIAMOND_DOGS_LOOM_FREEZE_RESOLVE_HPP
#include "Freeze.hpp"
#include "FrozenGraph.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "LoomKeys.hpp"
#include "Records.hpp"
#include <cstdint>
#include <ranges>
#include <source_location>
#include <unordered_map>
#include <vector>

// Step 3: replace every name a record uses with an index into the frozen tables (LM103, LM104, LM107).
namespace loom::freeze
{

    inline int32_t ResolveKey(FreezeContext& context, KeyRef key, KeyClass expected_kind, std::source_location where)
    {
        const auto found = context.Keys.find(key.Hash);

        if (found == context.Keys.end())
        {
            DiagnosticCode missingCode =
                (expected_kind == KeyClass::Anchor) ? DiagnosticCode::LM104 : DiagnosticCode::LM103;
            Diagnostic diag = MakeDiagnostic(missingCode, where);
            diag.Resource = key;
            context.Sink.Report(diag);
            return -1; // return an invalid index
        }

        if (found->second.Class != expected_kind)
        {
            Diagnostic diag = MakeDiagnostic(DiagnosticCode::LM107, where);
            diag.OtherWhere = found->second.Where;
            diag.Resource = key;
            context.Sink.Report(diag);
            return -1; // return an invalid index
        }

        return found->second.Index;
    }

    inline ResourceIndex ResolveResource(FreezeContext& context, KeyRef key, KeyClass expected_kind,
                                         std::source_location where)
    {
        return ResourceIndex{ ResolveKey(context, key, expected_kind, where) };
    }

    inline AnchorIndex ResolveAnchor(FreezeContext& context, KeyRef key, std::source_location where)
    {
        return AnchorIndex{ ResolveKey(context, key, KeyClass::Anchor, where) };
    }

    inline std::vector<FrozenAnchorUse> ResolveAnchorUses(FreezeContext& context, const std::vector<AnchorUseRecord>& uses)
    { 
        auto toFrozen = [&](const AnchorUseRecord& use) -> FrozenAnchorUse
        {
            return FrozenAnchorUse
            {
                .Anchor = ResolveAnchor(context, use.Anchor, use.Where),
                .Where = use.Where
            };
        };

        return uses |
               std::views::transform(toFrozen) |
               std::ranges::to<std::vector>();
    }

    // The bind's own Kind says which table its key must name, so a buffer key bound as an image is LM107.
    inline std::vector<FrozenBind> ResolveBinds(FreezeContext& context, const std::vector<BindRecord>& binds)
    {
        auto toFrozen = [&](const BindRecord& bind) -> FrozenBind
        {
            return FrozenBind
            {
                .BindingName = bind.BindingName,
                .Resource = ResolveResource(context, bind.Resource, ClassOf(bind.Kind), bind.Where),
                .Kind = bind.Kind,
                .Narrowing = bind.Narrowing,
                .Previous = bind.Previous,
                .Where = bind.Where
            };
        };

        return binds |
               std::views::transform(toFrozen) |
               std::ranges::to<std::vector>();
    }

    inline FrozenDispatch ResolveDispatch(FreezeContext& context, const DispatchRecord& record)
    {
        if (record.Form == DispatchForm::ThreadsOver)
        {
            // make sure key for resource to use for threads over resolves right
            ResourceIndex imageRsrc = ResolveResource(context, record.Resource, KeyClass::Image, record.Where);
            return FrozenDispatch
            {
                .Form = record.Form,
                .Resource = imageRsrc,
                .Where = record.Where
            };
        }
        else if (record.Form == DispatchForm::Indirect)
        {
            ResourceIndex bufferRsrc = ResolveResource(context, record.Resource, KeyClass::Buffer, record.Where);
            return FrozenDispatch
            {
                .Form = record.Form,
                .Resource = bufferRsrc,
                .Where = record.Where
            };
        }
        else
        {
            // invalid form caught already, so this last case is just default threads Form
            return FrozenDispatch
            {
                .Form = record.Form,
                .Resource = ResourceIndex{ -1 },
                .X = record.X,
                .Y = record.Y,
                .Z = record.Z,
                .Where = record.Where
            };
        }
    }

    inline FrozenPass ResolvePass(FreezeContext& context, const PassSource& source)
    {
        // Build the frozen pass starting now: 
        // copy Key,Kind,Policy,Where,Shader
        // set Sequence,PositionInSequence from PassSource
        // walk After, Before, Binds, and Dispatch
        FrozenPass frozenPass
        {
            .Key = source.Pass->Key,
            .Kind = source.Pass->Kind,
            .Policy = source.Pass->Policy,
            .Affinity = source.Pass->Affinity,
            .Where = source.Pass->Where,
            // declaration order (bc list is sorted) is just the current size of the passes list
            .DeclarationOrder = static_cast<int32_t>(context.Graph.Passes.size()),
            .Sequence = source.Sequence,
            .PositionInSequence = source.PositionInSequence,
            .Pins = source.Pass->Pins
        };

        if (source.Pass->Shaders.size() != 1)
        {
            Diagnostic diag = MakeDiagnostic(DiagnosticCode::LM105, source.Pass->Where);
            context.Sink.Report(diag);
        }
        else [[likely]]
        {
            frozenPass.Shader = source.Pass->Shaders.front();
        }

        // fairly straightforward: just convert the named records into indexed records
        frozenPass.Before = ResolveAnchorUses(context, source.Pass->Before);
        frozenPass.After = ResolveAnchorUses(context, source.Pass->After);
        frozenPass.Binds = ResolveBinds(context, source.Pass->Binds);
        frozenPass.Params = source.Pass->Params;

        const auto dispatchCount = std::ssize(source.Pass->Dispatches);
        const auto drawCount = std::ssize(source.Pass->Draws);
        const bool bothEmpty = (dispatchCount == 0 && drawCount == 0);
        const bool bothNonEmpty = (dispatchCount > 0 && drawCount > 0);

        // the first three errors all report two locations (which makes it way easier to track down srcloc)
        auto reportTwoLocError = [&context](const std::source_location& first, const std::source_location& second)
        {
            Diagnostic diag = MakeDiagnostic(DiagnosticCode::LM106, first);
            diag.OtherWhere = second;
            context.Sink.Report(diag);
        };

        // a pass cannot have both dispatches AND draws, nor should it be totally empty
        if (bothNonEmpty)
        {
            const DispatchRecord& dispatch = source.Pass->Dispatches.front();
            const DrawRecord& draw = source.Pass->Draws.front();
            reportTwoLocError(dispatch.Where, draw.Where);
        }
        else if (dispatchCount > 1)
        {
            // can only have one dispatch per pass, so report an error if there are multiple
            const DispatchRecord& firstDispatch = source.Pass->Dispatches[0];
            const DispatchRecord& nextDispatch = source.Pass->Dispatches[1];
            reportTwoLocError(firstDispatch.Where, nextDispatch.Where);
        }
        else if (drawCount > 1)
        {
            // can only have one draw per pass, so report an error if there are multiple
            const DrawRecord& firstDraw = source.Pass->Draws[0];
            const DrawRecord& nextDraw = source.Pass->Draws[1];
            reportTwoLocError(firstDraw.Where, nextDraw.Where);
        }
        else if (bothEmpty)
        {
            // a pass must have either a dispatch or a draw
            Diagnostic diag = MakeDiagnostic(DiagnosticCode::LM110, source.Pass->Where);
            context.Sink.Report(diag);
        }
        else if (dispatchCount == 1)
        {
            const DispatchRecord& dispatchRecord = source.Pass->Dispatches[0];
            frozenPass.Dispatch = ResolveDispatch(context, dispatchRecord);
        }
        else if (drawCount == 1)
        {
            // todo: add FrozenDraw struct
        }

        return frozenPass;
    }

    inline void ResolvePasses(FreezeContext& context, const std::vector<PassSource>& sources)
    {
        // sources has at least been sorted to be in declaration order
        context.Graph.Passes.reserve(sources.size());
        for (const auto& source : sources)
        {
            context.Graph.Passes.emplace_back(ResolvePass(context, source));
        }
    }

    inline void ResolveSequenceRanges(FreezeContext& context)
    {
        // With the passes resolved as best as we can, and the same being true for Sequences, we can now
        // set the FirstPass and PassCount for each sequence based on the resolved passes
        for (auto&& [passIdx, pass] : context.Graph.Passes | std::views::enumerate)
        {
            const SequenceIndex owner = pass.Sequence;
            if (!owner.IsValid())
            {
                continue;
            }

            // the sequences were already written in `PublishSequences`, where we said that all we need to do
            // is update `FirstPass` and `PassCount`... now's the time!
            FrozenSequence& sequence = context.Graph.Sequences[owner.Value];

            if (!sequence.FirstPass.IsValid())
            {
                sequence.FirstPass = PassIndex{ static_cast<int32_t>(passIdx) };
                sequence.PassCount = 0;
            }

            ++sequence.PassCount;
        }
    }

    inline void ResolveSequences(FreezeContext& context)
    {
        for (auto&& [seqIdx, frozenSequence] : context.Graph.Sequences | std::views::enumerate)
        {
            // In publish, we built the frozen sequences from sequence records: the two tables should be
            // parallel... so we can use the seqIdx to find the original, which we need to get the anchors
            // from to resolve them
            const SequenceRecord& sequence = context.Record.Sequences[seqIdx];
            frozenSequence.After = ResolveAnchorUses(context, sequence.After);
            frozenSequence.Before = ResolveAnchorUses(context, sequence.Before);

            // We should call this *after* ResolveSequenceRanges, so we can reuse this walk to report
            // if a Sequence is still empty
            if (!frozenSequence.FirstPass.IsValid() || (frozenSequence.PassCount <= 0))
            {
                Diagnostic diag = MakeDiagnostic(DiagnosticCode::LM111, frozenSequence.Where);
                context.Sink.Report(diag);
            }
        }
    }

    inline void ResolveImageTargets(FreezeContext& context)
    {
        for (FrozenImage& image : context.Graph.Images)
        {
            const ExtentSource& extentSource = image.Declaration.Extent;
            if (extentSource.Kind != ExtentSource::Kind::Target)
            {
                continue;
            }

            const int32_t targetIdx = ResolveKey(context,
                                                 extentSource.Target,
                                                 KeyClass::FrameTarget,
                                                 image.Where);
            image.ExtentTarget = FrameTargetIndex{ targetIdx };
        }
    }

    inline void ResolveOutputs(FreezeContext& context)
    {
        // make sure that each frame target is only claimed by a single output, to avoid conflicts and double usage errors
        std::vector<int32_t> claimedBy(context.Graph.FrameTargets.size(), -1);

        for (const auto&& [ idx, output ] : context.Record.Outputs | std::views::enumerate)
        {
            const int32_t targetIdx = ResolveKey(context,
                                                 output.Value.Target.ToRef(),
                                                 KeyClass::FrameTarget,
                                                 output.Where);
            const int32_t outputIdx = static_cast<int32_t>(context.Graph.Outputs.size());

            const FrameTargetIndex target{ targetIdx };
            context.Graph.Outputs.emplace_back(
                target,
                output.Value.OnSkip,
                output.Where);

            if (!target.IsValid())
            {
                // this was already reported by someone 
                // (so resolve key returned -1 and logged a diag)
                continue;
            }

            const int32_t earlier = claimedBy[target.Value];
            if (earlier >= 0)
            {
                Diagnostic diag = MakeDiagnostic(DiagnosticCode::LM112, output.Where);
                diag.OtherWhere = context.Graph.Outputs[earlier].Where;
                diag.Resource = output.Value.Target.ToRef();
                context.Sink.Report(diag);
                continue;
            }

            claimedBy[target.Value] = outputIdx;
        }
    }

    inline void ResolveAnchorOrders(FreezeContext& context)
    {
        auto resolveOrder = [&context](const AnchorOrderRecord& order)
        {
            return FrozenAnchorOrder
            {
                .Earlier = ResolveAnchor(context, order.Earlier, order.Where),
                .Later = ResolveAnchor(context, order.Later, order.Where),
                .Where = order.Where
            };
        };
        // eagerly using ranges bc so many of these functions are either inline or in same TU, so 
        // should be eagerly inlined and make assembly nice and clean (pls)
        context.Graph.AnchorOrders = context.Record.AnchorOrders |
                                     std::views::transform(resolveOrder) |
                                     std::ranges::to<std::vector>();
    }

}

#endif // !DIAMOND_DOGS_LOOM_FREEZE_RESOLVE_HPP
