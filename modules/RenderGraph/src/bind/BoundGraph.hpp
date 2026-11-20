#pragma once
#ifndef DIAMOND_DOGS_LOOM_BOUND_GRAPH_HPP
#define DIAMOND_DOGS_LOOM_BOUND_GRAPH_HPP
#include "ShaderLibraryTypes.hpp"
#include "PassStatements.hpp"
#include "order/OrderedGraph.hpp"
#include <cstdint>
#include <optional>
#include <vector>

// The Bind stage's output. Bind maps each Bind("name", key) of a pass to one binding of the variant that Resolve
// selected, once, at Compile. After Bind, no later stage reads a binding name (LM301 to LM305).
namespace loom
{
    class DiagnosticSink;
    struct FrozenGraph;
    struct ResolvedGraph;

    // One resource of one pass's pipeline, in placement order (guide section 3). A Bound resource is a descriptor at
    // (Group, Binding). A Handle or Pointer resource is an 8-byte value that Loom writes at ValueOffset in Container;
    // Group and Binding then name the uniform buffer that holds it, or are 0 when Container is PushConstants.
    struct BindingSlot
    {
        uint32_t Group{ 0u };
        uint32_t Binding{ 0u };
        lodestone::PlacementKind Form{ lodestone::PlacementKind::None };
        lodestone::StoredValueContainer Container{ lodestone::StoredValueContainer::None };
        uint32_t ValueOffset{ 0u };
        lodestone::BindingKind Kind{ lodestone::BindingKind::Invalid };
        ResourceAccess Access{ ResourceAccess::Invalid };       // after the V1 narrowing
        lodestone::ShaderStageKind Stages{ lodestone::ShaderStageKind::Invalid };
        uint32_t ArrayCount{ 0u };                              // 0: unbounded
        ResourceIndex Resource;                                 // the bound key's row in FrozenGraph::Resources
    };

    // The entry point constants of one pass. ParamsIndex points into FrozenPass::Params, and is -1 when the pass has no
    // Params statement (its shader then has no constants of its own, at most stored values that Loom writes).
    struct BoundParams
    {
        lodestone::EntryPointConstantsDelivery Delivery{ lodestone::EntryPointConstantsDelivery::None };
        uint32_t ByteSize{ 0u };
        int32_t ParamsIndex{ -1 };
    };

    // One row for each FrozenGraph pass, at the same index. A pass's slots are Slots[FirstSlot, FirstSlot + SlotCount).
    // A Record pass has no slots.
    struct BoundPass
    {
        int32_t FirstSlot{ 0 };
        int32_t SlotCount{ 0 };
        BoundParams Params;
    };

    // Accesses is Order's input: one row per pass, one entry per FrozenPass::Binds, the access after narrowing. A bind
    // whose binding is in other variants only gets Invalid, and Order skips it.
    struct BoundGraph
    {
        std::vector<BindingSlot> Slots;
        std::vector<BoundPass> Passes;
        BindAccessTable Accesses;
    };

    // Reports every Bind error it finds, not only the first one. Returns no graph when it reported an error. Keep the
    // bundle open while the sink holds diagnostics: their binding names are views into it.
    [[nodiscard]] std::optional<BoundGraph> Bind(const FrozenGraph& frozen,
                                                 const ResolvedGraph& resolved,
                                                 DiagnosticSink& sink);

}

#endif // !DIAMOND_DOGS_LOOM_BOUND_GRAPH_HPP
