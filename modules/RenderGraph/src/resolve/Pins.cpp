#include "Records.hpp"
#include "ShaderManifest.hpp"
#include "freeze/FrozenGraph.hpp"
#include "resolve/ResolveContext.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "ShaderManifestIndex.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

// One value per axis, the pass value first. A second Where on the same axis joins the values (1 then 2 means 1 or 2),
// so Loom merges the pins itself and builds one query. LM206: a graph pin that no resolved module declares.
namespace loom::resolve
{

namespace
{
    bool DeclaresAxis(const lodestone::manifest::ModuleView& module, std::string_view axis_name)
    {
        // we don't want to try and retrieve module.ModuleAxes() here, because the names are
        // actually stored separately in the manifest anyways
        for (uint32_t axis = 0u; axis < module.AxisCount(); ++axis)
        {
            if (AxisName(module, axis) == axis_name)
            {
                return true;
            }
        }

        return false;
    }

    bool PassPinsAxis(const FrozenPass& pass, std::string_view axis_name)
    {
        const auto isSameAxis = [axis_name](const PinRecord& record)
        {
            return record.AxisName == axis_name;
        };

        return std::ranges::any_of(pass.Pins, isSameAxis);
    }
}

std::vector<MergedPin> MergePins(ResolveContext& context,
                                 const FrozenPass& pass,
                                 const lodestone::ManifestIndex& index)
{
    namespace ls = lodestone;
    namespace lsm = lodestone::manifest;
    const lsm::ModuleView& module = index.View().Module();
    std::vector<MergedPin> results;
    results.reserve(pass.Pins.size() + context.Frozen.Pins.size());

    for (const PinRecord& pin : pass.Pins)
    {
        results.emplace_back(&pin, -1);
    }

    for (size_t graphIndex = 0; graphIndex < context.Frozen.Pins.size(); ++graphIndex)
    {
        const PinRecord& pin = context.Frozen.Pins[graphIndex];

        if (!DeclaresAxis(module, pin.AxisName))
        {
            continue;
        }

        // declared counts even when a pass pin overrides it - the axis exists, so the
        // pin is not just a typo
        context.GraphPinDeclared[graphIndex] = 1u;


        if (PassPinsAxis(pass, pin.AxisName))
        {
            continue;
        }

        results.emplace_back(&pin, static_cast<int32_t>(graphIndex));

    }

    return results;
}

void CheckGraphPinsDeclared(ResolveContext& context)
{
    for (size_t graphIndex = 0; graphIndex < context.Frozen.Pins.size(); ++graphIndex)
    {
        if (context.GraphPinDeclared[graphIndex] != 0u)
        {
            continue;
        }

        const PinRecord& pin = context.Frozen.Pins[graphIndex];
        const Diagnostic diag
        {
            .Code = DiagnosticCode::LM206,
            .Severity = DefaultSeverityOf(DiagnosticCode::LM206),
            .Where = pin.Where,
            .Binding = pin.AxisName,
        };
        context.Sink.Report(diag);
    }
}

}
