#pragma once
#ifndef DIAMOND_DOGS_LOOM_FREEZE_RESOLVE_DRAW_HPP
#define DIAMOND_DOGS_LOOM_FREEZE_RESOLVE_DRAW_HPP
#include "Freeze.hpp"
#include "ResolveKey.hpp"
#include "FrozenGraph.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "PassStatements.hpp"
#include "Records.hpp"
#include <cstdint>
#include <source_location>
#include <vector>

// Step 3, for the statements of draw and Record passes. Each function resolves the names in one kind of statement and
// checks the values that need no lookup (LM101, LM109), plus the at-most-once rule (LM113). Resolve.hpp calls these
// from ResolvePass.
namespace loom::freeze
{

    // A statement that may appear at most once appeared again. Where is the second one, OtherWhere the first.
    inline void ReportTwice(FreezeContext& context, std::source_location second, std::source_location first)
    {
        Diagnostic diag = MakeDiagnostic(DiagnosticCode::LM113, second);
        diag.OtherWhere = first;
        context.Sink.Report(diag);
    }

    inline FrozenDraw ResolveDraw(FreezeContext& context, const DrawRecord& draw)
    {
        FrozenDraw frozen
        {
            .Form = draw.Form,
            .Arguments = {},
            .VertexOrIndexCount = draw.VertexOrIndexCount,
            .InstanceCount = draw.InstanceCount,
            .FirstVertexOrIndex = draw.FirstVertexOrIndex,
            .FirstInstance = draw.FirstInstance,
            .VertexOffset = draw.VertexOffset,
            .Where = draw.Where
        };

        if (draw.Form == DrawForm::DrawIndirect)
        {
            frozen.Arguments = ResolveResource(context, draw.Arguments, KeyClass::Buffer, draw.Where);
            return frozen;
        }

        // Draw and DrawIndexed: the counts. A count of 0 draws nothing, but it is legal, so only negatives are errors.
        if (draw.VertexOrIndexCount < 0)
        {
            ReportOutOfRange(context, "VertexOrIndexCount", draw.VertexOrIndexCount, draw.Where);
        }

        if (draw.InstanceCount < 1)
        {
            ReportOutOfRange(context, "InstanceCount", draw.InstanceCount, draw.Where);
        }

        if (draw.FirstVertexOrIndex < 0)
        {
            ReportOutOfRange(context, "FirstVertexOrIndex", draw.FirstVertexOrIndex, draw.Where);
        }

        if (draw.FirstInstance < 0)
        {
            ReportOutOfRange(context, "FirstInstance", draw.FirstInstance, draw.Where);
        }

        return frozen;
    }

    inline FrozenColorTarget ResolveColorTarget(FreezeContext& context, const ColorTargetRecord& color)
    {
        FrozenColorTarget frozen
        {
            .Slot = color.Slot,
            .Kind = color.Kind,
            .Image = {},
            .FrameTarget = {},
            .Load = color.Load,
            .Store = color.Store,
            .Blend = color.Blend,
            .Where = color.Where
        };

        // The upper limit is the device's maxColorAttachments, checked at Layout (LM601).
        if (color.Slot < 0)
        {
            ReportOutOfRange(context, "Slot", color.Slot, color.Where);
        }

        if (color.Load.Kind == ColorLoad::Kind::Invalid)
        {
            ReportMissing(context, "Load", color.Where);
        }

        if (color.Store == AttachmentStore::Invalid)
        {
            ReportMissing(context, "Store", color.Where);
        }

        if (color.Blend == BlendMode::Invalid)
        {
            ReportMissing(context, "Blend", color.Where);
        }

        if (color.Kind == TargetKind::FrameTarget)
        {
            frozen.FrameTarget = FrameTargetIndex{ ResolveKey(context, color.Target, KeyClass::FrameTarget, color.Where) };
        }
        else
        {
            frozen.Image = ResolveResource(context, color.Target, KeyClass::Image, color.Where);
        }

        return frozen;
    }

