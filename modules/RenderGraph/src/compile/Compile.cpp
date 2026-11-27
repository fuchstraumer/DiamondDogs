#include "Compile.hpp"
#include "CompiledGraph.hpp"
#include "GraphBuilder.hpp"
#include "LoomDiagnostic.hpp"
#include "ShaderBundle.hpp"
#include "ShaderLibraryTypes.hpp"
#include "ShaderManifestDevice.hpp"
#include "alias/Lifetimes.hpp"
#include "bind/BoundGraph.hpp"
#include "bind/Helpers.hpp"
#include "compile/Pipelines.hpp"
#include "compile/Uses.hpp"
#include "compile/CompiledData.hpp"
#include "freeze/FrozenGraph.hpp"
#include "layout/LaidOutGraph.hpp"
#include "layout/Layout.hpp"
#include "order/OrderedGraph.hpp"
#include "resolve/ResolvedGraph.hpp"
#include "size/Size.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

namespace loom
{

    std::optional<CompiledGraph> Compile(const GraphBuilder& builder, const CompileInputs& inputs, DiagnosticSink& sink)
    {
        std::optional<FrozenGraph> frozen = Freeze(builder.Record(), sink);

        if (!frozen.has_value())
        {
            return std::nullopt;
        }

        const ResolveInputs resolveInputs
        {
            .Bundle = inputs.Bundle,
            .Supports = inputs.Supports,
            .SupportsUser = inputs.SupportsUser,
            .Limits = lodestone::manifest::DeviceLimits
            {
                .MaxWorkgroupMemoryBytes = inputs.ShaderLimits.MaxWorkgroupMemoryBytes,
                .MaxInvocationsPerWorkgroup = inputs.ShaderLimits.MaxInvocationsPerWorkgroup,
                .MaxWorkgroupSizeX = inputs.ShaderLimits.MaxWorkgroupSizeX,
                .MaxWorkgroupSizeY = inputs.ShaderLimits.MaxWorkgroupSizeY,
                .MaxWorkgroupSizeZ = inputs.ShaderLimits.MaxWorkgroupSizeZ,
                .MaxPushConstantBytes = inputs.ShaderLimits.MaxPushConstantBytes,
            },
        };

        const std::optional<ResolvedGraph> resolved = Resolve(*frozen, resolveInputs, sink);

        if (!resolved.has_value())
        {
            return std::nullopt;
        }

        std::optional<BoundGraph> bound = Bind(*frozen, *resolved, sink);

        if (!bound.has_value())
        {
            return std::nullopt;
        }

        std::optional<SizedGraph> sized = Size(*frozen, *bound, sink);

        if (!sized.has_value())
        {
            return std::nullopt;
        }

        std::optional<OrderedGraph> ordered = Order(*frozen, bound->Accesses, sink);

        if (!ordered.has_value())
        {
            return std::nullopt;
        }

        std::optional<LaidOutGraph> laidOut = Layout(*frozen, *resolved, *bound, *sized, inputs.Limits, sink);

        if (!laidOut.has_value())
        {
            return std::nullopt;
        }

        auto data = std::make_unique<CompiledData>();
        data->CodeFormat = inputs.Bundle.Profile().CodeFormat;

        // the slot sources view into the bundle: keep only the reflected access
        data->SlotReflectedAccess.reserve(bound->Sources.size());

        for (const SlotSource& source : bound->Sources)
        {
            data->SlotReflectedAccess.push_back(bind::ToResourceAccess(source.Reflection.Access()));
        }

        data->Slots = std::move(bound->Slots);
        data->BoundPasses = std::move(bound->Passes);
        data->Frozen = std::move(*frozen);
        data->Ordered = std::move(*ordered);
        data->Sized = std::move(*sized);
        data->LaidOut = std::move(*laidOut);

        // the pipelines read the resolved passes, which view into the bundle and into nothing that moved
        compile::CopyPipelines(*resolved, *data);
        compile::GatherFrameTargets(*data);
        compile::GatherCopies(*data);
        compile::GatherUses(*data);
        compile::GatherParams(*data);
        compile::ComputeSpans(*data);
        data->AliasCandidates = FindAliasCandidates(*data);

        return CompiledGraph{ std::move(data) };
    }

}
