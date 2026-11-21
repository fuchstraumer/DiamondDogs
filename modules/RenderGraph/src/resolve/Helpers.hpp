#pragma once
#ifndef DIAMOND_DOGS_LOOM_RESOLVE_HELPERS_HPP
#define DIAMOND_DOGS_LOOM_RESOLVE_HELPERS_HPP
#include "ShaderManifestIndex.hpp"
#include <cstdint>
#include <string_view>
#include <vector>

// Helpers.cpp: the small queries that more than one Resolve step uses.
namespace loom
{
    struct FrozenPass;
    struct PinRecord;

    namespace resolve
    {
        // The names SingleFor tests: the compute entry point, or the vertex entry point and, unless the draw is
        // depth-only, the fragment entry point. The views point into the FrozenGraph.
        [[nodiscard]] std::vector<std::string_view> EntryPointNames(const FrozenPass& pass);

        // Adds one pin to a query, with the Where overload for the pin value's domain. Required makes a pin on a gated
        // axis also select its gate; WhenActive also keeps the variants where the gate is off.
        [[nodiscard]] lodestone::ManifestQueryBuilder WherePin(const lodestone::ManifestQueryBuilder& query,
                                                               const PinRecord& pin,
                                                               lodestone::PinMode mode);

        // The axis name as the bundle stores it. The view lives as long as the bundle, so a diagnostic can keep it,
        // where QueryError::AxisName is a string that dies with the error.
        [[nodiscard]] std::string_view AxisName(const lodestone::manifest::ModuleView& module, uint32_t local_axis);

        // The capability axis rule. Lodestone's PreferCapable() cannot replace it yet: an axis that a capability gates
        // off keeps its canonical digit in the key, so the off variant and the on variant with the default value
        // differ in that axis, and neither dominates the other (Lodestone request C25). True for a boolean capability
        // axis: when both of its values survive ForDevice, Resolve pins it to true. That is safe, because ForDevice has
        // already removed true where the device lacks the capability, and Lodestone's capability axis check proves
        // that the false path needs a subset of what the true path needs. An integer or enum capability axis has no
        // "more capable" value that Loom can know, so it stays ambiguous (LM204).
        [[nodiscard]] bool PrefersCapableValue(const lodestone::manifest::ModuleView& module, uint32_t local_axis);
    }
}

#endif // !DIAMOND_DOGS_LOOM_RESOLVE_HELPERS_HPP
