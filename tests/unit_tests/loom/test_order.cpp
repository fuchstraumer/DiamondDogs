#include "freeze/FrozenGraph.hpp"
#include "GraphBuilder.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "LoomKeys.hpp"
#include "PassBuilders.hpp"
#include "freeze_test_helpers.hpp"
#include "order/OrderedGraph.hpp"
#include <cstdint>
#include <optional>
#include <source_location>
#include <vector>
#include <gtest/gtest.h>

// The Order stage, fed by Freeze. No pass binds anything yet, so the bind access table is empty for every pass.

using namespace freeze_tests;

namespace
{

    constexpr loom::PassKey k_Second{ "Test.Second" };
    constexpr loom::PassKey k_Third{ "Test.Third" };
    constexpr loom::AnchorKey k_AnchorA{ "Test.AnchorA" };
    constexpr loom::AnchorKey k_AnchorB{ "Test.AnchorB" };

    void ShaderAndThreads(loom::DispatchBuilder& pass)
    {
        pass.Shader("Test", "MainCS");
        pass.DispatchThreads(8, 8, 1);
    }

    std::vector<int32_t> PassOrder(const loom::OrderedGraph& ordered)
    {
        std::vector<int32_t> passes;

        for (const loom::PassIndex pass : ordered.Passes)
        {
            passes.push_back(pass.Value);
        }

        return passes;
    }

    // Freeze must succeed: every test here is about Order, so a Freeze error is a broken test, not a result.
    std::optional<loom::OrderedGraph> FreezeAndOrder(const loom::GraphBuilder& graph, loom::CollectingSink& sink)
    {
        const std::optional<loom::FrozenGraph> frozen = loom::Freeze(graph.Record(), sink);
        EXPECT_TRUE(frozen.has_value()) << "Freeze failed";

        if (!frozen.has_value())
        {
            return std::nullopt;
        }

        const loom::BindAccessTable binds{ .Accesses = std::vector<std::vector<loom::ResourceAccess>>(frozen->Passes.size()) };
        return loom::Order(*frozen, binds, sink);
    }

}

// Nothing orders the three passes, so the tie-break decides: declaration order.
TEST(Order, UnorderedPassesKeepDeclarationOrder)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, ShaderAndThreads);
    graph.AddDispatch(k_Second, k_EveryFrame, k_Graphics, ShaderAndThreads);
    graph.AddDispatch(k_Third, k_EveryFrame, k_Graphics, ShaderAndThreads);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered = FreezeAndOrder(graph, sink);

    ASSERT_TRUE(ordered.has_value()) << sink.Diagnostics().size() << " diagnostics";
    EXPECT_EQ(PassOrder(*ordered), (std::vector<int32_t>{ 0, 1, 2 }));
}

// The passes are declared in the opposite order to the one the anchors state. The anchors win.
TEST(Order, AnchorsReorderPasses)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.DeclareAnchor(k_AnchorA);
    graph.DeclareAnchor(k_AnchorB);
    graph.OrderAnchors(k_AnchorA, k_AnchorB);

    auto afterB = [](loom::DispatchBuilder& pass)
    {
        ShaderAndThreads(pass);
        pass.After(k_AnchorB);
    };

    auto betweenAAndB = [](loom::DispatchBuilder& pass)
    {
        ShaderAndThreads(pass);
        pass.After(k_AnchorA);
        pass.Before(k_AnchorB);
    };

    auto beforeA = [](loom::DispatchBuilder& pass)
    {
        ShaderAndThreads(pass);
        pass.Before(k_AnchorA);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, afterB);
    graph.AddDispatch(k_Second, k_EveryFrame, k_Graphics, betweenAAndB);
    graph.AddDispatch(k_Third, k_EveryFrame, k_Graphics, beforeA);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered = FreezeAndOrder(graph, sink);

    ASSERT_TRUE(ordered.has_value()) << sink.Diagnostics().size() << " diagnostics";
    EXPECT_EQ(PassOrder(*ordered), (std::vector<int32_t>{ 2, 1, 0 }));
}

