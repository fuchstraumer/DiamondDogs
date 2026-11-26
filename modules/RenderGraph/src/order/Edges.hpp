#pragma once
#ifndef DIAMOND_DOGS_LOOM_ORDER_EDGES_HPP
#define DIAMOND_DOGS_LOOM_ORDER_EDGES_HPP
#include "order/OrderContext.hpp"
#include <source_location>

// Edges.cpp: the ordering edges, from anchors, frame bounds, sequences, data flow (Q3h) and frame-start reads.
namespace loom::order
{

    void AddEdge(OrderContext& context,
                 NodeIndex from,
                 NodeIndex to,
                 EdgeReason reason,
                 std::source_location where,
                 ResourceSlot slot = ResourceSlot{ -1 });
    void AddAnchorEdges(OrderContext& context);
    void AddFrameBoundEdges(OrderContext& context);
    void AddSequenceEdges(OrderContext& context);
    void AddDataFlowEdges(OrderContext& context, const CollatedAccesses& collated);
    void AddFrameStartEdges(OrderContext& context, const CollatedAccesses& collated);

}

#endif // !DIAMOND_DOGS_LOOM_ORDER_EDGES_HPP
