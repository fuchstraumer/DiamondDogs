#include "bind/Layout.hpp"
#include "ShaderManifestFormat.hpp"
#include "ShaderLibraryTypes.hpp"
#include "bind/BindContext.hpp"
#include "bind/Helpers.hpp"
#include "bind/BoundGraph.hpp"
#include "freeze/FrozenGraph.hpp"
#include "resolve/ResolvedGraph.hpp"
#include "Records.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "ShaderManifest.hpp"
#include "ShaderManifestLayout.hpp"
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// One pass's pipeline: MergedLayoutView gives each resource once, with the mask of stages that read it. A dispatch
// merges its one compute instance, so a dispatch and a draw take the same path.
namespace loom::bind
{

namespace
{
    std::string_view DisagreeingBindingName(std::span<const lodestone::manifest::EntryPointInstanceView> instances,
                                            uint32_t record)
    {
        for (const lodestone::manifest::EntryPointInstanceView& instance : instances)
        {
            for (const lodestone::manifest::ResolvedResource resource : instance.Layout())
            {
                if (resource.Index() == record)
                {
                    return BindingNameOf(resource);
                }
            }
        }

        return {};
    }

}

    std::vector<lodestone::manifest::EntryPointInstanceView> PassInstances(const ResolvedPass& resolved)
    {
        namespace lsm = lodestone::manifest;
        std::vector<lsm::EntryPointInstanceView> instances;

        // Resolve selected this key in this environment, so the variant exists
        const lsm::VariantView variant = *resolved.Environment->VariantByKey(resolved.Variant);
        instances.push_back(variant.EntryPoint(static_cast<uint32_t>(resolved.EntryPoint)));

        if (resolved.FragmentEntryPoint != k_NoEntryPoint)
        {
            instances.push_back(variant.EntryPoint(static_cast<uint32_t>(resolved.FragmentEntryPoint)));
        }

        return instances;
    }

    std::optional<PassLayout> MergePassLayout(BindContext& context,
                                              const FrozenPass& pass,
                                              const ResolvedPass& resolved)
    {
        namespace lsm = lodestone::manifest;
        const std::vector<lsm::EntryPointInstanceView> instances = PassInstances(resolved);
        const lsm::ManifestResult<lsm::MergedLayoutView> merged = lsm::MergedLayoutView::Merge(instances);
        if (!merged.has_value())
        {
            // RecordIndex is the binding record that the stages disagree on
            const uint32_t record = merged.error().RecordIndex;

            Diagnostic diag
            {
                .Code = DiagnosticCode::LM304,
                .Severity = DefaultSeverityOf(DiagnosticCode::LM304),
                .Where = pass.Shader.Where,
                .Pass = pass.Key,
                .Binding = DisagreeingBindingName(instances, record),
            };

            context.Sink.Report(diag);
            return std::nullopt;
        }

        PassLayout layout;

        for (const lsm::MergedResource resource : *merged)
        {
            layout.Resources.push_back(resource);
        }

        // SPIR-V delivers constants as one push constant range from offset 0 for every stage. A target that delivers
        // them in a uniform buffer gives each instance its own constants.
        const std::optional<lsm::MergedPushConstants> pushConstants = merged->PushConstants();
        if (pushConstants.has_value())
        {
            layout.Constants = pushConstants->Constants;
        }
        else if (instances.front().Constants().Delivery() != lodestone::EntryPointConstantsDelivery::None)
        {
            layout.Constants = instances.front().Constants();
        }

        return layout;
    }

}
