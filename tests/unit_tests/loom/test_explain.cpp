#include "content/JsonWriter.hpp"
#include "freeze/FrozenGraph.hpp"
#include "GraphBuilder.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomKeys.hpp"
#include "PassBuilders.hpp"
#include "freeze_test_helpers.hpp"
#include "order/OrderContext.hpp"
#include "order/OrderedGraph.hpp"
#include <cstdint>
#include <format>
#include <optional>
#include <source_location>
#include <string>
#include <utility>
#include <vector>
#include <gtest/gtest.h>

// Explain (Q3e): the grouping it walks, the per-pass edge lists, and the exact text and JSON it prints.

using namespace freeze_tests;

namespace
{

    constexpr loom::PassKey k_Second{ "Test.Second" };
    constexpr loom::AnchorKey k_AnchorA{ "Test.AnchorA" };
    constexpr loom::AnchorKey k_AnchorB{ "Test.AnchorB" };
    constexpr loom::ResourceAccess k_Read = loom::ResourceAccess::Read;
    constexpr loom::ResourceAccess k_Write = loom::ResourceAccess::Write;

    // Explain reads the FrozenGraph after Order() returns, so a test keeps both.
    struct Compiled
    {
        loom::FrozenGraph Frozen;
        loom::OrderedGraph Ordered;
    };

    std::optional<Compiled> Compile(const loom::GraphBuilder& graph,
                                    std::vector<std::vector<loom::ResourceAccess>> accesses)
    {
        loom::CollectingSink sink;
        std::optional<loom::FrozenGraph> frozen = loom::Freeze(graph.Record(), sink);
        EXPECT_TRUE(frozen.has_value()) << "Freeze failed";

        if (!frozen.has_value())
        {
            return std::nullopt;
        }

        const loom::BindAccessTable binds{ .Accesses = std::move(accesses) };
        std::optional<loom::OrderedGraph> ordered = loom::Order(*frozen, binds, sink);
        EXPECT_TRUE(ordered.has_value()) << "Order failed";

        if (!ordered.has_value())
        {
            return std::nullopt;
        }

        return Compiled{ .Frozen = std::move(*frozen), .Ordered = std::move(*ordered) };
    }

    std::string Where(uint32_t line)
    {
        return std::format("{}({})", std::source_location::current().file_name(), line);
    }

    // The lines that the explained graph's statements sit on, so the expected text can name them.
    struct Lines
    {
        uint32_t OrderAnchors = 0;
        uint32_t FirstPass = 0;
        uint32_t FirstBefore = 0;
        uint32_t SecondPass = 0;
        uint32_t SecondAfter = 0;
        uint32_t SecondRead = 0;
    };

    // A before B (anchor order). First writes Data before A; Second reads Data after B. Edges, besides the frame
    // bounds: A -> B (AnchorOrder), First -> A (Before), B -> Second (After), First -> Second (DataFlow, Data).
    std::optional<Compiled> CompileExplainedGraph(Lines& lines)
    {
        loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
        loom::BufferDeclaration data = MakeBuffer();
        data.HistoryDepth = 1;
        data.NoHistoryAction = loom::OnNoHistory::Clear;
        graph.Declare(data);
        graph.DeclareAnchor(k_AnchorA);
        graph.DeclareAnchor(k_AnchorB);
        lines.OrderAnchors = std::source_location::current().line() + 1;
        graph.OrderAnchors(k_AnchorA, k_AnchorB);

        auto writeBeforeA = [&lines](loom::DispatchBuilder& pass)
        {
            pass.Shader("Test", "MainCS");
            pass.DispatchThreads(8, 8, 1);
            pass.Bind("Data", k_Data);
            lines.FirstBefore = std::source_location::current().line() + 1;
            pass.Before(k_AnchorA);
        };

        auto readAfterB = [&lines](loom::DispatchBuilder& pass)
        {
            pass.Shader("Test", "MainCS");
            pass.DispatchThreads(8, 8, 1);
            lines.SecondRead = std::source_location::current().line() + 1;
            pass.Bind("Data", k_Data);
            lines.SecondAfter = std::source_location::current().line() + 1;
            pass.After(k_AnchorB);
        };

        lines.FirstPass = std::source_location::current().line() + 1;
        graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, writeBeforeA);
        lines.SecondPass = std::source_location::current().line() + 1;
        graph.AddDispatch(k_Second, k_EveryFrame, k_Graphics, readAfterB);

