#include "PassStatements.hpp"
#include "ShaderManifestIndex.hpp"
#include "bind/BindContext.hpp"
#include "bind/Slots.hpp"
#include "bind/Helpers.hpp"
#include "bind/Layout.hpp"
#include "bind/BoundGraph.hpp"
#include "Records.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "ShaderManifest.hpp"
#include "ShaderManifestLayout.hpp"
#include "freeze/FrozenGraph.hpp"
#include "resolve/ResolvedGraph.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <source_location>
#include <string_view>
#include <utility>
#include <vector>

// LM301: a Bind name is in no variant of the entry points. LM302: a binding of the selected variant has no Bind.
// LM303: a narrowing adds access. LM304: the key's kind does not fit the binding. LM307: a view and its root, bound
// to different keys in one pass.
namespace loom::bind
{

namespace
{
    Diagnostic MakeBindDiagnostic(DiagnosticCode code, const FrozenPass& pass, std::source_location where,
                                  std::string_view binding)
    {
        return Diagnostic
        {
            .Code = code,
            .Severity = DefaultSeverityOf(code),
            .Where = where,
            .Pass = pass.Key,
            .Binding = binding,
        };
    }

    DiagnosticValue AccessValue(std::string_view field_name, ResourceAccess access)
    {
        std::string_view text{ "Invalid" };

        switch (access)
        {
        case ResourceAccess::Read:
            text = "Read";
            break;
        case ResourceAccess::Write:
            text = "Write";
            break;
        case ResourceAccess::ReadWrite:
            text = "ReadWrite";
            break;
        case ResourceAccess::Invalid:
            break;
        }

        return DiagnosticValue{ .FieldName = field_name, .Kind = DiagnosticValueKind::Text, .Text = text };
    }

    // True when `narrowed` has an access bit that `reflected` lacks. Read is bit 0 and Write bit 1.
    bool AddsAccess(ResourceAccess narrowed, ResourceAccess reflected)
    {
        const uint8_t added = static_cast<uint8_t>(narrowed) & ~static_cast<uint8_t>(reflected);
        return added != 0u;
    }

    int32_t FindResource(const PassLayout& layout, std::string_view binding_name)
    {
        const auto hasName = [binding_name](const lodestone::manifest::MergedResource& merged)
        {
            return BindingNameOf(merged.Resource) == binding_name;
        };

        const auto found = std::ranges::find_if(layout.Resources, hasName);
        if (found == layout.Resources.end())
        {
            return k_NotInVariant;
        }

        return static_cast<int32_t>(found - layout.Resources.begin());
    }

    // The binding names of the pass's entry points in every variant of the environment. This runs only on the miss
    // path, so a name that some other variant has is told apart from a typo.
    std::vector<std::string_view> NamesInAnyVariant(const ResolvedPass& resolved)
    {
        namespace lsm = lodestone::manifest;
        const lsm::EnvironmentView& environment = *resolved.Environment;
        const uint32_t variantCount = static_cast<uint32_t>(environment.Variants().size());
        std::vector<std::string_view> names;

        for (uint32_t variantIndex = 0u; variantIndex < variantCount; ++variantIndex)
        {
            const lsm::VariantView variant = environment.VariantAt(variantIndex);

            for (const int32_t entryPoint : { resolved.EntryPoint, resolved.FragmentEntryPoint })
            {
                if (entryPoint == k_NoEntryPoint)
                {
                    continue;
                }

                for (const lsm::ResolvedResource resource : variant.EntryPoint(static_cast<uint32_t>(entryPoint)).Layout())
                {
                    names.push_back(BindingNameOf(resource));
                }
            }
        }

        std::ranges::sort(names);
        const auto repeated = std::ranges::unique(names);
        names.erase(repeated.begin(), repeated.end());
        return names;
    }

    void ReportUnknownName(BindContext& context, const FrozenPass& pass, const FrozenBind& bind,
                           const std::vector<std::string_view>& names)
    {
        Diagnostic diag = MakeBindDiagnostic(DiagnosticCode::LM301, pass, bind.Where, bind.BindingName);
        diag.Resource = context.Frozen.Resources[bind.Resource.Value].Key;
        diag.Suggestion = lodestone::NearestName(bind.BindingName, names);
        context.Sink.Report(diag);
    }

