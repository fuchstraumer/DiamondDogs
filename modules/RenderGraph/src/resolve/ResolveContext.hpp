#pragma once
#ifndef DIAMOND_DOGS_LOOM_RESOLVE_CONTEXT_HPP
#define DIAMOND_DOGS_LOOM_RESOLVE_CONTEXT_HPP
#include "resolve/ResolvedGraph.hpp"
#include "freeze/FrozenGraph.hpp"
#include "Records.hpp"
#include "ShaderManifestIndex.hpp"
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// The Resolve stage's internals, shared by its source files and by the tests. The public part (ResolveInputs,
// ResolvedGraph, Resolve) is in ResolvedGraph.hpp. Each step lives in the file named for the question it answers.
namespace loom
{
    class DiagnosticSink;

    namespace resolve
    {
        struct ResolveContext
        {
            const FrozenGraph& Frozen;
            const ResolveInputs& Inputs;
            DiagnosticSink& Sink;

            // [graph pin] 1 once a resolved module declares the pin's axis. LM206 reports each pin still at 0.
            std::vector<uint8_t> GraphPinDeclared;
        };

        // Shaders.cpp: which module and entry points a pass names (LM201), and whether their stages fit the pass
        // kind (LM202).

        struct PassShader
        {
            const lodestone::ManifestIndex* Index{ nullptr };
            int32_t EntryPoint{ k_NoEntryPoint };
            int32_t FragmentEntryPoint{ k_NoEntryPoint };
        };

        [[nodiscard]] std::optional<PassShader> FindPassShader(ResolveContext& context, const FrozenPass& pass);

        // Pins.cpp: one value per axis, the pass value first (LM206 for a graph pin that no module declares).

        // Both pointers point into the FrozenGraph, so a diagnostic can keep views into the pin's strings.
        struct MergedPin
        {
            const PinRecord* Pin{ nullptr };
            int32_t GraphPinIndex{ -1 };    // the index in FrozenGraph::Pins, or -1 for a pass pin
        };

        // Drops a graph pin on an axis that the module does not declare, and marks each graph pin it keeps in
        // context.GraphPinDeclared.
        [[nodiscard]] std::vector<MergedPin> MergePins(ResolveContext& context,
                                                       const FrozenPass& pass,
                                                       const lodestone::ManifestIndex& index);

        void CheckGraphPinsDeclared(ResolveContext& context);

        // Variants.cpp: the query, the device filter, and one variant (LM203, LM204, LM205, LM207).

        [[nodiscard]] std::optional<lodestone::VariantKey> SelectVariant(ResolveContext& context,
                                                                         const FrozenPass& pass,
                                                                         const PassShader& shader,
                                                                         std::span<const MergedPin> pins);

        // Helpers for the steps above (Helpers.cpp).

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
        // differ in that axis, and neither dominates the other (Lodestone request C25). True for a boolean capability axis:
        // when both of its values survive ForDevice, Resolve pins it to true. That is safe, because ForDevice has
        // already removed true where the device lacks the capability, and Lodestone's capability axis check proves
        // that the false path needs a subset of what the true path needs. An integer or enum capability axis has no
        // "more capable" value that Loom can know, so it stays ambiguous (LM204).
        [[nodiscard]] bool PrefersCapableValue(const lodestone::manifest::ModuleView& module, uint32_t local_axis);

    }
}

#endif // !DIAMOND_DOGS_LOOM_RESOLVE_CONTEXT_HPP
