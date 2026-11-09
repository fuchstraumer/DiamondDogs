#include "order/OrderedGraph.hpp"
#include "Declarations.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "LoomKeys.hpp"
#include "PassStatements.hpp"
#include "Records.hpp"
#include "freeze/FrozenGraph.hpp"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <optional>
#include <queue>
#include <ranges>
#include <source_location>
#include <span>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace loom
{
    namespace order
    {
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
        
    }

    std::optional<OrderedGraph> Order(const FrozenGraph& frozen,
                                      const BindAccessTable& binds,
                                      DiagnosticSink& sink)
    {
        using namespace order;
        const int32_t nodeCount = static_cast<int32_t>(frozen.Passes.size()) +
                                  static_cast<int32_t>(frozen.Anchors.size());
        const int32_t slotCount = static_cast<int32_t>(frozen.Resources.size()) +
                                  static_cast<int32_t>(frozen.FrameTargets.size());
        OrderContext context
        {
            .Frozen=frozen,
            .Binds=binds,
            .Sink=sink,
            .PassCount=static_cast<int32_t>(frozen.Passes.size()),
            .NodeCount=nodeCount,
            .SlotCount=slotCount,
        };
        context.PassAccesses.resize(context.PassCount, {});
        for (auto& vec : context.PassAccesses)
        {
            // should be overkill in most cases, but still should be of aid
            // (plus, it's a vector of vectors... repeated reallocs are easy to make happen)
            vec.reserve(16u);
        }

        context.Edges.reserve(context.PassCount * 4);

        AddAnchorEdges(context);
        AddFrameBoundEdges(context);
        AddSequenceEdges(context);
        GatherAccesses(context);
        // data indexed by pass -> data indexed by slot
        CollatedAccesses collated = CollateAccessesBySlot(context);
        AddDataFlowEdges(context, collated);
        BuildSuccessors(context);
        auto sortNodesResult = SortNodes(context);
        if (!sortNodesResult.has_value())
        {
            return std::nullopt;
        }
        else
        {
            const auto& orderedNodes = *sortNodesResult;
            const Reachability reachability = ComputeReachability(context, orderedNodes);

            const int32_t errorsBefore = context.Sink.ErrorCount();
            const std::vector<uint8_t> hazardSlots = CheckHazards(context, collated, reachability);
            const std::vector<int32_t> positions = PositionsOf(context, orderedNodes);
            CheckReadsBeforeWrites(context, collated, positions, hazardSlots);

            // if we found hazards, then we can't return a valid ordered graph
            if (context.Sink.ErrorCount() != errorsBefore)
            {
                return std::nullopt;
            }

            // The warnings run only on a graph with no errors: the author fixes those first, and LM504 needs every
            // pair with a writer to be ordered, which a clean LM502 proves
            const Liveness liveness = CheckDeadWrites(context, collated, positions);
            CheckOrphans(context, liveness);

            // copy each pass node (pass.Value < context.PassCount) into result passes
            OrderedGraph result;
            auto filterFn = [&](NodeIndex node) { return node.Value < context.PassCount; };
            auto toPassIdx = [&](NodeIndex node) { return PassIndex{ node.Value }; };
            // important: filter *then* transform, otherwise you pay a perf penalty
            result.Passes = orderedNodes |
                            std::views::filter(filterFn) |
                            std::views::transform(toPassIdx) |
                            std::ranges::to<std::vector>();
            result.Edges = std::move(context.Edges);
            return result;
        }
    }

    namespace order
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

        void AppendBindingAccesses(const std::vector<ResourceAccess>& inputs,
                                  std::vector<PassAccess>& results,
                                  const FrozenPass& pass)
        {
            for (auto&& [bindIdx, binding] : std::views::enumerate(pass.Binds))
            {
                const ResourceSlot slot = SlotOfResource(binding.Resource);
                const ResourceAccess& access = inputs[bindIdx];
                PassAccess result
                {
                    .Slot = slot,
                    .Access = access,
                    .Previous = binding.Previous,
                    .Where = binding.Where
                };
                results.emplace_back(result);
            }
        }

        void AppendRecordAccesses(OrderContext& context,
                                  std::vector<PassAccess>& results,
                                  const FrozenPass& pass)
        {
            for (auto&& [recordIdx, record] : std::views::enumerate(pass.Uses))
            {
                ResourceSlot slot{};
                if (record.Target != TargetKind::FrameTarget)
                {
                    slot = SlotOfResource(record.Resource);
                }
                else
                {
                    slot = SlotOfFrameTarget(context, record.FrameTarget);
                }

                results.emplace_back(PassAccess{ .Slot = slot,
                                                 .Access = record.Access,
                                                 .Previous = false,
                                                 .Where = record.Where });
            }
        }

        // A ThreadsOver dispatch names its image only for the extent, so the image is not an access.
        void AppendDispatchAccesses(std::vector<PassAccess>& results, const FrozenPass& pass)
        {
            if (pass.Dispatch.Form != DispatchForm::Indirect)
            {
                return;
            }

            results.push_back(PassAccess{ .Slot = SlotOfResource(pass.Dispatch.Resource),
                                          .Access = ResourceAccess::Read,
                                          .Previous = false,
                                          .Where = pass.Dispatch.Where });
        }

        // Keep reads the old contents, and so does any blend but Opaque. Clear and DontCare never read them.
        ResourceAccess ColorTargetAccess(const FrozenColorTarget& color)
        {
            const bool readsOld = color.Load.Kind == ColorLoad::Kind::Keep || color.Blend != BlendMode::Opaque;
            return readsOld ? ResourceAccess::ReadWrite : ResourceAccess::Write;
        }

        // Keep with writes off is a depth test only. A Clear writes the target, even with depth writes off.
        ResourceAccess DepthTargetAccess(const FrozenDepthTarget& depth)
        {
            const bool keeps = depth.Load.Kind == DepthLoad::Kind::Keep;
            const bool writes = depth.Write == DepthWrite::Enabled;

            if (keeps)
            {
                return writes ? ResourceAccess::ReadWrite : ResourceAccess::Read;
            }

            return ResourceAccess::Write;
        }

        void AppendDrawAccesses(const OrderContext& context, std::vector<PassAccess>& results, const FrozenPass& pass)
        {
            if (pass.Draw.Form == DrawForm::DrawIndirect)
            {
                results.push_back(PassAccess{ .Slot = SlotOfResource(pass.Draw.Arguments),
                                              .Access = ResourceAccess::Read,
                                              .Previous = false,
                                              .Where = pass.Draw.Where });
            }

            for (const FrozenVertexBuffer& vertex : pass.VertexBuffers)
            {
                results.push_back(PassAccess{ .Slot = SlotOfResource(vertex.Buffer),
                                              .Access = ResourceAccess::Read,
                                              .Previous = false,
                                              .Where = vertex.Where });
            }

            if (pass.Index.Buffer.IsValid())
            {
                results.push_back(PassAccess{ .Slot = SlotOfResource(pass.Index.Buffer),
                                              .Access = ResourceAccess::Read,
                                              .Previous = false,
                                              .Where = pass.Index.Where });
            }

            for (const FrozenColorTarget& color : pass.Colors)
            {
                const ResourceSlot slot = color.Kind == TargetKind::FrameTarget
                                              ? SlotOfFrameTarget(context, color.FrameTarget)
                                              : SlotOfResource(color.Image);

                results.push_back(PassAccess{ .Slot = slot,
                                              .Access = ColorTargetAccess(color),
                                              .Previous = false,
                                              .Where = color.Where });
            }

            if (pass.Depth.Image.IsValid())
            {
                results.push_back(PassAccess{ .Slot = SlotOfResource(pass.Depth.Image),
                                              .Access = DepthTargetAccess(pass.Depth),
                                              .Previous = false,
                                              .Where = pass.Depth.Where });
            }
        }

        // One PassAccess per resource touch, from every statement that can name a resource. The fields of the other
        // pass kinds keep their invalid values, so each Append adds nothing for a pass of another kind.
        void GatherAccesses(OrderContext& context)
        {
            for (int32_t i = 0; i < context.PassCount; ++i)
            {
                // the bind access information coming *in* to this pass/stage
                const std::vector<ResourceAccess>& inputs = context.Binds.Accesses[i];
                // Where we will write results
                std::vector<PassAccess>& results = context.PassAccesses[i];
                const FrozenPass& pass = context.Frozen.Passes[i];
                assert(inputs.size() == pass.Binds.size());

                AppendBindingAccesses(inputs, results, pass);
                AppendRecordAccesses(context, results, pass);
                AppendDispatchAccesses(results, pass);
                AppendDrawAccesses(context, results, pass);
            }
        }

        CollatedAccesses CollateAccessesBySlot(OrderContext& context)
        {
            using AccessRef = CollatedAccesses::AccessRef;
            CollatedAccesses collated;
            collated.Readers.resize(context.SlotCount);
            collated.Writers.resize(context.SlotCount);

            for (int32_t passIdx = 0; passIdx < context.PassCount; ++passIdx)
            {
                for (auto&& [accessIdx, access] : std::views::enumerate(context.PassAccesses[passIdx]))
                {
                    const int32_t slotIdx = access.Slot.Value;
                    std::vector<AccessRef>& readers = collated.Readers[slotIdx];
                    std::vector<AccessRef>& writers = collated.Writers[slotIdx];

                    if (access.Previous)
                    {
                        continue;
                    }

                    auto pushReader = [&readers, passIdx, accessIdx] ()
                    {
                        if (!readers.empty() && (readers.back().PassIdx == passIdx))
                        {
                            return;
                        }

                        // have to cast bc enumerate gives us ptrdiff_t
                        AccessRef readData
                        {
                            .PassIdx = passIdx,
                            .AccessIdx = static_cast<int32_t>(accessIdx)
                        };

                        readers.push_back(readData);
                    };

                    auto pushWriter = [&writers, passIdx, accessIdx] ()
                    {
                        if (!writers.empty() && (writers.back().PassIdx == passIdx))
                        {
                            return;
                        }

                        AccessRef writeData
                        {
                            .PassIdx = passIdx,
                            .AccessIdx = static_cast<int32_t>(accessIdx)
                        };

                        writers.push_back(writeData);
                    };

                    switch (access.Access)
                    {
                    case ResourceAccess::Read:
                        pushReader();
                        break;
                    case ResourceAccess::Write:
                        pushWriter();
                        break;
                    case ResourceAccess::ReadWrite:
                        pushReader();
                        pushWriter();
                        break;
                    case ResourceAccess::Invalid:
                        assert(false && "Invalid resource access");
                        break;
                    }
                }
            }

            return collated;
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

        void BuildSuccessors(OrderContext& context)
        {
            context.Successors.assign(context.NodeCount, {});

            for (int32_t edgeIndex = 0; edgeIndex < std::ssize(context.Edges); ++edgeIndex)
            {
                const OrderEdge& currEdge = context.Edges[edgeIndex];
                const int32_t fromIdx = currEdge.From.Value;
                context.Successors[fromIdx].emplace_back(edgeIndex);
            }
        }

        std::optional<std::vector<NodeIndex>> SortNodes(OrderContext& context)
        {
            // Build in-degree counts for each node based on traversing the edges.
            std::vector<int32_t> inDegree(context.NodeCount, 0);
            for (const OrderEdge& edge : context.Edges)
            {
                const int32_t toIdx = edge.To.Value;
                ++inDegree[toIdx];
            }

            using MinHeap = std::priority_queue<int32_t, std::vector<int32_t>, std::greater<>>;
            // get the initial nodes of degree 0
            auto filterFn = [&inDegree](int32_t idx)->bool
            {
                return inDegree[idx] == 0;
            };
            auto zeroInDegreeNodes = std::views::iota(int32_t{0}, context.NodeCount) |
                                     std::views::filter(filterFn);
            MinHeap readyNodes(zeroInDegreeNodes.begin(), zeroInDegreeNodes.end());

            std::vector<NodeIndex> sortedNodes;

            while (!readyNodes.empty())
            {
                const int32_t currNodeIdx = readyNodes.top();
                readyNodes.pop();

                sortedNodes.push_back(NodeIndex{ currNodeIdx });
                // okay, now we need to go update the successors!
                // remember that successors was written with fromIdx, so this list
                // gives us all the edges pointing *TO* a node from currNode
                const auto& nodeSuccessors = context.Successors[currNodeIdx];
                for (const int32_t edgeIdx : nodeSuccessors)
                {
                    const OrderEdge& edge = context.Edges[edgeIdx];
                    const int32_t toIdx = edge.To.Value;
                    --inDegree[toIdx];
                    if (inDegree[toIdx] == 0)
                    {
                        readyNodes.push(toIdx);
                    }
                }
            }

            if (std::ssize(sortedNodes) != context.NodeCount)
            {
                // there was a cycle!
                ReportCycle(context, sortedNodes);
                return std::nullopt;
            }

            return sortedNodes;
        }

        Reachability ComputeReachability(OrderContext& context, const std::vector<NodeIndex>& ordered_nodes)
        {
            Reachability result;
            result.WordsPerNode = (int64_t{ context.NodeCount } + 63) / int64_t{ 64 };
            const size_t numEntries = static_cast<size_t>(context.NodeCount * result.WordsPerNode);
            result.Bits.assign(numEntries, 0ull);

            // safe to use one span the whole time here, and just "copy" from it to subspan as needed
            // bc we fully allocate upfront. so that's nice!
            std::span<uint64_t> allBits{ result.Bits };

            for (const NodeIndex node : ordered_nodes | std::views::reverse)
            {
                const size_t offset = static_cast<size_t>(node.Value * result.WordsPerNode);
                const std::span<uint64_t> row = allBits.subspan(offset,
                                                                static_cast<size_t>(result.WordsPerNode));
                for (const int32_t edgeIndex : context.Successors[node.Value])
                {
                    const int32_t successor = context.Edges[edgeIndex].To.Value;
                    row[successor / 64] |= 1ull << (successor % 64);
                    const size_t successorOffset = static_cast<size_t>(successor * result.WordsPerNode);
                    const std::span<const uint64_t> successorRow = allBits.subspan(successorOffset,
                                                                                   static_cast<size_t>(result.WordsPerNode));
                    for (int64_t word = 0; word < result.WordsPerNode; ++word)
                    {
                        row[word] |= successorRow[word];
                    }
                }
            }

            return result;
        }

        void RecordHazard(const OrderContext& context,
                          std::vector<Hazard>& hazards,
                          int32_t slot,
                          const CollatedAccesses::AccessRef& lhs,
                          const CollatedAccesses::AccessRef& rhs)
        {
            const bool inOrder = lhs.PassIdx < rhs.PassIdx;
            const CollatedAccesses::AccessRef& low = inOrder ? lhs : rhs;
            const CollatedAccesses::AccessRef& high = inOrder ? rhs : lhs;
            const std::source_location& lowWhere = context.PassAccesses[low.PassIdx][low.AccessIdx].Where;
            const std::source_location& highWhere = context.PassAccesses[high.PassIdx][high.AccessIdx].Where;
            hazards.push_back({ .LowPass=low.PassIdx, .HighPass=high.PassIdx, .Slot=slot, .LowWhere=lowWhere, .HighWhere=highWhere });
        }

        void ReportHazards(const OrderContext& context, const std::vector<Hazard>& hazards)
        {
            for (size_t i = 0; i < hazards.size(); ++i)
            {
                const Hazard& curr = hazards[i];
                const bool samePair = (i > 0) &&
                                      (curr.LowPass == hazards[i - 1].LowPass) &&
                                      (curr.HighPass == hazards[i - 1].HighPass);

                if (samePair && (hazards[i - 1].Slot == curr.Slot))
                {
                    continue;
                }

                Diagnostic diagnostic
                {
                    .Code = DiagnosticCode::LM502,
                    .Severity = samePair ? DiagnosticSeverity::Note : DiagnosticSeverity::Error,
                    .Where = curr.LowWhere,
                    .OtherWhere = curr.HighWhere,
                    .Pass = context.Frozen.Passes[curr.LowPass].Key,
                    .Resource = ResourceOfSlot(context, ResourceSlot{ curr.Slot })
                };

                DiagnosticValue diagValue
                {
                    .FieldName = "OtherPass",
                    .Kind = DiagnosticValueKind::Text,
                    .Text = context.Frozen.Passes[curr.HighPass].Key.Name
                };

                diagnostic.Actual = diagValue;

                context.Sink.Report(diagnostic);
            }
        }

        std::vector<uint8_t> CheckHazards(OrderContext& context, const CollatedAccesses& collated, const Reachability& reachability)
        {
            std::vector<Hazard> hazards;

            for (int32_t slot = 0; slot < context.SlotCount; ++slot)
            {
                const auto& writers = collated.Writers[slot];
                const auto& readers = collated.Readers[slot];

                // no writers: there's no way to have hazards if this slot is only read
                // one writer: `AddDataFlowEdges` would have already added the necessary
                // edges to prevent hazards, and this check runs after the sort. that means
                // we already know for a fact that the writer is ordered correctly
                if (writers.size() <= 1)
                {
                    continue;
                }

                // write-write: ever pair of writer passes
                for (size_t first = 0; first < writers.size(); ++first)
                {
                    const NodeIndex firstNode = NodeOfPass(PassIndex{ writers[first].PassIdx });
                    for (size_t second = first + 1; second < writers.size(); ++second)
                    {
                        const NodeIndex secondNode{ writers[second].PassIdx };
                        if (!Ordered(reachability, firstNode, secondNode))
                        {
                            RecordHazard(context, hazards, slot, writers[first], writers[second]);
                        }
                    }
                }

                // write-read: every writer against every reader
                for (const auto writer : writers)
                {
                    const NodeIndex writerNode{ writer.PassIdx };

                    for (const auto reader : readers)
                    {
                        const NodeIndex readerNode{ reader.PassIdx };
                        // If we are investigating a read-write resource,
                        // we need to check for self-reference
                        if (writerNode == readerNode)
                        {
                            continue;
                        }

                        if (!Ordered(reachability, writerNode, readerNode))
                        {
                            RecordHazard(context, hazards, slot, writer, reader);
                        }
                    }
                }
            }

            const auto hazardKey = [](const Hazard& hazard)
            {
                return std::tuple{ hazard.LowPass, hazard.HighPass, hazard.Slot };
            };
            // using hazardKey extracts the three numeric components of the hazard that we can 
            // use to sort (and `{}` becomes default sort comparator, std::less<> by default)
            std::ranges::stable_sort(hazards, {}, hazardKey);
            ReportHazards(context, hazards);
            
            std::vector<uint8_t> hazardSlots(context.SlotCount, 0);
            for (const auto& hazard : hazards)
            {
                hazardSlots[hazard.Slot] = 1;
            }

            return hazardSlots;
        }

        void ReportReadBeforeWrite(const OrderContext& context,
                                    const ResourceSlot slot,
                                    const CollatedAccesses::AccessRef& first_reader,
                                    const CollatedAccesses::AccessRef& first_writer)
        {
            const auto& passAccesses = context.PassAccesses[first_reader.PassIdx];
            const auto& where = passAccesses[first_reader.AccessIdx].Where;
            Diagnostic diag
            {
                .Code = DiagnosticCode::LM503,
                .Severity = DiagnosticSeverity::Error,
                .Where = where,
                .Pass = context.Frozen.Passes[first_reader.PassIdx].Key,
                .Resource = ResourceOfSlot(context, slot)
            };

            if (first_writer.PassIdx != -1)
            {
                const auto& writerAccesses = context.PassAccesses[first_writer.PassIdx];
                const auto& otherWhere = writerAccesses[first_writer.AccessIdx].Where;
                diag.OtherWhere = otherWhere;
                DiagnosticValue diagValue
                {
                    .FieldName = "OtherPass",
                    .Kind = DiagnosticValueKind::Text,
                    .Text = context.Frozen.Passes[first_writer.PassIdx].Key.Name
                };
                diag.Actual = diagValue;
            }

            context.Sink.Report(diag);
        }

        void CheckReadsBeforeWrites(OrderContext& context,
                                    const CollatedAccesses& collated,
                                    const std::vector<int32_t>& positions,
                                    const std::vector<uint8_t>& hazard_slots)
        {
            using AccessRef = CollatedAccesses::AccessRef;
            for (int32_t i = 0; i < context.SlotCount; ++i)
            {
                const auto& readers = collated.Readers[i];
                const bool hasSource = HasSource(context, ResourceSlot{i});
                const bool hasHazard = hazard_slots[i] != 0;
                // if nothing reads this, if it has data sources outside the graph, or if we already
                // marked that it has a hazard - skip it
                if (readers.empty() || hasSource || hasHazard)
                {
                    continue;
                }

                const auto positionOf = [&positions](const AccessRef& ref)
                {
                    const NodeIndex node = NodeOfPass(PassIndex{ref.PassIdx});
                    return positions[node.Value];
                };

                const AccessRef firstReader = std::ranges::min(readers, {}, positionOf);

                const auto& writers = collated.Writers[i];

                // We have no writers, but we also already know we don't have any input data
                // sources for this slot - that's a problem!
                if (writers.empty())
                {
                    static constexpr AccessRef k_InvalidWriter{ -1, -1 };
                    ReportReadBeforeWrite(context, ResourceSlot{i}, firstReader, k_InvalidWriter);
                    continue;
                }

                const AccessRef firstWriter = std::ranges::min(writers, {}, positionOf);

                if (positionOf(firstReader) <= positionOf(firstWriter))
                {
                    ReportReadBeforeWrite(context, ResourceSlot{i}, firstReader, firstWriter);
                }

            }
        }

        void ReportCycle(const OrderContext& context, const std::vector<NodeIndex>& emitted)
        {
            std::vector<uint8_t> wasEmitted(context.NodeCount, 0);
            for (const NodeIndex node : emitted)
            {
                wasEmitted[node.Value] = 1;
            }

            // find first un-emitted node; we'll start the walk here
            auto iter = std::ranges::find(wasEmitted, 0);
            assert(iter != wasEmitted.end()); // this would be a weird failure, but catch it just in case
            const int32_t firstUnemittedNode =
                static_cast<int32_t>(std::distance(wasEmitted.begin(), iter));

            // when a node was first reached
            std::vector<int32_t> stepOfNode(context.NodeCount, -1);
            stepOfNode[firstUnemittedNode] = 0; // mark the first step for the starting node
            // edge indices actually reached in walk, newest last
            std::vector<int32_t> walkEdges;
            int32_t repeatedNode = -1;

            // now run the walk
            int32_t currentNode = firstUnemittedNode;
            while (repeatedNode == -1)
            {
                bool stepped = false;
                for (auto&& [idx, edge] : context.Edges | std::views::enumerate)
                {
                    if (edge.To.Value != currentNode)
                    {
                        continue;
                    }

                    const int32_t fromIdx = edge.From.Value;
                    // if this isn't a leftover, skip it
                    if (wasEmitted[fromIdx] != 0u)
                    {
                        continue;
                    }

                    walkEdges.push_back(static_cast<int32_t>(idx));

                    if (stepOfNode[fromIdx] != -1)
                    {
                        // found our repeated node, which indicates a cycle in the graph
                        repeatedNode = fromIdx;
                        stepped = true;
                        break;
                    }

                    stepOfNode[fromIdx] = static_cast<int32_t>(walkEdges.size());
                    currentNode = fromIdx;
                    stepped = true;
                    break;
                }
                // best to assert instead of have an infinite loop
                assert(stepped);
            }

            // The walk ran backwards, so it's edges are newest first. Keep only the cycle (from the repeated node's
            // first visit on), not the path that led into it, and turn it to run forwards.
            std::vector<int32_t> cycleEdges(walkEdges.begin() + stepOfNode[repeatedNode], walkEdges.end());
            std::ranges::reverse(cycleEdges);

            ReportCycleEdges(context, cycleEdges);
        }

        std::string_view EdgeReasonName(EdgeReason reason) noexcept
        {
            switch (reason)
            {
            case EdgeReason::Invalid:       return "Invalid";
            case EdgeReason::After:         return "After";
            case EdgeReason::Before:        return "Before";
            case EdgeReason::AnchorOrder:   return "AnchorOrder";
            case EdgeReason::FrameBounds:   return "FrameBounds";
            case EdgeReason::SequenceChain: return "SequenceChain";
            case EdgeReason::ExposedAnchor: return "ExposedAnchor";
            case EdgeReason::DataFlow:      return "DataFlow";
            }

            return "Invalid";
        }

        // The pass at either end of an edge, or an empty key when both ends are anchors.
        KeyRef PassOfEdge(const OrderContext& context, const OrderEdge& edge)
        {
            if (edge.From.Value < context.PassCount)
            {
                return context.Frozen.Passes[edge.From.Value].Key;
            }

            if (edge.To.Value < context.PassCount)
            {
                return context.Frozen.Passes[edge.To.Value].Key;
            }

            return KeyRef{};
        }

        // One LM501 error at the first edge, then one note per edge in forward order, as a compiler prints "required
        // from here". A Diagnostic holds one location, so a cycle of any length needs the notes.
        void ReportCycleEdges(const OrderContext& context, const std::vector<int32_t>& cycle_edges)
        {
            const OrderEdge& first = context.Edges[cycle_edges.front()];

            Diagnostic error
            {
                .Code = DiagnosticCode::LM501,
                .Severity = DiagnosticSeverity::Error,
                .Where = first.Where,
            };

            // Name a pass on the cycle if it has one. An anchor-only cycle (OrderAnchors both ways) has none.
            for (const int32_t edgeIndex : cycle_edges)
            {
                error.Pass = PassOfEdge(context, context.Edges[edgeIndex]);

                if (!error.Pass.Name.empty())
                {
                    break;
                }
            }

            context.Sink.Report(error);

            for (const int32_t edgeIndex : cycle_edges)
            {
                const OrderEdge& edge = context.Edges[edgeIndex];

                Diagnostic note
                {
                    .Code = DiagnosticCode::LM501,
                    .Severity = DiagnosticSeverity::Note,
                    .Where = edge.Where,
                    .Pass = PassOfEdge(context, edge),
                };

                note.Actual = DiagnosticValue
                {
                    .FieldName = "Reason",
                    .Kind = DiagnosticValueKind::Text,
                    .Text = EdgeReasonName(edge.Reason),
                };

                context.Sink.Report(note);
            }
        }

        
        std::vector<int32_t> PositionsOf(const OrderContext& context, const std::vector<NodeIndex>& ordered_nodes)
        {
            // inverse of ordered_nodes: `positions[node]` gives the index of `node` in `ordered_nodes`
            std::vector<int32_t> positions(ordered_nodes.size(), -1);
            for (int32_t i = 0; i < static_cast<int32_t>(ordered_nodes.size()); ++i)
            {
                positions[ordered_nodes[i].Value] = i;
            }
            return positions;
        }

        bool HasSource(const OrderContext& context, ResourceSlot slot)
        {
            const int32_t resourceCount = static_cast<int32_t>(context.Frozen.Resources.size());
            if (slot.Value >= resourceCount)
            {
                // Slot is a frame target, meaning it does not have an external source
                return false;
            }

            const FrozenResource& resource = context.Frozen.Resources[slot.Value];
            if (resource.Origin == ResourceOrigin::Imported)
            {
                // Imported resources have an external source
                return true;
            }

            auto bufferHasSrc = [&](const int32_t decl_index) -> bool
            {
                const BufferDeclaration& bufferDecl = context.Frozen.Buffers[decl_index];
                return bufferDecl.Cpu == CpuAccess::Upload;
            };

            auto imageHasSrc = [&](const int32_t decl_index) -> bool
            {
                const FrozenImage& image = context.Frozen.Images[decl_index];
                return image.Declaration.Cpu == CpuAccess::Upload;
            };

            const int32_t declIndex = resource.DeclarationIndex;
            switch (resource.Kind)
            {
            case ResourceKind::Buffer:
                return bufferHasSrc(declIndex);
            case ResourceKind::Image:
                return imageHasSrc(declIndex);
            case ResourceKind::Sampler:
                // samplers have a source by virtue of not having real "contents" that create hazards
                return true;
            case ResourceKind::Invalid:
                std::unreachable(); // resources have all had their kind set by now
            }

            // Otherwise, the resource does not have an external source
            // (initial data fields don't yet exist)
            return false;
        }

        bool HasSink(const OrderContext& context, ResourceSlot slot)
        {
            const int32_t resourceCount = static_cast<int32_t>(context.Frozen.Resources.size());

            if (slot.Value >= resourceCount)
            {
                // A frame target is a sink only when the graph declares it as an output: the present reads it
                const int32_t target = slot.Value - resourceCount;
                const auto isThisTarget = [target](const FrozenOutput& output) { return output.Target.Value == target; };
                return std::ranges::any_of(context.Frozen.Outputs, isThisTarget);
            }

            const FrozenResource& resource = context.Frozen.Resources[slot.Value];

            // Before the Kind switch: an import's DeclarationIndex points into Imports, not Images
            if (resource.Origin == ResourceOrigin::Imported)
            {
                return true;
            }

            const int32_t declIndex = resource.DeclarationIndex;

            switch (resource.Kind)
            {
            case ResourceKind::Buffer:
            {
                // History: next frame's BindPrevious reads it. Readback: the CPU reads it after the frame
                const BufferDeclaration& buffer = context.Frozen.Buffers[declIndex];
                return buffer.HistoryDepth > 0 || buffer.Cpu == CpuAccess::Readback;
            }
            case ResourceKind::Image:
            {
                const ImageDeclaration& image = context.Frozen.Images[declIndex].Declaration;
                return image.HistoryDepth > 0 || image.Cpu == CpuAccess::Readback;
            }
            case ResourceKind::Sampler:
                // No pass writes a sampler, so LM504 never asks
                return false;
            case ResourceKind::Invalid:
                std::unreachable();
            }

            return false;
        }

        void ReportDeadWrite(const OrderContext& context,
                             ResourceSlot slot,
                             const CollatedAccesses::AccessRef& writer,
                             const CollatedAccesses::AccessRef& next_writer)
        {
            Diagnostic diagnostic
            {
                .Code = DiagnosticCode::LM504,
                .Severity = DiagnosticSeverity::Warning,
                .Where = context.PassAccesses[writer.PassIdx][writer.AccessIdx].Where,
                .Pass = context.Frozen.Passes[writer.PassIdx].Key,
                .Resource = ResourceOfSlot(context, slot)
            };

            // The last writer has no next writer: its data reaches the end of the frame, and no sink reads it there
            if (next_writer.PassIdx != -1)
            {
                diagnostic.OtherWhere = context.PassAccesses[next_writer.PassIdx][next_writer.AccessIdx].Where;
                diagnostic.Actual = DiagnosticValue
                {
                    .FieldName = "OverwrittenBy",
                    .Kind = DiagnosticValueKind::Text,
                    .Text = context.Frozen.Passes[next_writer.PassIdx].Key.Name
                };
            }

            context.Sink.Report(diagnostic);
        }

        Liveness CheckDeadWrites(OrderContext& context,
                                 const CollatedAccesses& collated,
                                 const std::vector<int32_t>& positions)
        {
            using AccessRef = CollatedAccesses::AccessRef;

            Liveness liveness
            {
                .Feeders = std::vector<std::vector<int32_t>>(context.PassCount),
                .Roots = std::vector<uint8_t>(context.PassCount, 0)
            };

            const auto positionOf = [&positions](const AccessRef& ref)
            {
                const NodeIndex node = NodeOfPass(PassIndex{ ref.PassIdx });
                return positions[node.Value];
            };

            // Reused across slots, so the loop allocates only when a slot has more accesses than any before it
            std::vector<AccessRef> writers;
            std::vector<AccessRef> readers;

            for (int32_t slot = 0; slot < context.SlotCount; ++slot)
            {
                if (collated.Writers[slot].empty())
                {
                    continue;
                }

                // Every pair with a writer is ordered (LM502 is clean when this runs), so positions are the true order
                writers.assign(collated.Writers[slot].begin(), collated.Writers[slot].end());
                readers.assign(collated.Readers[slot].begin(), collated.Readers[slot].end());
                std::ranges::sort(writers, {}, positionOf);
                std::ranges::sort(readers, {}, positionOf);

                const bool hasSink = HasSink(context, ResourceSlot{ slot });
                const size_t writerCount = writers.size();
                size_t nextReader = 0;

                // Writer i's data lives strictly after writer i and up to writer i + 1. Each reader falls in
                // exactly one window, so one cursor walks the readers once for the whole slot.
                for (size_t i = 0; i < writerCount; ++i)
                {
                    const AccessRef& writer = writers[i];
                    const int32_t start = positionOf(writer);
                    const bool isLast = (i + 1 == writerCount);
                    const int32_t end = isLast ? std::numeric_limits<int32_t>::max() : positionOf(writers[i + 1]);

                    // A reader at the writer's own position is a ReadWrite writer: it read the previous write
                    while (nextReader < readers.size() && positionOf(readers[nextReader]) <= start)
                    {
                        ++nextReader;
                    }

                    bool isLive = false;

                    // Inclusive end: a ReadWrite next writer sits in the readers at `end`, and it reads this write
                    while (nextReader < readers.size() && positionOf(readers[nextReader]) <= end)
                    {
                        liveness.Feeders[readers[nextReader].PassIdx].push_back(writer.PassIdx);
                        isLive = true;
                        ++nextReader;
                    }

                    if (isLast && hasSink)
                    {
                        liveness.Roots[writer.PassIdx] = 1;
                        isLive = true;
                    }

                    if (!isLive)
                    {
                        static constexpr AccessRef k_NoWriter{ -1, -1 };
                        const AccessRef& nextWriter = isLast ? k_NoWriter : writers[i + 1];
                        ReportDeadWrite(context, ResourceSlot{ slot }, writer, nextWriter);
                    }
                }
            }

            return liveness;
        }

        void CheckOrphans(OrderContext& context, const Liveness& liveness)
        {
            // Mark: every pass that a root reaches, walking from each reader back to the writers that feed it
            std::vector<uint8_t> marked = liveness.Roots;
            std::vector<int32_t> worklist;

            for (int32_t pass = 0; pass < context.PassCount; ++pass)
            {
                if (marked[pass] != 0)
                {
                    worklist.push_back(pass);
                }
            }

            while (!worklist.empty())
            {
                const int32_t reader = worklist.back();
                worklist.pop_back();

                for (const int32_t writer : liveness.Feeders[reader])
                {
                    if (marked[writer] == 0)
                    {
                        marked[writer] = 1;
                        worklist.push_back(writer);
                    }
                }
            }

            // Sweep: an unmarked pass does work that no output ever sees
            for (int32_t pass = 0; pass < context.PassCount; ++pass)
            {
                if (marked[pass] != 0)
                {
                    continue;
                }

                const FrozenPass& frozenPass = context.Frozen.Passes[pass];
                const Diagnostic diagnostic
                {
                    .Code = DiagnosticCode::LM505,
                    .Severity = DiagnosticSeverity::Warning,
                    .Where = frozenPass.Where,
                    .Pass = frozenPass.Key
                };

                context.Sink.Report(diagnostic);
            }
        }

    }

}
