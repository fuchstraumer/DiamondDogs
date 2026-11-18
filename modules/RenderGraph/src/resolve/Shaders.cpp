#include "Records.hpp"
#include "ShaderManifestFormat.hpp"
#include "freeze/FrozenGraph.hpp"
#include "resolve/ResolveContext.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "ShaderBundle.hpp"
#include "ShaderLibraryTypes.hpp"
#include "ShaderManifest.hpp"
#include "ShaderManifestIndex.hpp"
#include "resolve/ResolvedGraph.hpp"
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

// LM201: the module or an entry point is not in the bundle. LM202: an entry point's stage does not fit the pass kind.
namespace loom::resolve
{

namespace
{
    Diagnostic MakeShaderDiagnostic(DiagnosticCode code,
                                    const FrozenPass& pass,
                                    std::string_view binding)
    {
        return Diagnostic
        {
            .Code = code,
            .Severity = DefaultSeverityOf(code),
            .Where = pass.Shader.Where,
            .OtherWhere = {},
            .Pass = pass.Key,
            .Binding = binding,
        };
    }

    std::optional<int32_t> FindEntryPoint(ResolveContext& context,
                                          const FrozenPass& pass,
                                          const lodestone::manifest::ModuleView& module,
                                          std::string_view entry_point_name,
                                          lodestone::ShaderStageKind shader_stage)
    {
        namespace ls = lodestone;
        namespace lsm = lodestone::manifest;
        // first, search the manifest using it's find function
        const auto found = module.FindEntryPointIndex(entry_point_name);

        if (!found.has_value())
        {
            // gather candidates for suggestion checking
            std::vector<std::string_view> names;
            const lsm::BundleView& bundle = module.Bundle();
            for (const lsm::EntryPoint& ep : module.EntryPoints())
            {
                std::string_view name = bundle.String(ep.NameString);
                names.emplace_back(name);
            }

            std::string_view suggestion = ls::NearestName(entry_point_name, names);

            Diagnostic diag = MakeShaderDiagnostic(DiagnosticCode::LM201, pass, entry_point_name);
            if (!suggestion.empty())
            {
                diag.Suggestion = suggestion;
            }

            context.Sink.Report(diag);
            return std::nullopt;
        }

        const auto& entryPoints = module.EntryPoints();
        const uint32_t idx{ *found };

        const lsm::EntryPoint& entryPoint = entryPoints[idx];

        if (entryPoint.Stage != static_cast<uint32_t>(shader_stage))
        {
            Diagnostic diag = MakeShaderDiagnostic(DiagnosticCode::LM202, pass, entry_point_name);
            context.Sink.Report(diag);
            return std::nullopt;
        }

        return static_cast<int32_t>(idx);
    }

}

std::optional<PassShader> FindPassShader(ResolveContext& context, const FrozenPass& pass)
{
    namespace ls = lodestone;
    namespace lsm = lodestone::manifest;
    const rhi::ShaderBundle& bundle = context.Inputs.Bundle;
    // get the index for the module we're trying to resolve within the overall shader bundle
    // (recall that the index is the top-level query surface, and it's per environment,module combo)
    const rhi::ShaderBundleResult<const ls::ManifestIndex*> index =
        bundle.ModuleIndex(pass.Shader.ModuleName);

    if (!index.has_value())
    {
        // index couldn't be resolved, use suggest in lodestone to try and find nearest names
        const std::vector<std::string_view> modules = bundle.ModuleNames();
        Diagnostic diag = MakeShaderDiagnostic(DiagnosticCode::LM201, pass, pass.Shader.ModuleName);
        diag.Suggestion = lodestone::NearestName(pass.Shader.ModuleName, modules);
        context.Sink.Report(diag);
        return std::nullopt;
    }

    const lsm::ModuleView& module = (*index)->View().Module();
    const bool isDrawPass = (pass.Kind == PassKind::Draw);

    const ls::ShaderStageKind firstStage = isDrawPass ? ls::ShaderStageKind::Vertex :
                                                        ls::ShaderStageKind::Compute;
    const std::string_view& entryPointName = pass.Shader.EntryPoint;
    const std::optional<int32_t> entryPoint = FindEntryPoint(context,
                                                             pass,
                                                             module,
                                                             entryPointName,
                                                             firstStage);

    if (!entryPoint.has_value())
    {
        return std::nullopt;
    }

    PassShader result
    {
        .Index = *index,
        .EntryPoint = *entryPoint,
        .FragmentEntryPoint = k_NoEntryPoint
    };

    // an empty fragment entrypoint is just a depth-only draw
    // todo: may be worth also using our arbitrary string tags in lodestone to identify
    // depth only entrypoints even more clearly, so we can gather them upfront potentially?

    if (isDrawPass && !pass.Shader.FragmentEntryPoint.empty())
    {
        const std::string_view& epName = pass.Shader.FragmentEntryPoint;
        const std::optional<int32_t> fragEp = FindEntryPoint(context,
                                                             pass,
                                                             module,
                                                             epName,
                                                             ls::ShaderStageKind::Fragment);

        if (!fragEp.has_value())
        {
            return std::nullopt;
        }

        result.FragmentEntryPoint = *fragEp;
    }

    return result;
}

}