    // Two targets in one slot is LM113. The lists are a handful of entries, so a scan of the earlier ones is enough.
    inline std::vector<FrozenColorTarget> ResolveColorTargets(FreezeContext& context,
                                                              const std::vector<ColorTargetRecord>& colors)
    {
        std::vector<FrozenColorTarget> frozen;
        frozen.reserve(colors.size());

        for (const ColorTargetRecord& color : colors)
        {
            for (const FrozenColorTarget& earlier : frozen)
            {
                if (earlier.Slot == color.Slot)
                {
                    ReportTwice(context, color.Where, earlier.Where);
                    break;
                }
            }

            frozen.push_back(ResolveColorTarget(context, color));
        }

        return frozen;
    }

    inline FrozenDepthTarget ResolveDepthTarget(FreezeContext& context, const DepthTargetRecord& depth)
    {
        if (depth.Load.Kind == DepthLoad::Kind::Invalid)
        {
            ReportMissing(context, "Load", depth.Where);
        }
        else if (depth.Load.Kind == DepthLoad::Kind::Clear && (depth.Load.ClearDepth < 0.0f || depth.Load.ClearDepth > 1.0f))
        {
            ReportOutOfRangeReal(context, "ClearDepth", depth.Load.ClearDepth, depth.Where);
        }

        if (depth.Store == AttachmentStore::Invalid)
        {
            ReportMissing(context, "Store", depth.Where);
        }

        if (depth.Write == DepthWrite::Invalid)
        {
            ReportMissing(context, "Write", depth.Where);
        }

        return FrozenDepthTarget
        {
            .Image = ResolveResource(context, depth.Target, KeyClass::Image, depth.Where),
            .Load = depth.Load,
            .Store = depth.Store,
            .Compare = depth.Compare,
            .Write = depth.Write,
            .Where = depth.Where
        };
    }

    inline std::vector<FrozenVertexBuffer> ResolveVertexBuffers(FreezeContext& context,
                                                                const std::vector<VertexBufferRecord>& buffers)
    {
        std::vector<FrozenVertexBuffer> frozen;
        frozen.reserve(buffers.size());

        for (const VertexBufferRecord& buffer : buffers)
        {
            if (buffer.Slot < 0)
            {
                ReportOutOfRange(context, "Slot", buffer.Slot, buffer.Where);
            }

            for (const FrozenVertexBuffer& earlier : frozen)
            {
                if (earlier.Slot == buffer.Slot)
                {
                    ReportTwice(context, buffer.Where, earlier.Where);
                    break;
                }
            }

            frozen.push_back(FrozenVertexBuffer
            {
                .Slot = buffer.Slot,
                .Buffer = ResolveResource(context, buffer.Buffer, KeyClass::Buffer, buffer.Where),
                .Where = buffer.Where
            });
        }

        return frozen;
    }

    inline FrozenIndexBuffer ResolveIndexBuffer(FreezeContext& context, const IndexBufferRecord& index)
    {
        if (index.Type == IndexType::Invalid)
        {
            ReportMissing(context, "Type", index.Where);
        }

        return FrozenIndexBuffer
        {
            .Buffer = ResolveResource(context, index.Buffer, KeyClass::Buffer, index.Where),
            .Type = index.Type,
            .Where = index.Where
        };
    }

    // Reports every repeat after the first, then returns the first, or nothing when the list is empty.
    template<typename Statement>
    inline const Statement* AtMostOnce(FreezeContext& context, const std::vector<Statement>& statements)
    {
        if (statements.empty())
        {
            return nullptr;
        }

        for (std::size_t index = 1; index < statements.size(); ++index)
        {
            ReportTwice(context, statements[index].Where, statements.front().Where);
        }

        return &statements.front();
    }