    // LM304 and LM303 for a bind whose binding is in the selected variant
    void CheckBindFits(BindContext& context, const FrozenPass& pass, const FrozenBind& bind,
                       const lodestone::manifest::ResolvedResource& resource)
    {
        const std::string_view name = BindingNameOf(resource);

        if (!KeyFitsBinding(bind.Kind, resource.Kind()))
        {
            Diagnostic diag = MakeBindDiagnostic(DiagnosticCode::LM304, pass, bind.Where, name);
            diag.Resource = context.Frozen.Resources[bind.Resource.Value].Key;
            context.Sink.Report(diag);
            return;
        }

        const ResourceAccess reflected = ToResourceAccess(resource.Access());
        const ResourceAccess narrowed = NarrowedAccess(reflected, bind.Narrowing);
        if (AddsAccess(narrowed, reflected))
        {
            Diagnostic diag = MakeBindDiagnostic(DiagnosticCode::LM303, pass, bind.Where, name);
            diag.Resource = context.Frozen.Resources[bind.Resource.Value].Key;
            diag.Expected = AccessValue("Access", reflected);
            diag.Actual = AccessValue("Access", narrowed);
            context.Sink.Report(diag);
        }
    }

}

std::vector<int32_t> MatchBinds(BindContext& context,
                                const FrozenPass& pass,
                                const ResolvedPass& resolved,
                                const PassLayout& layout)
{
    std::vector<int32_t> matches(pass.Binds.size(), k_NotInVariant);
    // filled on the first miss only: most passes never need it
    std::vector<std::string_view> otherNames;

    for (size_t bindIndex = 0u; bindIndex < pass.Binds.size(); ++bindIndex)
    {
        const FrozenBind& bind = pass.Binds[bindIndex];
        const int32_t position = FindResource(layout, bind.BindingName);
        if (position != k_NotInVariant)
        {
            matches[bindIndex] = position;
            CheckBindFits(context, pass, bind, layout.Resources[position].Resource);
            continue;
        }

        if (otherNames.empty())
        {
            otherNames = NamesInAnyVariant(resolved);
        }

        // in another variant: this variant does not read it, so the bind is skipped without a diagnostic
        if (std::ranges::binary_search(otherNames, std::string_view{ bind.BindingName }))
        {
            continue;
        }

        ReportUnknownName(context, pass, bind, otherNames);
    }

    return matches;
}

void CheckViews(BindContext& context,
                const FrozenPass& pass,
                const PassLayout& layout,
                const std::vector<int32_t>& matches)
{
    for (size_t bindIndex = 0u; bindIndex < matches.size(); ++bindIndex)
    {
        // a bind skipped for this variant names no binding here
        if (matches[bindIndex] == k_NotInVariant)
        {
            continue;
        }

        const std::string_view root = ViewRootOf(layout.Resources[matches[bindIndex]].Resource);
        const int32_t rootPosition = root.empty() ? k_NotInVariant : FindResource(layout, root);
        const auto rootBind = std::ranges::find(matches, rootPosition);

        // a root that this pass does not read, or does not bind (already LM302), has no key to compare
        if (rootPosition == k_NotInVariant || rootBind == matches.end())
        {
            continue;
        }

        const FrozenBind& view = pass.Binds[bindIndex];
        const FrozenBind& rootBound = pass.Binds[static_cast<size_t>(rootBind - matches.begin())];

        if (view.Resource == rootBound.Resource)
        {
            continue;
        }

        Diagnostic diag = MakeBindDiagnostic(DiagnosticCode::LM307, pass, view.Where, view.BindingName);
        diag.OtherWhere = rootBound.Where;
        diag.Resource = context.Frozen.Resources[view.Resource.Value].Key;
        context.Sink.Report(diag);
    }
}

void CheckUnboundResources(BindContext& context,
                           const FrozenPass& pass,
                           const PassLayout& layout,
                           const std::vector<int32_t>& matches)
{
    // The constants uniform buffer is the graph's own: the backend writes it, so no Bind names it. A dispatch merges its
    // one instance, so the binding position in the instance's layout is the position in the merged resources.
    int32_t constantsPosition = k_NotInVariant;

    if (layout.Constants.has_value() && layout.Constants->Delivery() == lodestone::EntryPointConstantsDelivery::UniformBuffer)
    {
        constantsPosition = static_cast<int32_t>(layout.Constants->BindingPosition());
    }

    for (size_t position = 0u; position < layout.Resources.size(); ++position)
    {
        const bool bound = std::ranges::contains(matches, static_cast<int32_t>(position));
        if (bound || std::cmp_equal(position, constantsPosition))
        {
            continue;
        }

        const std::string_view name = BindingNameOf(layout.Resources[position].Resource);
        context.Sink.Report(MakeBindDiagnostic(DiagnosticCode::LM302, pass, pass.Shader.Where, name));
    }
}

}
