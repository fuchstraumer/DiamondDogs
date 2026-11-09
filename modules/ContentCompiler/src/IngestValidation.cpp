#include "IngestValidation.hpp"
#include "ContentDiagnostic.hpp"
#include "ContentTypes.hpp"
#include "Ingest.hpp"
#include "KindRegistry.hpp"
#include "RecipeTable.hpp"
#include "content/Key.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <queue>
#include <unordered_map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    // an edge whose two ends are nodes of the graph
    struct EdgeEnds
    {
        int32_t From{ 0 };
        int32_t To{ 0 };
    };

    cc::Diagnostic MakeError(cc::DiagnosticCode code, std::vector<uint32_t> nodes, std::string message);
    cc::Diagnostic MakeNodeError(cc::DiagnosticCode code, uint32_t node, std::string message);
    void CheckNode(int32_t index, const cc::AuthoredNode& node, const cc::KindRegistry& registry,
                   cc::detail::ValidatedGraph& validated, std::vector<cc::Diagnostic>& diagnostics);
    void CheckParams(int32_t index, const cc::AuthoredNode& node, const cc::KindOps& kind,
                     std::vector<cc::Diagnostic>& diagnostics);
    void CheckEdge(int32_t edge_index, const cc::AuthoredEdge& edge, const cc::KindRegistry& registry,
                   cc::detail::ValidatedGraph& validated, std::vector<EdgeEnds>& structural_edges,
                   std::vector<cc::Diagnostic>& diagnostics);
    void CheckRequiredSlots(const cc::KindRegistry& registry, const cc::detail::ValidatedGraph& validated,
                            std::vector<cc::Diagnostic>& diagnostics);
    std::optional<size_t> FindSlot(std::span<const cc::Slot> slots, std::string_view name);
    std::optional<size_t> FindOutput(std::span<const cc::Output> outputs, std::string_view name);
    std::string ListOutputNames(std::span<const cc::Output> outputs);
    std::vector<int32_t> SortTopologically(int32_t node_count, std::span<const EdgeEnds> edges);
    std::vector<int32_t> FindCycle(int32_t node_count, std::span<const EdgeEnds> edges,
                                   std::span<const int32_t> sorted_order);
    int32_t LowestUnsortedPredecessor(int32_t node, std::span<const EdgeEnds> edges,
                                      const std::vector<bool>& is_sorted);
}

namespace cc::detail
{
    std::vector<BatchCollision> FindBatchCollisions(std::span<const PreparedRecipe> prepared)
    {
        std::unordered_map<foundation::content::RecipeKey, size_t,
                           foundation::content::KeyHasher<foundation::content::RecipeTag>> firstWithKey;
        std::vector<BatchCollision> collisions;

        for (size_t node = 0; node < prepared.size(); ++node)
        {
            const auto [entry, isFirst] = firstWithKey.emplace(prepared[node].Key, node);

            if (isFirst)
            {
                continue;
            }

            const size_t earlier = entry->second;

            if (!std::ranges::equal(prepared[earlier].Bytes, prepared[node].Bytes))
            {
                collisions.push_back(BatchCollision{ node, earlier });
            }
        }

        return collisions;
    }

    std::vector<Diagnostic> ValidateGraph(const AuthoredGraph& graph, const KindRegistry& registry,
                                          ValidatedGraph& validated)
    {
        const int32_t nodeCount = static_cast<int32_t>(graph.Nodes.size());

        std::vector<Diagnostic> diagnostics;
        std::vector<EdgeEnds> structuralEdges;
        validated.NodeKinds.assign(graph.Nodes.size(), KindId{});
        validated.SlotProducers.assign(graph.Nodes.size(), {});
        validated.SlotOutputs.assign(graph.Nodes.size(), {});

        for (int32_t index = 0; index < nodeCount; ++index)
        {
            CheckNode(index, graph.Nodes[index], registry, validated, diagnostics);
        }

        for (int32_t edgeIndex = 0; edgeIndex < static_cast<int32_t>(graph.Edges.size()); ++edgeIndex)
        {
            CheckEdge(edgeIndex, graph.Edges[edgeIndex], registry, validated, structuralEdges, diagnostics);
        }

        CheckRequiredSlots(registry, validated, diagnostics);

        validated.TopologicalOrder = SortTopologically(nodeCount, structuralEdges);

        if (static_cast<int32_t>(validated.TopologicalOrder.size()) < nodeCount)
        {
            const std::span<const int32_t> sortedOrder = validated.TopologicalOrder;
            const std::vector<int32_t> cycle = FindCycle(nodeCount, structuralEdges, sortedOrder);

            std::vector<uint32_t> cycleNodes;
            cycleNodes.reserve(cycle.size());

            for (const int32_t node : cycle)
            {
                cycleNodes.push_back(static_cast<uint32_t>(node));
            }

            const std::string message = std::format("the graph has a cycle through {} nodes", cycle.size());
            diagnostics.push_back(MakeError(DiagnosticCode::CycleDetected, std::move(cycleNodes), message));
        }

        return diagnostics;
    }
}

