#include "order/OrderContext.hpp"
#include "freeze/FrozenGraph.hpp"
#include "order/OrderedGraph.hpp"
#include <cstdint>
#include <iterator>
#include <source_location>

// Where edges come from: one builder per EdgeReason.

namespace loom::order
{

    void AddEdge(OrderContext& context,
                    NodeIndex from,
                    NodeIndex to,
                    EdgeReason reason,
                    std::source_location where,
                    ResourceSlot slot)
    {
        context.Edges.emplace_back(from,
                                    to,
                                    reason,
                                    slot,
                                    where);
    }

    void AddAnchorEdges(OrderContext& context)
    {
        for (const FrozenAnchorOrder& anchorOrder : context.Frozen.AnchorOrders)
        {
            const NodeIndex earlyNode = NodeOfAnchor(context, anchorOrder.Earlier);
            const NodeIndex lateNode = NodeOfAnchor(context, anchorOrder.Later);
            AddEdge(context, earlyNode, lateNode, EdgeReason::AnchorOrder, anchorOrder.Where);
        }

        for (int32_t i = 0; i < context.PassCount; ++i)
        {
            const FrozenPass& pass = context.Frozen.Passes[i];
            const NodeIndex passNode = NodeOfPass(PassIndex{ i });

            for (const FrozenAnchorUse& use : pass.After)
            {
                const NodeIndex earlierNode = NodeOfAnchor(context, use.Anchor);
                AddEdge(context, earlierNode, passNode, EdgeReason::After, use.Where);
            }

            for (const FrozenAnchorUse& use : pass.Before)
            {
                const NodeIndex laterNode = NodeOfAnchor(context, use.Anchor);
                AddEdge(context, passNode, laterNode, EdgeReason::Before, use.Where);
            }
        }
    }

    void AddFrameBoundEdges(OrderContext& context)
    {
        const NodeIndex begin = NodeOfAnchor(context, k_BeginFrameAnchorIndex);
        const NodeIndex end = NodeOfAnchor(context, k_EndFrameAnchorIndex);

        for (int32_t i = 0; i < context.PassCount; ++i)
        {
            const NodeIndex passNode = NodeOfPass(PassIndex{ i });
            const std::source_location& where = context.Frozen.Passes[i].Where;

            // every pass gets these anchors - because they all have to occur within these bounds
            AddEdge(context, begin, passNode, EdgeReason::FrameBounds, where);
            AddEdge(context, passNode, end, EdgeReason::FrameBounds, where);
        }

        // This walk has to start after the index of the built-in anchors - otherwise we'd be adding
        // cyclic deps of these anchors to themselves!
        for (int32_t anchorIndex = k_EndFrameAnchorIndex.Value + 1; anchorIndex < std::ssize(context.Frozen.Anchors); ++anchorIndex)
        {
            const NodeIndex anchorNode = NodeOfAnchor(context, AnchorIndex{ anchorIndex });
            const std::source_location& where = context.Frozen.Anchors[anchorIndex].Where;
            // and now it really is just this simple: add the frame ordering anchors, exactly as above.
            AddEdge(context, begin, anchorNode, EdgeReason::FrameBounds, where);
            AddEdge(context, anchorNode, end, EdgeReason::FrameBounds, where);
        }
    }

    void AddSequenceEdges(OrderContext& context)
    {
        // first step: chain the passes within each sequence
        // second step: the sequences own `After` `Before` anchors
        // third step: the exposed anchors, finally!
        // realize that a sequence is not *itself* a node in the grpah: it is manifested
        // through the passes it "contains" (refers to as a span, really)
        // this in turn is why we don't need to add the frame bounds here -
        // the passes already did that!
        for (const FrozenSequence& sequence : context.Frozen.Sequences)
        {
            const int32_t firstPass = sequence.FirstPass.Value;
            const int32_t lastPass = firstPass + sequence.PassCount - 1;
            // we don't touch the actual last pass, because pass+1 there is not in our sequence!
            for (int32_t passIdx = firstPass; passIdx < lastPass; ++passIdx)
            {
                const NodeIndex currPassNode = NodeOfPass(PassIndex{ passIdx });
                const NodeIndex nextPassNode = NodeOfPass(PassIndex{ passIdx + 1 });
                // Where is the *later* passes location, bc that's what *SET* the position!
                const std::source_location& where = context.Frozen.Passes[passIdx + 1].Where;
                AddEdge(context, currPassNode, nextPassNode, EdgeReason::SequenceChain, where);
            }

            const NodeIndex firstPassNode = NodeOfPass(PassIndex{ firstPass });
            // now we need to add the sequence's own anchors
            for (const FrozenAnchorUse& anchor : sequence.After)
            {
                const NodeIndex anchorNode = NodeOfAnchor(context, anchor.Anchor);
                AddEdge(context, anchorNode, firstPassNode, EdgeReason::After, anchor.Where);
            }

            // but now we use the actual last pass - to make sure that it still receives the proper
            // edges and ordering that it needs!
            const NodeIndex lastPassNode = NodeOfPass(PassIndex{ lastPass });
            for (const FrozenAnchorUse& anchor : sequence.Before)
            {
                const NodeIndex anchorNode = NodeOfAnchor(context, anchor.Anchor);
                AddEdge(context, lastPassNode, anchorNode, EdgeReason::Before, anchor.Where);
            }

            // ExposedAnchors hold `Position`, the location of the anchor within this position.
            // This means that the ordering is relative to this sequence, so based on the value
            // of each position `k` , we need to apply before/after ordering as needed.
            for (const auto& anchorIdx : sequence.ExposedAnchors)
            {
                // Generate a lazy view of passes within this sequence - should make building the ordering edges a little easier
                // to follow along with
                const FrozenAnchor& anchor = context.Frozen.Anchors[anchorIdx.Value];
                const NodeIndex anchorNode = NodeOfAnchor(context, anchorIdx);

                if (anchor.Position > 0)
                {
                    const NodeIndex passNode = NodeOfPass(PassIndex{ firstPass + anchor.Position - 1 });
                    AddEdge(context, passNode, anchorNode, EdgeReason::ExposedAnchor, anchor.Where);
                }

                if (anchor.Position < sequence.PassCount)
                {
                    const NodeIndex passNode = NodeOfPass(PassIndex{ firstPass + anchor.Position });
                    AddEdge(context, anchorNode, passNode, EdgeReason::ExposedAnchor, anchor.Where);
                }
            }
        }
    }

    void AddDataFlowEdges(OrderContext& context, const CollatedAccesses& collated)
    {
        // now that we have collated accesses by slot, we can iterate over each slot
        // and determine the data flow edges based on the read and write accesses for that slot
        for (int32_t slotIdx = 0; slotIdx < context.SlotCount; ++slotIdx)
        {
            const auto& readers = collated.Readers[slotIdx];
            const auto& writers = collated.Writers[slotIdx];
            if (writers.size() != 1)
            {
                continue;
            }

            const CollatedAccesses::AccessRef& writerRef = writers.front();
            const PassIndex writerPass{ writerRef.PassIdx };
            for (const auto& reader : readers)
            {
                const PassIndex readerPass{ reader.PassIdx };
                if (readerPass == writerPass)
                {
                    continue;
                }

                const auto& accesses = context.PassAccesses[readerPass.Value];
                const auto& where = accesses[reader.AccessIdx].Where;
                AddEdge(context,
                        NodeOfPass(writerPass),
                        NodeOfPass(readerPass),
                        EdgeReason::DataFlow,
                        where,
                        ResourceSlot{ slotIdx });
            }
        }
    }

}


