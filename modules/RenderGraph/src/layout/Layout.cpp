#include "layout/LaidOutGraph.hpp"
#include "layout/LayoutContext.hpp"
#include "bind/BoundGraph.hpp"
#include "freeze/FrozenGraph.hpp"
#include "resolve/ResolvedGraph.hpp"
#include "size/SizedGraph.hpp"
#include "LoomDiagnostic.hpp"
#include "Records.hpp"
#include "ShaderManifest.hpp"
#include "ShaderManifestLayout.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Each pass in declaration order: its limits and its feedback loops, then its pipeline description and, for a
// dispatch, its group count. The image limits run once, over the resources.
namespace loom
{

namespace
{
    // What every pipeline names: the module, the variant by its suffix, and the entry points
    PipelineDescription DescribeShaders(const FrozenPass& pass, const ResolvedPass& resolved)
    {
        const lodestone::manifest::VariantView variant = *resolved.Environment->VariantByKey(resolved.Variant);

        return PipelineDescription
        {
            .Module = pass.Shader.ModuleName,
            .Variant = std::string{ variant.Suffix() },
            .EntryPoint = pass.Shader.EntryPoint,
            .FragmentEntryPoint = pass.Shader.FragmentEntryPoint,
        };
    }

    // The fixed-function state of a draw: its depth target and its raster state. The colors come from
    // CheckAttachments.
    void DescribeDraw(const FrozenGraph& frozen, const FrozenPass& pass, PipelineDescription& description)
    {
        description.Raster = pass.Raster;
        description.DepthCompare = pass.Depth.Compare;
        description.DepthWrites = pass.Depth.Write;

        if (pass.Depth.Image.IsValid())
        {
            description.DepthFormat = ImageFormatOf(frozen, frozen.Resources[pass.Depth.Image.Value]);
        }
    }

    // Two passes with equal descriptions share one row
    int32_t PipelineIndexOf(std::vector<PipelineDescription>& pipelines, PipelineDescription&& description)
    {
        for (size_t index = 0u; index < pipelines.size(); ++index)
        {
            if (pipelines[index] == description)
            {
                return static_cast<int32_t>(index);
            }
        }

        pipelines.push_back(std::move(description));
        return static_cast<int32_t>(pipelines.size() - 1u);
    }

}

std::optional<LaidOutGraph> Layout(const FrozenGraph& frozen,
                                   const ResolvedGraph& resolved,
                                   const BoundGraph& bound,
                                   const SizedGraph& sized,
                                   const LayoutLimits& limits,
                                   DiagnosticSink& sink)
{
    const int32_t errorsBefore = sink.ErrorCount();
    layout::LayoutContext context
    {
        .Frozen = frozen,
        .Resolved = resolved,
        .Bound = bound,
        .Sized = sized,
        .Limits = limits,
        .Sink = sink,
    };

    LaidOutGraph result;
    result.Passes.resize(frozen.Passes.size());

    for (size_t passIndex = 0u; passIndex < frozen.Passes.size(); ++passIndex)
    {
        const FrozenPass& pass = frozen.Passes[passIndex];
        const ResolvedPass& resolvedPass = resolved.Passes[passIndex];

        layout::CheckPassLimits(context, static_cast<int32_t>(passIndex));
        layout::CheckFeedback(context, static_cast<int32_t>(passIndex));

        // a Record pass has no pipeline and no group count
        if (pass.Kind == PassKind::Record)
        {
            continue;
        }

        PipelineDescription description = DescribeShaders(pass, resolvedPass);

        if (pass.Kind == PassKind::Dispatch)
        {
            result.Passes[passIndex].Dispatch = layout::LayOutDispatch(context, pass, resolvedPass);
        }
        else
        {
            DescribeDraw(frozen, pass, description);
            description.Colors = layout::CheckAttachments(context, pass, resolvedPass);
            layout::CheckVertexInputs(context, pass, resolvedPass);
        }

        result.Passes[passIndex].Pipeline = PipelineIndexOf(result.Pipelines, std::move(description));
    }

    layout::CheckImageLimits(context);

    if (sink.ErrorCount() != errorsBefore)
    {
        return std::nullopt;
    }

    return result;
}

}
