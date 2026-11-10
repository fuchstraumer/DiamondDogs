#include "order/OrderContext.hpp"
#include "order/OrderedGraph.hpp"
#include "content/JsonWriter.hpp"
#include "freeze/FrozenGraph.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <ranges>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Why the order is what it is (Q3e): the run order, and each pass's edges with their reason and source location.
namespace loom
{
    namespace order
    {
        namespace
        {
            // we cast to size_t because this is immediately used as an index
            constexpr size_t EndOf(const OrderEdge& edge, EdgeEnd end)
            {
                switch (end)
                {
                case EdgeEnd::From:
                    return static_cast<size_t>(edge.From.Value);
                case EdgeEnd::To:
                    return static_cast<size_t>(edge.To.Value);
                default:
                    std::unreachable();
                }
            }

            std::string_view NodeName(const FrozenGraph& frozen, NodeIndex node)
            {
                const int32_t passCount = static_cast<int32_t>(frozen.Passes.size());

                // Node space: the passes first, then the anchors
                if (node.Value < passCount)
                {
                    return frozen.Passes[node.Value].Key.Name;
                }

                return frozen.Anchors[node.Value - passCount].Key.Name;
            }

            std::string_view NodeKind(const FrozenGraph& frozen, NodeIndex node)
            {
                const int32_t passCount = static_cast<int32_t>(frozen.Passes.size());
                return (node.Value < passCount) ? "pass" : "anchor";
            }

            // Empty for an edge with no resource: every reason except DataFlow
            std::string_view SlotName(const FrozenGraph& frozen, ResourceSlot slot)
            {
                if (!slot.IsValid())
                {
                    return {};
                }

                return ResourceOfSlot(frozen, slot).Name;
            }

            // The diagnostics' form, file(line), so an IDE can jump to it. Empty when there is no location.
            std::string FormatWhere(const std::source_location& where)
            {
                if (where.line() == 0)
                {
                    return {};
                }

                return std::format("{}({})", where.file_name(), where.line());
            }

            // far_end is the end that is not this pass: From for an incoming edge, To for an outgoing one
            void AppendEdges(std::vector<ExplainedEdge>& results,
                             const std::vector<OrderEdge>& edges,
                             const EdgesByNode& grouped,
                             int32_t node,
                             EdgeEnd far_end)
            {
                for (int32_t i = grouped.Offsets[node]; i < grouped.Offsets[node + 1]; ++i)
                {
                    const OrderEdge& edge = edges[grouped.EdgeIndices[i]];

                    // every pass has the FrameBounds edges, so they tell us nothing
                    if (edge.Reason == EdgeReason::FrameBounds)
                    {
                        continue;
                    }

                    const NodeIndex other = (far_end == EdgeEnd::From) ? edge.From : edge.To;
                    results.push_back(ExplainedEdge{ .Other = other,
                                                     .Reason = edge.Reason,
                                                     .Resource = edge.Resource,
                                                     .Where = edge.Where });
                }
            }

            // "<indent><relation> <node>: <reason> [<resource>]  <file(line)>"
            void AppendTextEdge(std::string& out,
                                const FrozenGraph& frozen,
                                std::string_view indent,
                                std::string_view relation,
                                const ExplainedEdge& edge)
            {
                std::format_to(std::back_inserter(out), "{}{} {}: {}", indent, relation, NodeName(frozen, edge.Other),
                               EdgeReasonName(edge.Reason));

                const std::string_view resource = SlotName(frozen, edge.Resource);

                if (!resource.empty())
                {
                    std::format_to(std::back_inserter(out), " {}", resource);
                }

                const std::string where = FormatWhere(edge.Where);

                if (!where.empty())
                {
                    std::format_to(std::back_inserter(out), "  {}", where);
                }

                out.push_back('\n');
            }

