#pragma once
#ifndef DIAMOND_DOGS_LOOM_ORDER_CONTEXT_HPP
#define DIAMOND_DOGS_LOOM_ORDER_CONTEXT_HPP
#include "order/OrderedGraph.hpp"
#include "containers/SmallVector.hpp"
#include "LoomKeys.hpp"
#include "PassStatements.hpp"
#include "freeze/FrozenGraph.hpp"
#include <cstdint>
#include <source_location>
#include <vector>

// The Order stage's shared state: the context its steps pass along, the reachability matrix, the per-slot access
// tables, and the index helpers. Each step's functions are in the header named for its source file (Accesses.hpp,
// Edges.hpp, Sort.hpp, DataChecks.hpp, Liveness.hpp, Explain.hpp). The public part (OrderedGraph, Order) is in
// OrderedGraph.hpp.
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
            // 64 bytes: copying this struct is vectorized entirely
            using AccessRefVec = foundation::SmallVector<AccessRef, 8>;
            std::vector<AccessRefVec> Writers;
            std::vector<AccessRefVec> Readers;
        };

    }

} // namespace loom

#endif // !DIAMOND_DOGS_LOOM_ORDER_CONTEXT_HPP