namespace
{
    cc::Diagnostic MakeError(cc::DiagnosticCode code, std::vector<uint32_t> nodes, std::string message)
    {
        return cc::Diagnostic
        {
            .Level = cc::Severity::Error,
            .Code = code,
            .Nodes = std::move(nodes),
            .Message = std::move(message),
        };
    }

    cc::Diagnostic MakeNodeError(cc::DiagnosticCode code, uint32_t node, std::string message)
    {
        return MakeError(code, std::vector<uint32_t>{ node }, std::move(message));
    }

    void CheckNode(int32_t index, const cc::AuthoredNode& node, const cc::KindRegistry& registry,
                   cc::detail::ValidatedGraph& validated, std::vector<cc::Diagnostic>& diagnostics)
    {
        const std::optional<cc::KindId> kindId = registry.Find(node.KindName);

        if (!kindId.has_value())
        {
            const std::string message = std::format(
                "node {} has the unknown kind \"{}\"", index, node.KindName);
            diagnostics.push_back(MakeNodeError(cc::DiagnosticCode::UnknownKind, index, message));

            return;
        }

        const cc::KindOps& kind = registry.Ops(*kindId);
        validated.NodeKinds[index] = *kindId;
        validated.SlotProducers[index].assign(kind.Slots.size(), cc::detail::k_NoNode);
        validated.SlotOutputs[index].assign(kind.Slots.size(), cc::detail::k_NoOutput);

        CheckParams(index, node, kind, diagnostics);
    }

    void CheckParams(int32_t index, const cc::AuthoredNode& node, const cc::KindOps& kind,
                     std::vector<cc::Diagnostic>& diagnostics)
    {
        if (node.Params.TypeIdentity() != kind.ParamsType)
        {
            const std::string_view heldName = node.Params.IsEmpty() ? "nothing" : node.Params.TypeName();
            const std::string message = std::format(
                "node {} has kind \"{}\", which takes {}, but its params hold {}",
                index, kind.Name, kind.ParamsTypeName, heldName);
            diagnostics.push_back(MakeNodeError(cc::DiagnosticCode::ParamsTypeMismatch, index, message));

            return;
        }

        const std::optional<std::string> nanPath = node.Params.FindNaN();

        if (!nanPath.has_value())
        {
            return;
        }

        const std::string message = std::format("node {} has a NaN in \"{}\"", index, *nanPath);
        cc::Diagnostic diagnostic = MakeNodeError(cc::DiagnosticCode::NaNParameter, index, message);
        diagnostic.FieldPath = *nanPath;
        diagnostics.push_back(std::move(diagnostic));
    }