            // No pass owns an edge between two anchors, so these get their own section
            void AppendTextAnchorOrders(std::string& out, const FrozenGraph& frozen, const OrderedGraph& ordered)
            {
                bool wroteHeader = false;

                for (const OrderEdge& edge : ordered.Edges)
                {
                    if (edge.Reason != EdgeReason::AnchorOrder)
                    {
                        continue;
                    }

                    if (!wroteHeader)
                    {
                        out += "anchor orders:\n";
                        wroteHeader = true;
                    }

                    std::format_to(std::back_inserter(out), "  {} before {}  {}\n", NodeName(frozen, edge.From),
                                   NodeName(frozen, edge.To), FormatWhere(edge.Where));
                }
            }

            void WriteJsonWhere(foundation::content::JsonWriter& writer,
                                std::string_view name,
                                const std::source_location& where)
            {
                writer.Key(name);
                writer.BeginObject();
                writer.KeyString("file", where.file_name());
                writer.KeyUInt("line", where.line());
                writer.EndObject();
            }

            void WriteJsonEdges(foundation::content::JsonWriter& writer,
                                const FrozenGraph& frozen,
                                std::string_view name,
                                const std::vector<ExplainedEdge>& edges)
            {
                writer.Key(name);
                writer.BeginArray();

                for (const ExplainedEdge& edge : edges)
                {
                    writer.BeginObject();
                    writer.KeyString("node", NodeName(frozen, edge.Other));
                    writer.KeyString("kind", NodeKind(frozen, edge.Other));
                    writer.KeyString("reason", EdgeReasonName(edge.Reason));

                    const std::string_view resource = SlotName(frozen, edge.Resource);

                    if (!resource.empty())
                    {
                        writer.KeyString("resource", resource);
                    }

                    WriteJsonWhere(writer, "where", edge.Where);
                    writer.EndObject();
                }

                writer.EndArray();
            }

        }

        EdgesByNode GroupEdgesByNode(const std::vector<OrderEdge>& edges, int32_t node_count, EdgeEnd which_end)
        {
            EdgesByNode result
            {
                .Offsets = std::vector<int32_t>(node_count + 1, 0),
                .EdgeIndices = std::vector<int32_t>(edges.size(), -1)
            };

            // count into [node + 1], so that after the prefix sum, Offsets[node] is where node's run starts
            for (const OrderEdge& edge : edges)
            {
                const size_t node = EndOf(edge, which_end);
                result.Offsets[node + 1]++;
            }

            // fold left (prefix sum) to get the final offsets for each node
            for (int32_t i = 1; i <= node_count; ++i)
            {
                result.Offsets[i] += result.Offsets[i - 1];
            }

            // one write position per node. Walking the edges in order keeps each node's edges in insertion order
            std::vector<int32_t> cursor(result.Offsets.begin(), result.Offsets.begin() + node_count);

            for (const auto&& [idx, edge] : std::views::enumerate(edges))
            {
                const size_t node = EndOf(edge, which_end);
                const size_t insertPos = static_cast<size_t>(cursor[node]++);
                result.EdgeIndices[insertPos] = static_cast<int32_t>(idx);
            }

            return result;
        }

        std::vector<ExplainedPass> ExplainPasses(const FrozenGraph& frozen, const OrderedGraph& ordered)
        {
            const int32_t passCount = static_cast<int32_t>(frozen.Passes.size());
            const int32_t nodeCount = passCount + static_cast<int32_t>(frozen.Anchors.size());
            const EdgesByNode incoming = GroupEdgesByNode(ordered.Edges, nodeCount, EdgeEnd::To);
            const EdgesByNode outgoing = GroupEdgesByNode(ordered.Edges, nodeCount, EdgeEnd::From);

            std::vector<ExplainedPass> results;
            results.reserve(ordered.Passes.size());

            for (int32_t position = 0; std::cmp_less(position, ordered.Passes.size()); ++position)
            {
                const PassIndex pass = ordered.Passes[position];
                const int32_t node = NodeOfPass(pass).Value;
                ExplainedPass explained
                {
                    .Position = position,
                    .Pass = pass
                };

                // grouped by To, these edges point at this pass: the far end is From. The reverse for outgoing.
                AppendEdges(explained.Incoming, ordered.Edges, incoming, node, EdgeEnd::From);
                AppendEdges(explained.Outgoing, ordered.Edges, outgoing, node, EdgeEnd::To);
                results.push_back(std::move(explained));
            }

            return results;
        }

    }

