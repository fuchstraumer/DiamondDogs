#include "layout/LayoutContext.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "PassStatements.hpp"
#include "Records.hpp"
#include "ShaderLibraryTypes.hpp"
#include <cstdint>
#include <source_location>
#include <string_view>
#include <vector>

// LM604: a pass reads an image that it also writes as a target. Loom records barriers between passes, never inside
// one, so the read sees undefined texels: a feedback loop. Three reads are not loops. A depth target with depth
// writes off is read-only, so a pass may sample it (soft particles, for example). An input attachment
// (InputRenderTarget) reads the target through the attachment, which is its purpose. A copy is a transfer, not a
// shader read; Order sees it.
// Expected is the target's use, Actual the read's use.
namespace loom::layout
{

namespace
{
    struct WrittenTarget
    {
        ResourceIndex Image;
        ImageUse Use{ ImageUse::Invalid };
        std::source_location Where;
    };

    struct ImageRead
    {
        ResourceIndex Image;
        ImageUse Use{ ImageUse::Invalid };
        std::string_view Binding;
        std::source_location Where;
    };

    std::string_view UseName(ImageUse use)
    {
        switch (use)
        {
        case ImageUse::Sampled:
            return "Sampled";
        case ImageUse::Storage:
            return "Storage";
        case ImageUse::ColorTarget:
            return "ColorTarget";
        case ImageUse::DepthTarget:
            return "DepthTarget";
        case ImageUse::TransferSource:
            return "TransferSource";
        case ImageUse::TransferDestination:
            return "TransferDestination";
        case ImageUse::Invalid:
            break;
        }

        return "Invalid";
    }

    // A shader read of an image, as Order would see it. Invalid for every other kind, the input attachment included.
    ImageUse ReadUseOf(lodestone::BindingKind kind)
    {
        switch (kind)
        {
        case lodestone::BindingKind::Texture:
            [[fallthrough]];
        case lodestone::BindingKind::CombinedTextureSampler:
            return ImageUse::Sampled;
        case lodestone::BindingKind::StorageTexture:
            return ImageUse::Storage;
        default:
            break;
        }

        return ImageUse::Invalid;
    }

    bool IsImage(const FrozenGraph& frozen, ResourceIndex resource)
    {
        return resource.IsValid() && frozen.Resources[resource.Value].Kind == ResourceKind::Image;
    }

    // A draw's color targets, and its depth target when it writes depth
    void GatherDrawTargets(const FrozenGraph& frozen, const FrozenPass& pass, std::vector<WrittenTarget>& targets)
    {
        for (const FrozenColorTarget& color : pass.Colors)
        {
            if (color.Kind == TargetKind::Image && IsImage(frozen, color.Image))
            {
                targets.push_back(WrittenTarget{ color.Image, ImageUse::ColorTarget, color.Where });
            }
        }

        const bool writesDepth = IsImage(frozen, pass.Depth.Image) && pass.Depth.Write == DepthWrite::Enabled;
        if (writesDepth)
        {
            targets.push_back(WrittenTarget{ pass.Depth.Image, ImageUse::DepthTarget, pass.Depth.Where });
        }
    }

    // Each image that the pass's shaders bind for reading
    void GatherBoundReads(const LayoutContext& context, int32_t pass_index, std::vector<ImageRead>& reads)
    {
        const BoundPass& bound = context.Bound.Passes[pass_index];
        const FrozenPass& pass = context.Frozen.Passes[pass_index];

        for (int32_t slotIndex = bound.FirstSlot; slotIndex < bound.FirstSlot + bound.SlotCount; ++slotIndex)
        {
            const BindingSlot& slot = context.Bound.Slots[slotIndex];
            const SlotSource& source = context.Bound.Sources[slotIndex];
            const ImageUse use = ReadUseOf(slot.Kind);

            // a slot that no bind names is already LM302
            if (source.Bind < 0 || use == ImageUse::Invalid || !IsImage(context.Frozen, slot.Resource))
            {
                continue;
            }

            const FrozenBind& bind = pass.Binds[source.Bind];
            reads.push_back(ImageRead{ slot.Resource, use, bind.BindingName, bind.Where });
        }
    }

    // A Record pass states both sides as uses. A target use that only reads (a read-only depth target) writes nothing.
    void GatherRecordUses(const FrozenGraph& frozen, const FrozenPass& pass, std::vector<WrittenTarget>& targets,
                          std::vector<ImageRead>& reads)
    {
        for (const FrozenUse& use : pass.Uses)
        {
            if (use.Target != TargetKind::Image || !IsImage(frozen, use.Resource))
            {
                continue;
            }

            const bool target = use.AsImage == ImageUse::ColorTarget || use.AsImage == ImageUse::DepthTarget;
            const bool shaderRead = use.AsImage == ImageUse::Sampled || use.AsImage == ImageUse::Storage;
            if (target && use.Access != ResourceAccess::Read)
            {
                targets.push_back(WrittenTarget{ use.Resource, use.AsImage, use.Where });
            }
            else if (shaderRead)
            {
                reads.push_back(ImageRead{ use.Resource, use.AsImage, std::string_view{}, use.Where });
            }
        }
    }

    void ReportLM604(LayoutContext& context, const FrozenPass& pass, const WrittenTarget& target,
                     const ImageRead& read)
    {
        Diagnostic diag
        {
            .Code = DiagnosticCode::LM604,
            .Severity = DefaultSeverityOf(DiagnosticCode::LM604),
            .Where = read.Where,
            .OtherWhere = target.Where,
            .Pass = pass.Key,
            .Resource = context.Frozen.Resources[read.Image.Value].Key,
            .Binding = read.Binding,
            .Expected = TextValue("Use", UseName(target.Use)),
            .Actual = TextValue("Use", UseName(read.Use)),
        };

        context.Sink.Report(diag);
    }

}

    void CheckFeedback(LayoutContext& context, int32_t pass_index)
    {
        const FrozenPass& pass = context.Frozen.Passes[pass_index];
        std::vector<WrittenTarget> targets;
        std::vector<ImageRead> reads;

        if (pass.Kind == PassKind::Record)
        {
            GatherRecordUses(context.Frozen, pass, targets, reads);
        }
        else
        {
            GatherDrawTargets(context.Frozen, pass, targets);
            GatherBoundReads(context, pass_index, reads);
        }

        // one report per read: the first target that writes the same image
        for (const ImageRead& read : reads)
        {
            for (const WrittenTarget& target : targets)
            {
                if (target.Image == read.Image)
                {
                    ReportLM604(context, pass, target, read);
                    break;
                }
            }
        }
    }

}