    void CheckEdge(int32_t edge_index,
                   const cc::AuthoredEdge& edge,
                   const cc::KindRegistry& registry,
                   cc::detail::ValidatedGraph& validated,
                   std::vector<EdgeEnds>& structural_edges,
                   std::vector<cc::Diagnostic>& diagnostics)
    {
        const size_t nodeCount = validated.NodeKinds.size();
        const bool fromExists = edge.FromNode < nodeCount;
        const bool toExists = edge.ToNode < nodeCount;

        if (!fromExists || !toExists)
        {
            // the node list holds the end that exists, so that the diagnostic still points at a real node
            std::vector<uint32_t> existingNodes;

            if (fromExists)
            {
                existingNodes.push_back(edge.FromNode);
            }
            else if (toExists)
            {
                existingNodes.push_back(edge.ToNode);
            }

            const std::string message = std::format(
                "edge {} runs from node {} to node {}, and the graph has {} nodes",
                edge_index, edge.FromNode, edge.ToNode, nodeCount);
            diagnostics.push_back(
                MakeError(cc::DiagnosticCode::DanglingEdge, std::move(existingNodes), message));

            return;
        }

        const int32_t from = static_cast<int32_t>(edge.FromNode);
        const int32_t to = static_cast<int32_t>(edge.ToNode);
        structural_edges.push_back(EdgeEnds{ from, to });

        const cc::KindId consumerKindId = validated.NodeKinds[to];

        // UnknownKind is reported for this node already
        if (consumerKindId.Value == 0)
        {
            return;
        }

        const cc::KindOps& consumerKind = registry.Ops(consumerKindId);
        const std::optional<size_t> slotIndex = FindSlot(consumerKind.Slots, edge.ToSlot);

        if (!slotIndex.has_value())
        {
            const std::string message = std::format(
                R"(edge {} goes to slot "{}", and kind "{}" has no such slot)",
                edge_index, edge.ToSlot, consumerKind.Name);
            diagnostics.push_back(MakeNodeError(cc::DiagnosticCode::UnknownSlot, edge.ToNode, message));

            return;
        }

        int32_t& producer = validated.SlotProducers[to][*slotIndex];

        if (producer != cc::detail::k_NoNode)
        {
            const std::string message = std::format(
                "edge {} goes to slot \"{}\" of node {}, which has an edge there",
                edge_index, edge.ToSlot, edge.ToNode);
            diagnostics.push_back(
                MakeNodeError(cc::DiagnosticCode::SlotAlreadyConnected, edge.ToNode, message));

            return;
        }

        const cc::KindId producerKindId = validated.NodeKinds[from];

        // UnknownKind is reported for the producer already
        if (producerKindId.Value == 0)
        {
            producer = from;

            return;
        }

        const cc::KindOps& producerKind = registry.Ops(producerKindId);
        const std::optional<size_t> outputIndex = FindOutput(producerKind.Outputs, edge.FromOutput);

        // an edge with an unknown output fills no slot, as a dangling edge does
        if (!outputIndex.has_value())
        {
            const std::string outputNames = ListOutputNames(producerKind.Outputs);
            const std::string message = std::format(
                R"(edge {} reads the output "{}" of node {}, and kind "{}" has no such output (it has: {}))",
                edge_index, edge.FromOutput, edge.FromNode, producerKind.Name, outputNames);
            diagnostics.push_back(MakeNodeError(cc::DiagnosticCode::UnknownOutput, edge.FromNode, message));

            return;
        }

        producer = from;
        validated.SlotOutputs[to][*slotIndex] = static_cast<int32_t>(*outputIndex);

        const cc::Slot& slot = consumerKind.Slots[*slotIndex];
        const cc::Output& output = producerKind.Outputs[*outputIndex];

        if (output.Produces != slot.Accepts)
        {
            const std::string message = std::format(
                R"(edge {}: output "{}" of kind "{}" of node {} makes an artifact that slot "{}" refuses)",
                edge_index, output.Name, producerKind.Name, edge.FromNode, slot.Name);
            diagnostics.push_back(MakeNodeError(cc::DiagnosticCode::SlotKindMismatch, edge.ToNode, message));
        }
    }

    void CheckRequiredSlots(const cc::KindRegistry& registry, const cc::detail::ValidatedGraph& validated,
                            std::vector<cc::Diagnostic>& diagnostics)
    {
        for (size_t node = 0; node < validated.NodeKinds.size(); ++node)
        {
            const cc::KindId kindId = validated.NodeKinds[node];

            if (kindId.Value == 0)
            {
                continue;
            }

            const std::span<const cc::Slot> slots = registry.Ops(kindId).Slots;

            for (size_t slotIndex = 0; slotIndex < slots.size(); ++slotIndex)
            {
                const bool isMissing = slots[slotIndex].Use == cc::SlotUse::Required &&
                                       validated.SlotProducers[node][slotIndex] == cc::detail::k_NoNode;

                if (!isMissing)
                {
                    continue;
                }

                const std::string message = std::format(
                    "node {} has no edge into the required slot \"{}\"", node, slots[slotIndex].Name);
                const uint32_t nodeIndex = static_cast<uint32_t>(node);
                diagnostics.push_back(
                    MakeNodeError(cc::DiagnosticCode::MissingRequiredSlot, nodeIndex, message));
            }
        }
    }

