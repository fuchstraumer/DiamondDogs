#pragma once
#ifndef DIAMOND_DOGS_LOOM_RESOLVE_PINS_HPP
#define DIAMOND_DOGS_LOOM_RESOLVE_PINS_HPP
#include <cstdint>
#include <vector>

namespace lodestone
{
    class ManifestIndex;
}

// Pins.cpp: one value per axis, the pass value first (LM206 for a graph pin that no module declares).
namespace loom
{
    struct FrozenPass;
    struct PinRecord;

    namespace resolve
    {
        struct ResolveContext;

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
    }
}

#endif // !DIAMOND_DOGS_LOOM_RESOLVE_PINS_HPP
