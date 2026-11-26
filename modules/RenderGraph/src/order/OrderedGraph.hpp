#pragma once
#ifndef DIAMOND_DOGS_LOOM_ORDERED_GRAPH_HPP
#define DIAMOND_DOGS_LOOM_ORDERED_GRAPH_HPP
#include "PassStatements.hpp"
#include "freeze/FrozenGraph.hpp"
#include <cstdint>
#include <optional>
#include <source_location>
#include <string>
#include <vector>

namespace loom
{
    class DiagnosticSink;

    // Helps us to understand - especially during error/explain prints - why an edge record
    // exists by stating what the ordering pass found during it's traversal
    enum class EdgeReason : uint8_t
    {
        Invalid = 0,
        After = 1, // pass.After(a), or first pass of sequence
        Before = 2, // pass.Before(a), or last pass of sequence
        AnchorOrder = 3, // OrderAnchors(early, late)
        FrameBounds = 4, // BeginFrame, EndFrame
        SequenceChain = 5, // pass[i] -> pass[i+1] chain
        ExposedAnchor = 6, 
        DataFlow = 7,
        FrameStart = 8 // a BindFrameStart reader before each writer of the slot
    };

    using NodeIndex = FrozenIndex<struct NodeIndexTag>;
    using ResourceSlot = FrozenIndex<struct ResourceSlotTag>;

    struct OrderEdge
    {
        NodeIndex From{ -1 };
        NodeIndex To{ -1 };
        EdgeReason Reason{ EdgeReason::Invalid };
        ResourceSlot Resource; // only for EdgeReason::DataFlow
        std::source_location Where;
    };

    struct BindAccessTable
    {
        // will likely flatten later
        std::vector<std::vector<ResourceAccess>> Accesses;
    };

    // Passes is the run order: level first, then declaration order inside a level. A pass's level is the count of
    // passes on the longest ordering path into it; anchors add nothing. Two passes in one level have no path between
    // them, so LM502 proves they have no hazard, and the barriers go only at level boundaries.
    struct OrderedGraph
    {
        std::vector<PassIndex> Passes;
        std::vector<OrderEdge> Edges;
        std::vector<int32_t> Levels;        // [pass] its level, from 0
        int32_t LevelCount{ 0 };
    };

    // Explain, ExplainPass and ExplainJson are in Explain.hpp (Q3e).
    std::optional<OrderedGraph> Order(const FrozenGraph& frozen,
                                      const BindAccessTable& binds,
                                      DiagnosticSink& sink);

} // namespace loom

#endif // !DIAMOND_DOGS_LOOM_ORDERED_GRAPH_HPP