// After(EndFrame) meets the frame-bound edge pass -> EndFrame. The cycle is those two edges: one error, two notes.
TEST(Order, AfterEndFrameIsLM501WithEachEdge)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    uint32_t afterLine = 0;

    auto build = [&afterLine](loom::DispatchBuilder& pass)
    {
        ShaderAndThreads(pass);
        afterLine = std::source_location::current().line() + 1;
        pass.After(loom::EndFrameAnchor);
    };

    const uint32_t passLine = std::source_location::current().line() + 1;
    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, build);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered = FreezeAndOrder(graph, sink);

    ASSERT_FALSE(ordered.has_value());
    EXPECT_EQ(sink.ErrorCount(), 1);
    EXPECT_EQ(sink.NoteCount(), 2);
    ASSERT_EQ(sink.Diagnostics().size(), 3u);

    const loom::Diagnostic& error = sink.Diagnostics()[0];
    EXPECT_EQ(error.Code, loom::DiagnosticCode::LM501);
    EXPECT_EQ(error.Severity, loom::DiagnosticSeverity::Error);
    EXPECT_EQ(error.Pass.Hash, k_First.Hash);
    EXPECT_EQ(error.Where.line(), passLine);

    // Forward order: the pass runs before EndFrame (frame bound), and EndFrame runs before the pass (After).
    const loom::Diagnostic& bound = sink.Diagnostics()[1];
    EXPECT_EQ(bound.Severity, loom::DiagnosticSeverity::Note);
    EXPECT_EQ(bound.Actual.Text, "FrameBounds");
    EXPECT_EQ(bound.Where.line(), passLine);

    const loom::Diagnostic& after = sink.Diagnostics()[2];
    EXPECT_EQ(after.Actual.Text, "After");
    EXPECT_EQ(after.Where.line(), afterLine);
}

// A -> B (OrderAnchors), B -> pass (After B), pass -> A (Before A). Each of the three statements gets its note.
TEST(Order, AnchorCycleReportsEveryStatement)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.DeclareAnchor(k_AnchorA);
    graph.DeclareAnchor(k_AnchorB);

    const uint32_t orderLine = std::source_location::current().line() + 1;
    graph.OrderAnchors(k_AnchorA, k_AnchorB);

    uint32_t afterLine = 0;
    uint32_t beforeLine = 0;

    auto build = [&afterLine, &beforeLine](loom::DispatchBuilder& pass)
    {
        ShaderAndThreads(pass);
        afterLine = std::source_location::current().line() + 1;
        pass.After(k_AnchorB);
        beforeLine = std::source_location::current().line() + 1;
        pass.Before(k_AnchorA);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, build);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered = FreezeAndOrder(graph, sink);

    ASSERT_FALSE(ordered.has_value());
    ASSERT_EQ(sink.Diagnostics().size(), 4u);
    EXPECT_EQ(sink.Diagnostics()[0].Code, loom::DiagnosticCode::LM501);
    EXPECT_EQ(sink.Diagnostics()[0].Where.line(), beforeLine);

    // The walk starts at the pass, the lowest node left over, so the cycle reads from the pass's own edge.
    EXPECT_EQ(sink.Diagnostics()[1].Actual.Text, "Before");
    EXPECT_EQ(sink.Diagnostics()[1].Where.line(), beforeLine);
    EXPECT_EQ(sink.Diagnostics()[2].Actual.Text, "AnchorOrder");
    EXPECT_EQ(sink.Diagnostics()[2].Where.line(), orderLine);
    EXPECT_EQ(sink.Diagnostics()[3].Actual.Text, "After");
    EXPECT_EQ(sink.Diagnostics()[3].Where.line(), afterLine);
}

// The pass waits on a cycle between two anchors, but is not on it. The walk starts at the pass and enters the cycle,
// so the report must cut the edge that led in: two notes, both AnchorOrder, and no pass on the error.
TEST(Order, PathIntoCycleIsNotReported)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.DeclareAnchor(k_AnchorA);
    graph.DeclareAnchor(k_AnchorB);
    graph.OrderAnchors(k_AnchorA, k_AnchorB);
    graph.OrderAnchors(k_AnchorB, k_AnchorA);

    auto build = [](loom::DispatchBuilder& pass)
    {
        ShaderAndThreads(pass);
        pass.After(k_AnchorB);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, build);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered = FreezeAndOrder(graph, sink);

    ASSERT_FALSE(ordered.has_value());
    ASSERT_EQ(sink.Diagnostics().size(), 3u);
    EXPECT_EQ(sink.Diagnostics()[0].Code, loom::DiagnosticCode::LM501);
    EXPECT_TRUE(sink.Diagnostics()[0].Pass.Name.empty());
    EXPECT_EQ(sink.Diagnostics()[1].Actual.Text, "AnchorOrder");
    EXPECT_EQ(sink.Diagnostics()[2].Actual.Text, "AnchorOrder");
}