    // Everything a draw pass states besides its shader, binds and params.
    inline void ResolveDrawStatements(FreezeContext& context, const PassRecord& pass, FrozenPass& frozen)
    {
        if (pass.Draws.empty())
        {
            Diagnostic diag = MakeDiagnostic(DiagnosticCode::LM110, pass.Where);
            context.Sink.Report(diag);
        }
        else if (pass.Draws.size() > 1)
        {
            Diagnostic diag = MakeDiagnostic(DiagnosticCode::LM106, pass.Draws[0].Where);
            diag.OtherWhere = pass.Draws[1].Where;
            context.Sink.Report(diag);
        }
        else
        {
            frozen.Draw = ResolveDraw(context, pass.Draws.front());
        }

        frozen.Colors = ResolveColorTargets(context, pass.Colors);
        frozen.VertexBuffers = ResolveVertexBuffers(context, pass.VertexBuffers);

        const DepthTargetRecord* depth = AtMostOnce(context, pass.Depths);

        if (depth != nullptr)
        {
            frozen.Depth = ResolveDepthTarget(context, *depth);
        }

        const RasterStateRecord* raster = AtMostOnce(context, pass.RasterStates);

        if (raster != nullptr)
        {
            frozen.Raster = raster->State;

            if (raster->State.Topology == PrimitiveTopology::Invalid)
            {
                ReportMissing(context, "Topology", raster->Where);
            }

            if (raster->State.Polygon == PolygonMode::Invalid)
            {
                ReportMissing(context, "Polygon", raster->Where);
            }
        }

        const IndexBufferRecord* index = AtMostOnce(context, pass.IndexBuffers);

        if (index != nullptr)
        {
            frozen.Index = ResolveIndexBuffer(context, *index);
        }

        // A draw must write somewhere: a depth-only draw has no color target, but it has a depth target.
        if (pass.Colors.empty() && pass.Depths.empty())
        {
            ReportMissing(context, "Color", pass.Where);
        }

        const bool indexed = !pass.Draws.empty() && pass.Draws.front().Form == DrawForm::DrawIndexed;

        if (indexed && pass.IndexBuffers.empty())
        {
            ReportMissing(context, "IndexBuffer", pass.Draws.front().Where);
        }
    }

    inline FrozenUse ResolveUse(FreezeContext& context, const UseRecord& use)
    {
        FrozenUse frozen
        {
            .Kind = use.Kind,
            .Target = use.Target,
            .Resource = {},
            .FrameTarget = {},
            .Access = use.Access,
            .AsImage = use.AsImage,
            .AsBuffer = use.AsBuffer,
            .Where = use.Where
        };

        if (use.Access == ResourceAccess::Invalid)
        {
            ReportMissing(context, "Access", use.Where);
        }

        if (use.Kind == ResourceKind::Buffer)
        {
            if (use.AsBuffer == BufferUse::Invalid)
            {
                ReportMissing(context, "Use", use.Where);
            }

            frozen.Resource = ResolveResource(context, use.Resource, KeyClass::Buffer, use.Where);
            return frozen;
        }

        if (use.AsImage == ImageUse::Invalid)
        {
            ReportMissing(context, "Use", use.Where);
        }

        if (use.Target == TargetKind::FrameTarget)
        {
            frozen.FrameTarget = FrameTargetIndex{ ResolveKey(context, use.Resource, KeyClass::FrameTarget, use.Where) };
        }
        else
        {
            frozen.Resource = ResolveResource(context, use.Resource, KeyClass::Image, use.Where);
        }

        return frozen;
    }

    // A Record pass has exactly one recorder (LM105) and no other statements besides its anchors and uses.
    inline void ResolveRecordStatements(FreezeContext& context, const PassRecord& pass, FrozenPass& frozen)
    {
        if (pass.Recorders.size() != 1)
        {
            Diagnostic diag = MakeDiagnostic(DiagnosticCode::LM105, pass.Where);
            context.Sink.Report(diag);
        }
        else
        {
            frozen.Recorder = pass.Recorders.front().Callback;
        }

        frozen.Uses.reserve(pass.Uses.size());

        for (const UseRecord& use : pass.Uses)
        {
            frozen.Uses.push_back(ResolveUse(context, use));
        }
    }

}

#endif // !DIAMOND_DOGS_LOOM_FREEZE_RESOLVE_DRAW_HPP
