#pragma once
#ifndef DIAMOND_DOGS_LOOM_ORDERED_GRAPH_HPP
#define DIAMOND_DOGS_LOOM_ORDERED_GRAPH_HPP
#include "PassStatements.hpp"
#include "freeze/FrozenGraph.hpp"
#include <cstdint>
#include <optional>
#include <source_location>
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
        DataFlow = 7
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

    struct OrderedGraph
    {
        std::vector<PassIndex> Passes;
        std::vector<OrderEdge> Edges;
    };

    std::optional<OrderedGraph> Order(const FrozenGraph& frozen,
                                      const BindAccessTable& binds,
                                      DiagnosticSink& sink);
    
    namespace order
    {
        struct PassAccess
        {
            ResourceSlot Slot;
            ResourceAccess Access{ ResourceAccess::Invalid };
            bool Previous{ false };
            std::source_location Where;
        };

        struct OrderContext
        {
            const FrozenGraph& Frozen;
            const BindAccessTable& Binds;
            DiagnosticSink& Sink;

            int32_t PassCount{ -1 };
            int32_t NodeCount{ -1 };
            int32_t SlotCount{ -1 };

            std::vector<std::vector<PassAccess>> PassAccesses;
            std::vector<OrderEdge> Edges;
            std::vector<std::vector<int32_t>> Successors; // [node] -> indices into edges
        };

        // Row n holds the nodes that n reaches: bits[n*WordsPerNode, (n+1) * WordsPerNode)
        struct Reachability
        {
            int64_t WordsPerNode{ -1 };
            std::vector<uint64_t> Bits; // effectively flattened bit matrix
        };

        inline NodeIndex NodeOfPass(PassIndex pass)
        {
            return NodeIndex{ pass.Value };
        }

        inline NodeIndex NodeOfAnchor(const OrderContext& context, AnchorIndex anchor)
        {
            const int32_t offset = context.PassCount;
            return NodeIndex{ offset + anchor.Value };
        }

        inline ResourceSlot SlotOfResource(ResourceIndex resource)
        {
            return ResourceSlot{ resource.Value };
        }

        inline ResourceSlot SlotOfFrameTarget(const OrderContext& context, FrameTargetIndex target)
        {
            const int32_t offset = static_cast<int32_t>(context.Frozen.Resources.size());
            return ResourceSlot{ offset + target.Value };
        }
    }

} // namespace loom

#endif // !DIAMOND_DOGS_LOOM_ORDERED_GRAPH_HPP