    std::string Explain(const FrozenGraph& frozen, const OrderedGraph& ordered)
    {
        using namespace order;
        const std::vector<ExplainedPass> passes = ExplainPasses(frozen, ordered);

        std::string out;
        std::format_to(std::back_inserter(out), "order: {} passes\n", passes.size());

        for (const ExplainedPass& explained : passes)
        {
            const FrozenPass& pass = frozen.Passes[explained.Pass.Value];
            std::format_to(std::back_inserter(out), "  {} {}  {}\n", explained.Position, pass.Key.Name,
                           FormatWhere(pass.Where));

            for (const ExplainedEdge& edge : explained.Incoming)
            {
                AppendTextEdge(out, frozen, "      ", "after", edge);
            }
        }

        AppendTextAnchorOrders(out, frozen, ordered);
        return out;
    }

    std::string ExplainPass(const FrozenGraph& frozen, const OrderedGraph& ordered, PassIndex pass)
    {
        using namespace order;
        const std::vector<ExplainedPass> passes = ExplainPasses(frozen, ordered);

        // the entry for a pass sits at its run position, not at its pass index
        const auto isThisPass = [pass](const ExplainedPass& explained) { return explained.Pass == pass; };
        const auto found = std::ranges::find_if(passes, isThisPass);

        if (found == passes.end())
        {
            return {};
        }

        const FrozenPass& frozenPass = frozen.Passes[pass.Value];

        std::string out;
        std::format_to(std::back_inserter(out), "{}: position {} of {}  {}\n", frozenPass.Key.Name, found->Position,
                       passes.size(), FormatWhere(frozenPass.Where));

        for (const ExplainedEdge& edge : found->Incoming)
        {
            AppendTextEdge(out, frozen, "  ", "after", edge);
        }

        for (const ExplainedEdge& edge : found->Outgoing)
        {
            AppendTextEdge(out, frozen, "  ", "before", edge);
        }

        return out;
    }

    foundation::content::JsonResult<std::string> ExplainJson(const FrozenGraph& frozen,
                                                             const OrderedGraph& ordered,
                                                             foundation::content::JsonStyle style)
    {
        using namespace order;
        const std::vector<ExplainedPass> passes = ExplainPasses(frozen, ordered);

        foundation::content::JsonWriter writer{ style };
        writer.BeginObject();
        writer.Key("passes");
        writer.BeginArray();

        for (const ExplainedPass& explained : passes)
        {
            const FrozenPass& pass = frozen.Passes[explained.Pass.Value];
            writer.BeginObject();
            writer.KeyInt("position", explained.Position);
            writer.KeyString("pass", pass.Key.Name);
            WriteJsonWhere(writer, "where", pass.Where);
            WriteJsonEdges(writer, frozen, "incoming", explained.Incoming);
            WriteJsonEdges(writer, frozen, "outgoing", explained.Outgoing);
            writer.EndObject();
        }

        writer.EndArray();
        writer.Key("anchorOrders");
        writer.BeginArray();

        for (const OrderEdge& edge : ordered.Edges)
        {
            if (edge.Reason != EdgeReason::AnchorOrder)
            {
                continue;
            }

            writer.BeginObject();
            writer.KeyString("earlier", NodeName(frozen, edge.From));
            writer.KeyString("later", NodeName(frozen, edge.To));
            WriteJsonWhere(writer, "where", edge.Where);
            writer.EndObject();
        }

        writer.EndArray();
        writer.EndObject();
        return writer.Finish();
    }

}