        return Compile(graph, { { k_Write }, { k_Read } });
    }

}

// The worked example: 3 nodes, edges e0: 0 -> 2, e1: 1 -> 2, e2: 0 -> 1, e3: 2 -> 1. Grouped by To, then by From.
// Within a node, the edges keep their insertion order (the counting sort is stable).
TEST(Explain, GroupEdgesByNodeIsACountingSort)
{
    const auto edge = [](int32_t from, int32_t to)
    {
        return loom::OrderEdge{ .From = loom::NodeIndex{ from }, .To = loom::NodeIndex{ to } };
    };

    const std::vector<loom::OrderEdge> edges{ edge(0, 2), edge(1, 2), edge(0, 1), edge(2, 1) };

    const loom::order::EdgesByNode byTo = loom::order::GroupEdgesByNode(edges, 3, loom::order::EdgeEnd::To);
    EXPECT_EQ(byTo.Offsets, (std::vector<int32_t>{ 0, 0, 2, 4 }));
    EXPECT_EQ(byTo.EdgeIndices, (std::vector<int32_t>{ 2, 3, 0, 1 }));

    const loom::order::EdgesByNode byFrom = loom::order::GroupEdgesByNode(edges, 3, loom::order::EdgeEnd::From);
    EXPECT_EQ(byFrom.Offsets, (std::vector<int32_t>{ 0, 2, 3, 4 }));
    EXPECT_EQ(byFrom.EdgeIndices, (std::vector<int32_t>{ 0, 2, 1, 3 }));
}

// Each pass, in run order, with the edges into it and out of it. The far end, reason, resource and location of
// each edge. No FrameBounds edge in either list.
TEST(Explain, PassesListBothDirectionsWithoutFrameBounds)
{
    Lines lines;
    const std::optional<Compiled> compiled = CompileExplainedGraph(lines);
    ASSERT_TRUE(compiled.has_value());

    const std::vector<loom::order::ExplainedPass> passes = loom::order::ExplainPasses(compiled->Frozen, compiled->Ordered);
    ASSERT_EQ(passes.size(), 2u);

    // Node space: passes 0 and 1, then the anchors. BeginFrame and EndFrame come first, so A is node 4, B is node 5.
    const loom::order::ExplainedPass& first = passes[0];
    EXPECT_EQ(first.Position, 0);
    EXPECT_EQ(first.Pass.Value, 0);
    EXPECT_TRUE(first.Incoming.empty()) << "only the frame bound, which is left out";
    ASSERT_EQ(first.Outgoing.size(), 2u);
    EXPECT_EQ(first.Outgoing[0].Other.Value, 4);
    EXPECT_EQ(first.Outgoing[0].Reason, loom::EdgeReason::Before);
    EXPECT_EQ(first.Outgoing[0].Where.line(), lines.FirstBefore);
    EXPECT_EQ(first.Outgoing[1].Other.Value, 1);
    EXPECT_EQ(first.Outgoing[1].Reason, loom::EdgeReason::DataFlow);
    EXPECT_EQ(first.Outgoing[1].Resource.Value, 0);

    const loom::order::ExplainedPass& second = passes[1];
    EXPECT_EQ(second.Position, 1);
    ASSERT_EQ(second.Incoming.size(), 2u);
    EXPECT_EQ(second.Incoming[0].Other.Value, 5);
    EXPECT_EQ(second.Incoming[0].Reason, loom::EdgeReason::After);
    EXPECT_EQ(second.Incoming[1].Other.Value, 0);
    EXPECT_EQ(second.Incoming[1].Reason, loom::EdgeReason::DataFlow);
    EXPECT_EQ(second.Incoming[1].Where.line(), lines.SecondRead) << "a data edge is at the reader's Bind";
    EXPECT_TRUE(second.Outgoing.empty());
}

