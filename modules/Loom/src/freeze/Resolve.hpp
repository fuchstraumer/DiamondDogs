#pragma once
#ifndef DIAMOND_DOGS_LOOM_FREEZE_RESOLVE_HPP
#define DIAMOND_DOGS_LOOM_FREEZE_RESOLVE_HPP
#include "Freeze.hpp"
#include "ResolveDraw.hpp"
#include "ResolveKey.hpp"
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

    // What dispatch and draw passes share: exactly one shader (LM105), the binds and the params.
    inline void ResolveShaderStatements(FreezeContext& context, const PassRecord& pass, FrozenPass& frozen)
    {
        if (pass.Shaders.size() != 1)
        {
            Diagnostic diag = MakeDiagnostic(DiagnosticCode::LM105, pass.Where);
            context.Sink.Report(diag);
        }
        else [[likely]]
        {
            frozen.Shader = pass.Shaders.front();
        }

        frozen.Binds = ResolveBinds(context, pass.Binds);
        frozen.Params = pass.Params;
    }

    // Exactly one dispatch: none is LM110, two is LM106 with both lines.
    inline void ResolveDispatchStatement(FreezeContext& context, const PassRecord& pass, FrozenPass& frozen)
    {
        if (pass.Dispatches.empty())
        {
            Diagnostic diag = MakeDiagnostic(DiagnosticCode::LM110, pass.Where);
            context.Sink.Report(diag);
        }
        else if (pass.Dispatches.size() > 1)
        {
            Diagnostic diag = MakeDiagnostic(DiagnosticCode::LM106, pass.Dispatches[0].Where);
            diag.OtherWhere = pass.Dispatches[1].Where;
            context.Sink.Report(diag);
        }
        else
        {
            frozen.Dispatch = ResolveDispatch(context, pass.Dispatches.front());
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

        // fairly straightforward: just convert the named records into indexed records
        frozenPass.Before = ResolveAnchorUses(context, source.Pass->Before);
        frozenPass.After = ResolveAnchorUses(context, source.Pass->After);

        // Each builder records only its own kind's statements, so each kind checks only its own.
        switch (source.Pass->Kind)
        {
        case PassKind::Dispatch:
            ResolveShaderStatements(context, *source.Pass, frozenPass);
            ResolveDispatchStatement(context, *source.Pass, frozenPass);
            break;
        case PassKind::Draw:
            ResolveShaderStatements(context, *source.Pass, frozenPass);
            ResolveDrawStatements(context, *source.Pass, frozenPass);
            break;
        case PassKind::Record:
            ResolveRecordStatements(context, *source.Pass, frozenPass);
            break;
        case PassKind::Invalid:
            break;
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
