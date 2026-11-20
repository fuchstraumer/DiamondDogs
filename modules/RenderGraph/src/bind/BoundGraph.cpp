#include "bind/BoundGraph.hpp"
#include "PassStatements.hpp"
#include "bind/BindContext.hpp"
#include "freeze/FrozenGraph.hpp"
#include "resolve/ResolvedGraph.hpp"
#include "LoomDiagnostic.hpp"
#include "Records.hpp"
#include "ShaderManifestLayout.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// The Bind stage's steps, in the order they run. Each step lives in the file named for the question it answers.
namespace loom
{

namespace
{
    // The bind that names `position`, or k_NotInVariant when no bind does (already LM302)
    int32_t BindOfPosition(const std::vector<int32_t>& matches, int32_t position)
    {
        for (size_t bindIndex = 0u; bindIndex < matches.size(); ++bindIndex)
        {
            if (matches[bindIndex] == position)
            {
                return static_cast<int32_t>(bindIndex);
            }
        }

        return bind::k_NotInVariant;
    }

    // One slot per merged resource, and the access of each matched bind for Order
    void AppendSlots(BoundGraph& result,
                     size_t pass_index,
                     const FrozenPass& pass,
                     const bind::PassLayout& layout,
                     const std::vector<int32_t>& matches)
    {
        std::vector<ResourceAccess>& accesses = result.Accesses.Accesses[pass_index];

        for (size_t position = 0u; position < layout.Resources.size(); ++position)
        {
            const lodestone::manifest::MergedResource& merged = layout.Resources[position];
            const lodestone::manifest::ResolvedResource& resource = merged.Resource;
            const int32_t bindIndex = BindOfPosition(matches, static_cast<int32_t>(position));

            BindingSlot slot
            {
                .Group = resource.PlacementValue().Word0,
                .Binding = resource.PlacementValue().Word1,
                .Form = resource.Placement(),
                .Container = resource.ValueContainer(),
                .ValueOffset = resource.ValueOffset(),
                .Kind = resource.Kind(),
                .Access = bind::ToResourceAccess(resource.Access()),
                .Stages = merged.Stages,
                .ArrayCount = resource.ArrayCount(),
            };

            if (bindIndex != bind::k_NotInVariant)
            {
                const FrozenBind& frozenBind = pass.Binds[bindIndex];
                slot.Access = bind::NarrowedAccess(slot.Access, frozenBind.Narrowing);
                slot.Resource = frozenBind.Resource;
                accesses[bindIndex] = slot.Access;
            }

            result.Slots.push_back(slot);
            result.Sources.push_back(SlotSource{ .Reflection = resource,
                                                 .Pass = static_cast<int32_t>(pass_index),
                                                 .Bind = bindIndex });
        }
    }

}

std::optional<BoundGraph> Bind(const FrozenGraph& frozen, const ResolvedGraph& resolved, DiagnosticSink& sink)
{
    using namespace bind;
    BindContext context
    {
        .Frozen = frozen,
        .Sink = sink,
    };

    const int32_t errorsBefore = sink.ErrorCount();
    BoundGraph result;
    result.Passes.resize(frozen.Passes.size());
    result.Accesses.Accesses.resize(frozen.Passes.size());

    for (size_t passIndex = 0u; passIndex < frozen.Passes.size(); ++passIndex)
    {
        const FrozenPass& pass = frozen.Passes[passIndex];
        const ResolvedPass& resolvedPass = resolved.Passes[passIndex];
        result.Accesses.Accesses[passIndex].assign(pass.Binds.size(), ResourceAccess::Invalid);

        // a Record pass runs a callback, not a shader
        if (resolvedPass.Environment == nullptr)
        {
            continue;
        }

        const std::optional<PassLayout> layout = MergePassLayout(context, pass, resolvedPass);
        if (!layout.has_value())
        {
            continue;
        }

        const std::vector<int32_t> matches = MatchBinds(context, pass, resolvedPass, *layout);
        CheckUnboundResources(context, pass, *layout, matches);
        CheckViews(context, pass, *layout, matches);

        BoundPass& boundPass = result.Passes[passIndex];
        boundPass.FirstSlot = static_cast<int32_t>(result.Slots.size());
        boundPass.SlotCount = static_cast<int32_t>(layout->Resources.size());
        boundPass.Params = BindPassParams(context, pass, *layout);
        AppendSlots(result, passIndex, pass, *layout, matches);
    }

    if (sink.ErrorCount() != errorsBefore)
    {
        return std::nullopt;
    }

    return result;
}

}