// The filter is on the reason, not on the node: an author's After(BeginFrame) also comes from the BeginFrame node,
// and it is a real statement, so Explain keeps it.
TEST(Explain, AuthorEdgeFromBeginFrameIsKept)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };

    auto afterBeginFrame = [](loom::DispatchBuilder& pass)
    {
        pass.Shader("Test", "MainCS");
        pass.DispatchThreads(8, 8, 1);
        pass.After(loom::BeginFrameAnchor);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, afterBeginFrame);

    const std::optional<Compiled> compiled = Compile(graph, { {} });
    ASSERT_TRUE(compiled.has_value());

    const std::vector<loom::order::ExplainedPass> passes = loom::order::ExplainPasses(compiled->Frozen, compiled->Ordered);
    ASSERT_EQ(passes.size(), 1u);
    ASSERT_EQ(passes[0].Incoming.size(), 1u);
    EXPECT_EQ(passes[0].Incoming[0].Reason, loom::EdgeReason::After);
    EXPECT_EQ(passes[0].Incoming[0].Other.Value, 1) << "BeginFrame is the first anchor, node PassCount + 0";
}

TEST(Explain, TextIsExact)
{
    Lines lines;
    const std::optional<Compiled> compiled = CompileExplainedGraph(lines);
    ASSERT_TRUE(compiled.has_value());

    const std::string expected =
        "order: 2 passes\n" +
        std::format("  0 Test.First  {}\n", Where(lines.FirstPass)) +
        std::format("  1 Test.Second  {}\n", Where(lines.SecondPass)) +
        std::format("      after Test.AnchorB: After  {}\n", Where(lines.SecondAfter)) +
        std::format("      after Test.First: DataFlow Test.Data  {}\n", Where(lines.SecondRead)) +
        "anchor orders:\n" +
        std::format("  Test.AnchorA before Test.AnchorB  {}\n", Where(lines.OrderAnchors));

    EXPECT_EQ(loom::Explain(compiled->Frozen, compiled->Ordered), expected);
}

TEST(Explain, PassTextShowsUpstreamAndDownstream)
{
    Lines lines;
    const std::optional<Compiled> compiled = CompileExplainedGraph(lines);
    ASSERT_TRUE(compiled.has_value());

    const std::string expected =
        std::format("Test.First: position 0 of 2  {}\n", Where(lines.FirstPass)) +
        std::format("  before Test.AnchorA: Before  {}\n", Where(lines.FirstBefore)) +
        std::format("  before Test.Second: DataFlow Test.Data  {}\n", Where(lines.SecondRead));

    EXPECT_EQ(loom::ExplainPass(compiled->Frozen, compiled->Ordered, loom::PassIndex{ 0 }), expected);
    EXPECT_TRUE(loom::ExplainPass(compiled->Frozen, compiled->Ordered, loom::PassIndex{ 7 }).empty()) << "no such pass";
}

// The JSON holds the same facts. The paths are escaped in JSON, so the test checks the fields that hold no path.
TEST(Explain, JsonHoldsTheSameFacts)
{
    Lines lines;
    const std::optional<Compiled> compiled = CompileExplainedGraph(lines);
    ASSERT_TRUE(compiled.has_value());

    const foundation::content::JsonResult<std::string> json =
        loom::ExplainJson(compiled->Frozen, compiled->Ordered, foundation::content::JsonStyle::Compact);
    ASSERT_TRUE(json.has_value());

    const std::string& text = *json;
    const size_t firstPass = text.find("\"position\":0,\"pass\":\"Test.First\"");
    const size_t secondPass = text.find("\"position\":1,\"pass\":\"Test.Second\"");
    ASSERT_NE(firstPass, std::string::npos);
    ASSERT_NE(secondPass, std::string::npos);
    EXPECT_LT(firstPass, secondPass) << "passes in run order";

    EXPECT_NE(text.find("\"node\":\"Test.AnchorA\",\"kind\":\"anchor\",\"reason\":\"Before\""), std::string::npos);
    EXPECT_NE(text.find("\"node\":\"Test.First\",\"kind\":\"pass\",\"reason\":\"DataFlow\",\"resource\":\"Test.Data\""),
              std::string::npos);
    EXPECT_NE(text.find(std::format("\"line\":{}", lines.SecondRead)), std::string::npos);
    EXPECT_NE(text.find("\"anchorOrders\":[{\"earlier\":\"Test.AnchorA\",\"later\":\"Test.AnchorB\""), std::string::npos);
    EXPECT_EQ(text.find("FrameBounds"), std::string::npos);
}