    std::optional<size_t> FindSlot(std::span<const cc::Slot> slots, std::string_view name)
    {
        const auto found = std::ranges::find(slots, name, &cc::Slot::Name);

        if (found == slots.end())
        {
            return std::nullopt;
        }

        return static_cast<size_t>(found - slots.begin());
    }

    std::optional<size_t> FindOutput(std::span<const cc::Output> outputs, std::string_view name)
    {
        const auto found = std::ranges::find(outputs, name, &cc::Output::Name);

        if (found == outputs.end())
        {
            return std::nullopt;
        }

        return static_cast<size_t>(found - outputs.begin());
    }

    std::string ListOutputNames(std::span<const cc::Output> outputs)
    {
        std::string names;

        for (const cc::Output& output : outputs)
        {
            if (!names.empty())
            {
                names += ", ";
            }

            names += '"';
            names += output.Name;
            names += '"';
        }

        return names;
    }

    std::vector<int32_t> SortTopologically(int32_t node_count, std::span<const EdgeEnds> edges)
    {
        std::vector<int32_t> incomingCounts(node_count, 0);
        std::vector<std::vector<int32_t>> successors(node_count);

        for (const EdgeEnds& edge : edges)
        {
            ++incomingCounts[edge.To];
            successors[edge.From].push_back(edge.To);
        }

        // a min-heap, so that of the nodes that are ready the lowest authored index goes first
        std::priority_queue<int32_t, std::vector<int32_t>, std::greater<>> ready;

        for (int32_t node = 0; node < node_count; ++node)
        {
            if (incomingCounts[node] == 0)
            {
                ready.push(node);
            }
        }

        std::vector<int32_t> order;
        order.reserve(node_count);

        while (!ready.empty())
        {
            const int32_t node = ready.top();
            ready.pop();
            order.push_back(node);

            for (const int32_t successor : successors[node])
            {
                --incomingCounts[successor];

                if (incomingCounts[successor] == 0)
                {
                    ready.push(successor);
                }
            }
        }

        return order;
    }

    std::vector<int32_t> FindCycle(int32_t node_count, std::span<const EdgeEnds> edges,
                                   std::span<const int32_t> sorted_order)
    {
        std::vector<bool> isSorted(node_count, false);

        for (const int32_t node : sorted_order)
        {
            isSorted[node] = true;
        }

        const auto firstUnsorted = std::ranges::find(isSorted, false);
        int32_t node = static_cast<int32_t>(firstUnsorted - isSorted.begin());

        // Every node that Kahn's algorithm left has an unsorted predecessor. Walking back along them must
        // repeat a node, and the walk between the two visits is a cycle. Nodes only downstream of a cycle are
        // not on it.
        std::vector<int32_t> walk;
        std::vector<int32_t> positionInWalk(node_count, -1);

        while (positionInWalk[node] < 0)
        {
            positionInWalk[node] = static_cast<int32_t>(walk.size());
            walk.push_back(node);
            node = LowestUnsortedPredecessor(node, edges, isSorted);
        }

        std::vector<int32_t> cycle(walk.begin() + positionInWalk[node], walk.end());

        // the walk runs against the edges, so reverse it, then start at the lowest index
        std::ranges::reverse(cycle);
        std::ranges::rotate(cycle, std::ranges::min_element(cycle));

        return cycle;
    }

    int32_t LowestUnsortedPredecessor(int32_t node, std::span<const EdgeEnds> edges,
                                      const std::vector<bool>& is_sorted)
    {
        int32_t lowest = cc::detail::k_NoNode;

        for (const EdgeEnds& edge : edges)
        {
            const bool isCandidate = edge.To == node && !is_sorted[edge.From];

            if (isCandidate && (lowest == cc::detail::k_NoNode || edge.From < lowest))
            {
                lowest = edge.From;
            }
        }

        return lowest;
    }
}
