#pragma once
#ifndef DIAMOND_DOGS_LOOM_ORDER_EXPLAIN_HPP
#define DIAMOND_DOGS_LOOM_ORDER_EXPLAIN_HPP
#include "order/OrderedGraph.hpp"
#include "content/JsonWriter.hpp"
#include <cstdint>
#include <source_location>
#include <string>
#include <vector>

// Explain.cpp: why the order is what it is (Q3e). The run order, and each pass's edges with their reason and source
// location, as text or JSON.
namespace loom
{
    struct FrozenGraph;

    // Text: the run order, and each pass's incoming edges with reason and location. Then the anchor orders.
    std::string Explain(const FrozenGraph& frozen, const OrderedGraph& ordered);
    // One pass: its place in the order, the edges into it (upstream) and out of it (downstream). One hop each.
    std::string ExplainPass(const FrozenGraph& frozen, const OrderedGraph& ordered, PassIndex pass);
    // The same data for an agent: every pass with both edge lists, then the anchor orders
    foundation::content::JsonResult<std::string> ExplainJson(const FrozenGraph& frozen,
                                                             const OrderedGraph& ordered,
                                                             foundation::content::JsonStyle style);

    namespace order
    {
        enum class EdgeEnd : uint8_t
        {
            Invalid = 0,
            From = 1,
            To = 2
        };

        // Edges grouped by one endpoint (CSR): node n's edges are EdgeIndices[Offsets[n], Offsets[n + 1])
        struct EdgesByNode
        {
            std::vector<int32_t> Offsets;       // NodeCount + 1 entries
            std::vector<int32_t> EdgeIndices;   // indices into OrderedGraph::Edges
        };

        // One edge seen from one pass: Other is the node at the edge's far end
        struct ExplainedEdge
        {
            NodeIndex Other;
            EdgeReason Reason{ EdgeReason::Invalid };
            ResourceSlot Resource;      // DataFlow only
            std::source_location Where;
        };

        // FrameBounds edges are left out of both lists: every pass has them, so they explain nothing
        struct ExplainedPass
        {
            int32_t Position{ -1 };     // place in the run order
            PassIndex Pass;
            std::vector<ExplainedEdge> Incoming;    // the nodes that run before this pass, and why
            std::vector<ExplainedEdge> Outgoing;    // the nodes that run after it, and why
        };

        EdgesByNode GroupEdgesByNode(const std::vector<OrderEdge>& edges, int32_t node_count, EdgeEnd which_end);
        // One entry per pass, in run order
        std::vector<ExplainedPass> ExplainPasses(const FrozenGraph& frozen, const OrderedGraph& ordered);
    }

}

#endif // !DIAMOND_DOGS_LOOM_ORDER_EXPLAIN_HPP
