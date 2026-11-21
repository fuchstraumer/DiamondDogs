#pragma once
#ifndef DIAMOND_DOGS_LOOM_ORDER_SORT_HPP
#define DIAMOND_DOGS_LOOM_ORDER_SORT_HPP
#include "order/OrderContext.hpp"
#include "LoomKeys.hpp"
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

// Sort.cpp: what order, and what reaches what. The sort (Kahn), the cycle report (LM501), the closure, the levels, the
// run order, and positions.
namespace loom::order
{

    void BuildSuccessors(OrderContext& context);
    std::optional<std::vector<NodeIndex>> SortNodes(OrderContext& context);
    // Reads context.Edges and context.Successors. ordered_nodes must be a topological order of every node.
    Reachability ComputeReachability(OrderContext& context, const std::vector<NodeIndex>& ordered_nodes);

    void ReportCycle(const OrderContext& context, const std::vector<NodeIndex>& emitted);
    void ReportCycleEdges(const OrderContext& context, const std::vector<int32_t>& cycle_edges);
    std::string_view EdgeReasonName(EdgeReason reason) noexcept;
    KeyRef PassOfEdge(const OrderContext& context, const OrderEdge& edge);

    std::vector<int32_t> PositionsOf(const OrderContext& context, const std::vector<NodeIndex>& ordered_nodes);
    // [pass] the count of passes on the longest path into it. ordered_nodes is a topological order of every node.
    std::vector<int32_t> PassLevels(const OrderContext& context, const std::vector<NodeIndex>& ordered_nodes);
    // The run order: the pass nodes by level, then by declaration order inside a level.
    std::vector<NodeIndex> LevelMajorOrder(const OrderContext& context, const std::vector<int32_t>& levels);

}

#endif // !DIAMOND_DOGS_LOOM_ORDER_SORT_HPP
