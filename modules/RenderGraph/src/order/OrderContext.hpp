#pragma once
#ifndef DIAMOND_DOGS_LOOM_ORDER_CONTEXT_HPP
#define DIAMOND_DOGS_LOOM_ORDER_CONTEXT_HPP
#include "order/OrderedGraph.hpp"
#include "LoomKeys.hpp"
#include "PassStatements.hpp"
#include "freeze/FrozenGraph.hpp"
#include <cstdint>
#include <optional>
#include <source_location>
#include <string_view>
#include <vector>

// The Order stage's internals, shared by its source files and by the reachability tests. The public part
// (OrderedGraph, Order) is in OrderedGraph.hpp.
namespace loom
{
    class DiagnosticSink;

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

        inline bool Reaches(const Reachability& reach, NodeIndex row, NodeIndex col)
        {
            // check: is bit `col` in row `row` set, which indicates whether that the pass at `row`
            // reaches the pass at `col`
            // row.value*reach.WordsPerNode: base offset to get to the start of row
            // col.value/64 = offset based on word size
            const uint64_t word = reach.Bits[(row.Value * reach.WordsPerNode) + (col.Value / 64)];
            const uint64_t mask = uint64_t{ 1 } << (col.Value % 64);
            return (word & mask) != 0;
        }

        inline bool Ordered(const Reachability& reach, NodeIndex lhs, NodeIndex rhs)
        {
            return Reaches(reach, lhs, rhs) || Reaches(reach, rhs, lhs);
        }

        // Reads context.Edges and context.Successors. ordered_nodes must be a topological order of every node.
        Reachability ComputeReachability(OrderContext& context, const std::vector<NodeIndex>& ordered_nodes);

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

        // Takes the FrozenGraph, not the context: Explain runs after Order() returns, when no context exists
        inline KeyRef ResourceOfSlot(const FrozenGraph& frozen, ResourceSlot slot)
        {
            const int32_t resourceCount = static_cast<int32_t>(frozen.Resources.size());
            // remember our slots are a kind of flattened index space where the first `resourceCount` slots correspond to actual resources
            // and the slots after that are for frame targets
            if (slot.Value < resourceCount)
            {
                return frozen.Resources[slot.Value].Key;
            }
            else
            {
                return frozen.FrameTargets[slot.Value - resourceCount].Key;
            }
        }

        inline KeyRef ResourceOfSlot(const OrderContext& context, ResourceSlot slot)
        {
            return ResourceOfSlot(context.Frozen, slot);
        }


        /** @brief After running `GatherAccesses`, we have a table indexed on
         * on the *pass*, where each row within that table contains a `PassAccess`
         * struct for each *SLOT* that is accessed by that pass.
         *
         * To complete the data flow analysis, we need to collate these accesses by slot,
         * creating two tables indexed on the *slot*, where each row contains the indices
         * of passes that either write to or read from that slot.
         */
        struct CollatedAccesses
        {
            // we store the actual accessIdx for retrieving `where` and other
            // data stored on the access struct as needed
            struct AccessRef
            {
                int32_t PassIdx;
                int32_t AccessIdx;
            };
            std::vector<std::vector<AccessRef>> Writers;
            std::vector<std::vector<AccessRef>> Readers;
        };

        struct Hazard
        {
            int32_t LowPass{ -1 };
            int32_t HighPass{ -1 };
            int32_t Slot{ -1 };
            std::source_location LowWhere;
            std::source_location HighWhere;
        };

        void AddEdge(OrderContext& context,
                     NodeIndex from,
                     NodeIndex to,
                     EdgeReason reason,
                     std::source_location where,
                     ResourceSlot slot = ResourceSlot{ -1 });
        void AddAnchorEdges(OrderContext& context);
        void AddFrameBoundEdges(OrderContext& context);
        void AddSequenceEdges(OrderContext& context);
        void AppendBindingAccesses(const std::vector<ResourceAccess>& inputs,
                                  std::vector<PassAccess>& results,
                                  const FrozenPass& pass);
        void AppendRecordAccesses(OrderContext& context,
                                  std::vector<PassAccess>& results,
                                  const FrozenPass& pass);
        ResourceAccess ColorTargetAccess(const FrozenColorTarget& color);
        ResourceAccess DepthTargetAccess(const FrozenDepthTarget& depth);
        void AppendDrawAccesses(const OrderContext& context,
                                std::vector<PassAccess>& results,
                                const FrozenPass& pass);
        void AppendDispatchAccesses(std::vector<PassAccess>& results, const FrozenPass& pass);
        void GatherAccesses(OrderContext& context);
        CollatedAccesses CollateAccessesBySlot(OrderContext& context);
        void AddDataFlowEdges(OrderContext& context, const CollatedAccesses& collated);
        void BuildSuccessors(OrderContext& context);
        std::optional<std::vector<NodeIndex>> SortNodes(OrderContext& context);
        void RecordHazard(const OrderContext& context,
                          std::vector<Hazard>& hazards,
                          int32_t slot,
                          const CollatedAccesses::AccessRef& lhs,
                          const CollatedAccesses::AccessRef& rhs);
        void ReportHazards(const OrderContext& context, const std::vector<Hazard>& hazards);
        std::vector<uint8_t> CheckHazards(OrderContext& context,
                                          const CollatedAccesses& collated,
                                          const Reachability& reachability);
        void ReportReadBeforeWrite(const OrderContext& context,
                                    const ResourceSlot slot,
                                    const CollatedAccesses::AccessRef& first_reader,
                                    const CollatedAccesses::AccessRef& first_writer);
        void CheckReadsBeforeWrites(OrderContext& context,
                                    const CollatedAccesses& collated,
                                    const std::vector<int32_t>& positions,
                                    const std::vector<uint8_t>& hazard_slots);
        // What LM504 learns about the writes, for LM505 to walk.
        struct Liveness
        {
            std::vector<std::vector<int32_t>> Feeders; // [reader pass] -> the writer passes whose write it reads
            std::vector<uint8_t> Roots;                // [pass] is 1 when a sink reads one of its writes
        };

        void ReportDeadWrite(const OrderContext& context,
                             ResourceSlot slot,
                             const CollatedAccesses::AccessRef& writer,
                             const CollatedAccesses::AccessRef& next_writer);
        Liveness CheckDeadWrites(OrderContext& context,
                                 const CollatedAccesses& collated,
                                 const std::vector<int32_t>& positions);
        void CheckOrphans(OrderContext& context, const Liveness& liveness);

        void ReportCycle(const OrderContext& context, const std::vector<NodeIndex>& emitted);
        void ReportCycleEdges(const OrderContext& context, const std::vector<int32_t>& cycle_edges);
        std::string_view EdgeReasonName(EdgeReason reason) noexcept;
        KeyRef PassOfEdge(const OrderContext& context, const OrderEdge& edge);
        
        std::vector<int32_t> PositionsOf(const OrderContext& context, const std::vector<NodeIndex>& ordered_nodes);
        // True when resource at the given slot has a source (for it's contents) from outside the frame/graph
        bool HasSource(const OrderContext& context, ResourceSlot slot);
        bool HasSink(const OrderContext& context, ResourceSlot slot);

        // Explain

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

} // namespace loom

#endif // !DIAMOND_DOGS_LOOM_ORDER_CONTEXT_HPP
