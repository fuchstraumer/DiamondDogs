#include "LoomKeys.hpp"
#include "order/OrderContext.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "freeze/FrozenGraph.hpp"
#include "order/OrderedGraph.hpp"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <optional>
#include <queue>
#include <ranges>
#include <span>
#include <string_view>
#include <vector>

// What order, and what reaches what: the sort (Kahn), the cycle report (LM501), the closure, and positions.
namespace loom
{
    namespace order
    {

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

    }

}
