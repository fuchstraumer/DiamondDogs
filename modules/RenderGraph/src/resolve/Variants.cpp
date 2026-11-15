#include "ShaderLibraryTypes.hpp"
#include "VariantKey.hpp"
#include "freeze/FrozenGraph.hpp"
#include "resolve/ResolveContext.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "ShaderManifest.hpp"
#include "ShaderManifestIndex.hpp"
#include <algorithm>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

// The query from the merged pins, the device filter, then one variant. LM203: no variant matches the pins. LM204: more
// than one still matches. LM205: an unpinned axis takes its declared default (a warning). LM207: variants match the
// pins, and the device can run none of them.
namespace loom::resolve
{

namespace
{
    Diagnostic MakeVariantDiagnostic(DiagnosticCode code, const FrozenPass& pass, std::string_view name)
    {
        return Diagnostic
        {
            .Code = code,
            .Severity = DefaultSeverityOf(code),
            .Where = pass.Shader.Where,
            .Binding = name,
        };
    }

    // If the axis in `axis_name` is pinned in the provided `pins`, return that name
    // (this is so that we return the axis name, which correlates to the input source 1:1)
    std::string_view PinnedAxisName(std::span<const MergedPin> pins, std::string_view axis_name)
    {
        const auto isSameAxis = [axis_name](const MergedPin& pin)
        {
            return pin.Pin->AxisName == axis_name;
        };

        const auto found = std::ranges::find_if(pins, isSameAxis);

        return found != pins.end() ? std::string_view{ found->Pin->AxisName } : std::string_view{};
    }

    // returns the bundles capability name - same as above, we want to return what's in the manifest
    // this is also a static string that should resolve so long as the bundle file isn't closed early
    std::string_view CapabilityName(const lodestone::manifest::EnvironmentView& environment,
                                    std::string_view capability_name)
    {
        // span of indices into master name table
        const auto capabilityNames = environment.CapabilityNames();
        const auto checkName = [capability_name, environment](uint32_t index)
        {
            const std::string_view& storedName = environment.CapabilityName(index);
            return storedName == capability_name;
        };

        const auto found = std::ranges::find_if(capabilityNames, checkName);
        return found != capabilityNames.end() ? std::string_view{ environment.CapabilityName(*found) } : std::string_view{};
    }

    // a declared default as a constraint. we use the `WhenActive` query field here to make sure
    // this doesn't allow the defualt of a gated axis to switch the gate on
    lodestone::ManifestQueryBuilder WhereDefault(const lodestone::ManifestQueryBuilder& query,
                                                std::string_view axis_name,
                                                const lodestone::QueryAxisValue& value)
    {
        switch (value.Type)
        {
        case lodestone::AxisValueDomain::Boolean:
            return query.WhereWhenActive(axis_name, value.IntegralValue != 0);
        case lodestone::AxisValueDomain::Integral:
            return query.WhereWhenActive(axis_name, value.IntegralValue);
        case lodestone::AxisValueDomain::Enum:
            [[fallthrough]];
        case lodestone::AxisValueDomain::Type:
            return query.WhereWhenActive(axis_name, value.Type, value.Name);
        case lodestone::AxisValueDomain::None:
            break;
        }

        return query;
    }

}

std::optional<lodestone::VariantKey> SelectVariant(ResolveContext& context,
                                                    const FrozenPass& pass,
                                                    const PassShader& shader,
                                                    std::span<const MergedPin> pins)
{
    namespace ls = lodestone;
    namespace lsm = lodestone::manifest;
    const lsm::EnvironmentView& environment = shader.Index->View();
    const lsm::ModuleView& module = environment.Module();
    const std::vector<std::string_view> entryPointNames = EntryPointNames(pass);

    // open a new query - remember this comes from the index first
    ls::ManifestQueryBuilder query = shader.Index->Query();

    // now we will construct the query based on the merged pins
    for (const MergedPin& pin : pins)
    {
        // pass pins can switch gates on: graph pins apply to every module, so they can't
        const bool isPassPin = (pin.GraphPinIndex < 0);
        const ls::PinMode mode = isPassPin ? ls::PinMode::Required : ls::PinMode::WhenActive;
        query = WherePin(query, *pin.Pin, mode);
    }

    // now filter the query to the device scope (limits, supported features, etc)
    query = query.ForDevice(context.Inputs.Supports, context.Inputs.SupportsUser, context.Inputs.Limits);

    // each round of this loop will now pin any axis that still varies, so that the loop ends
    // in at most one round per axis
    using RoundResult = std::expected<ls::VariantKey, ls::QueryError>;
    for (uint32_t round = 0u; std::cmp_less_equal(round, module.AxisCount()); ++round)
    {
        const RoundResult selected = query.SingleFor(entryPointNames);

        if (selected.has_value())
        {
            return *selected;
        }

        const ls::QueryError& error = selected.error();
        if (error.Code == lodestone::QueryErrorCode::NoVariantFitsDevice)
        {
            // we can't fix this by pinning axes in successive rounds, so we should break early
            Diagnostic diag = MakeVariantDiagnostic(DiagnosticCode::LM207,
                                                    pass,
                                                    CapabilityName(environment, error.AxisName));
        
            context.Sink.Report(diag);
            return std::nullopt;
        }

        if (error.Code != lodestone::QueryErrorCode::AmbiguousVariant)
        {
            // not a device fit falure, but still tells us we can't fix the query by pinning axes
            const auto axisName = PinnedAxisName(pins, error.AxisName);
            Diagnostic diag = MakeVariantDiagnostic(DiagnosticCode::LM203,
                                                    pass,
                                                    axisName);
            // don't forget to copy the suggestion over, since it can make fixing this wayy easier
            diag.Suggestion = error.Suggestion;
            context.Sink.Report(diag);
            return std::nullopt;
        }

        bool pinnedAny = false;

        for (const uint32_t axis : error.VaryingAxes)
        {
            const std::string_view axisName = AxisName(module, axis);

            // device runs both values on this axis: prefer the more capable one
            if (PrefersCapableValue(module, axis))
            {
                // we've also filtered to what this device supports already, so this is safe!
                query = query.Where(axisName, true);
                pinnedAny = true;
                continue;
            }

            const std::string_view pinnedName = PinnedAxisName(pins, axisName);
            // if a pinned axis still varies, there is no default to apply: error :(
            if (!pinnedName.empty())
            {
                continue;
            }

            const lodestone::QueryResult<lodestone::QueryAxisValue> fallback = shader.Index->AxisDefault(axisName);
            // not even a default we could *try* to pin? also error. sad
            if (!fallback.has_value())
            {
                continue;
            }

            query = WhereDefault(query, axisName, *fallback);
            pinnedAny = true;
            // this is just a warning diag: let users know they didn't pin an axis, so we use the default
            // this is fine for getting things running, but generally users should be pinning axes
            Diagnostic diag = MakeVariantDiagnostic(DiagnosticCode::LM205,
                                                    pass,
                                                    axisName);
            context.Sink.Report(diag);
        }

        if (!pinnedAny)
        {
            // if we failed to pin, break and return nullopt+the errors in the sink
            break;
        }

    }

    Diagnostic diag = MakeVariantDiagnostic(DiagnosticCode::LM204,
                                            pass,
                                            "");
    context.Sink.Report(diag);
    return std::nullopt;
}

}
