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
    constexpr loom::SequenceKey k_Tick{ "Test.Tick" };
    constexpr loom::PassKey k_TickA{ "Test.TickA" };
    constexpr loom::PassKey k_TickB{ "Test.TickB" };

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

    // The Bind stage does not exist yet, so the data-flow tests state each bind's access by hand: one row per pass in
    // declaration order, one entry per Bind call in that pass.
    std::optional<loom::OrderedGraph> FreezeAndOrderWithAccesses(const loom::GraphBuilder& graph,
                                                                 loom::CollectingSink& sink,
                                                                 std::vector<std::vector<loom::ResourceAccess>> accesses)
    {
        const std::optional<loom::FrozenGraph> frozen = loom::Freeze(graph.Record(), sink);
        EXPECT_TRUE(frozen.has_value()) << "Freeze failed";

        if (!frozen.has_value())
        {
            return std::nullopt;
        }

        EXPECT_EQ(accesses.size(), frozen->Passes.size()) << "one access row per pass";

        const loom::BindAccessTable binds{ .Accesses = std::move(accesses) };
        return loom::Order(*frozen, binds, sink);
    }

    std::vector<loom::OrderEdge> DataFlowEdges(const loom::OrderedGraph& ordered)
    {
        std::vector<loom::OrderEdge> edges;

        for (const loom::OrderEdge& edge : ordered.Edges)
        {
            if (edge.Reason == loom::EdgeReason::DataFlow)
            {
                edges.push_back(edge);
            }
        }

        return edges;
    }

    constexpr loom::ResourceAccess k_Read = loom::ResourceAccess::Read;
    constexpr loom::ResourceAccess k_Write = loom::ResourceAccess::Write;
    constexpr loom::ResourceAccess k_ReadWrite = loom::ResourceAccess::ReadWrite;

    void BindData(loom::DispatchBuilder& pass)
    {
        ShaderAndThreads(pass);
        pass.Bind("Data", k_Data);
    }

    loom::BufferDeclaration MakeBufferWithHistory()
    {
        loom::BufferDeclaration buffer = MakeBuffer();
        buffer.HistoryDepth = 1;
        buffer.NoHistoryAction = loom::OnNoHistory::Clear;
        return buffer;
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

// Sequences

// A top-level pass is declared first, so the sequence starts at pass 1, not 0. The exposed anchor's position is local
// to the sequence. First is After the anchor and Second is Before it, so a global reading of either side of the anchor
// makes a cycle (pass 0 -> anchor -> pass 0, or pass 1 -> anchor -> pass 1).
TEST(Order, ExposedAnchorPositionIsLocalToTheSequence)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };

    auto afterA = [](loom::DispatchBuilder& pass)
    {
        ShaderAndThreads(pass);
        pass.After(k_AnchorA);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, afterA);

    auto buildTick = [](loom::SequenceBuilder& tick)
    {
        tick.AddDispatch(k_TickA, k_EveryFrame, k_Graphics, ShaderAndThreads);
        tick.ExposeAnchor(k_AnchorA);
        tick.AddDispatch(k_TickB, k_EveryFrame, k_Graphics, ShaderAndThreads);
    };

    graph.AddSequence(k_Tick, buildTick);

    auto beforeA = [](loom::DispatchBuilder& pass)
    {
        ShaderAndThreads(pass);
        pass.Before(k_AnchorA);
    };

    graph.AddDispatch(k_Second, k_EveryFrame, k_Graphics, beforeA);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered = FreezeAndOrder(graph, sink);

    // Passes: 0 First, 1 TickA, 2 TickB, 3 Second. TickA and Second come before the anchor; First and TickB after it.
    ASSERT_TRUE(ordered.has_value()) << sink.Diagnostics().size() << " diagnostics";
    EXPECT_EQ(PassOrder(*ordered), (std::vector<int32_t>{ 1, 3, 0, 2 }));
}

// Data flow (Q3h)

// The reader is declared first. The one writer's edge moves it after the writer: the edges define the order.
TEST(Order, OneWriterOrdersItsReader)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(MakeBuffer());
    uint32_t readLine = 0;

    auto read = [&readLine](loom::DispatchBuilder& pass)
    {
        ShaderAndThreads(pass);
        readLine = std::source_location::current().line() + 1;
        pass.Bind("Data", k_Data);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, read);
    graph.AddDispatch(k_Second, k_EveryFrame, k_Graphics, BindData);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered = FreezeAndOrderWithAccesses(graph, sink, { { k_Read }, { k_Write } });

    ASSERT_TRUE(ordered.has_value()) << sink.Diagnostics().size() << " diagnostics";
    EXPECT_EQ(PassOrder(*ordered), (std::vector<int32_t>{ 1, 0 }));

    const std::vector<loom::OrderEdge> edges = DataFlowEdges(*ordered);
    ASSERT_EQ(edges.size(), 1u);
    EXPECT_EQ(edges[0].From.Value, 1);
    EXPECT_EQ(edges[0].To.Value, 0);
    EXPECT_EQ(edges[0].Resource.Value, 0) << "the graph's only resource";
    EXPECT_EQ(edges[0].Where.line(), readLine) << "the reader's Bind made the edge";
}

// Two writers: the graph does not pick which one the reader follows. No data edges; the declaration order stays.
TEST(Order, TwoWritersAddNoDataEdges)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(MakeBuffer());
    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, BindData);
    graph.AddDispatch(k_Second, k_EveryFrame, k_Graphics, BindData);
    graph.AddDispatch(k_Third, k_EveryFrame, k_Graphics, BindData);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered =
        FreezeAndOrderWithAccesses(graph, sink, { { k_Read }, { k_Write }, { k_Write } });

    ASSERT_TRUE(ordered.has_value()) << sink.Diagnostics().size() << " diagnostics";
    EXPECT_TRUE(DataFlowEdges(*ordered).empty());
    EXPECT_EQ(PassOrder(*ordered), (std::vector<int32_t>{ 0, 1, 2 }));
}

// Writers count as passes, not as statements: two write binds in one pass are still one writer.
TEST(Order, TwoWriteBindsInOnePassAreOneWriter)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(MakeBuffer());

    auto writeTwice = [](loom::DispatchBuilder& pass)
    {
        ShaderAndThreads(pass);
        pass.Bind("DataA", k_Data);
        pass.Bind("DataB", k_Data);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, BindData);
    graph.AddDispatch(k_Second, k_EveryFrame, k_Graphics, writeTwice);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered =
        FreezeAndOrderWithAccesses(graph, sink, { { k_Read }, { k_Write, k_Write } });

    ASSERT_TRUE(ordered.has_value()) << sink.Diagnostics().size() << " diagnostics";
    EXPECT_EQ(DataFlowEdges(*ordered).size(), 1u);
    EXPECT_EQ(PassOrder(*ordered), (std::vector<int32_t>{ 1, 0 }));
}

// A ReadWrite pass is the writer and one of the readers. It gets no edge to itself, so there is no cycle.
TEST(Order, ReadWritePassHasNoSelfEdge)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(MakeBuffer());
    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, BindData);
    graph.AddDispatch(k_Second, k_EveryFrame, k_Graphics, BindData);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered =
        FreezeAndOrderWithAccesses(graph, sink, { { k_Read }, { k_ReadWrite } });

    ASSERT_TRUE(ordered.has_value()) << sink.Diagnostics().size() << " diagnostics";

    const std::vector<loom::OrderEdge> edges = DataFlowEdges(*ordered);
    ASSERT_EQ(edges.size(), 1u);
    EXPECT_EQ(edges[0].From.Value, 1);
    EXPECT_EQ(edges[0].To.Value, 0);
}

// BindPrevious reads last frame's contents, so this frame's writer gets no edge to it. The reader is declared first,
// and it stays first.
TEST(Order, PreviousReadGetsNoEdge)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(MakeBufferWithHistory());

    auto readPrevious = [](loom::DispatchBuilder& pass)
    {
        ShaderAndThreads(pass);
        pass.BindPrevious("Data", k_Data);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, readPrevious);
    graph.AddDispatch(k_Second, k_EveryFrame, k_Graphics, BindData);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered = FreezeAndOrderWithAccesses(graph, sink, { { k_Read }, { k_Write } });

    ASSERT_TRUE(ordered.has_value()) << sink.Diagnostics().size() << " diagnostics";
    EXPECT_TRUE(DataFlowEdges(*ordered).empty());
    EXPECT_EQ(PassOrder(*ordered), (std::vector<int32_t>{ 0, 1 }));
}

// The anchors put the reader before the writer, and the data puts it after. The graph never picks one in silence (Q3b):
// LM501, with a DataFlow note at the reader's Bind.
TEST(Order, DataEdgeAgainstAnchorsIsLM501)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(MakeBuffer());
    graph.DeclareAnchor(k_AnchorA);
    uint32_t readLine = 0;

    auto readBeforeA = [&readLine](loom::DispatchBuilder& pass)
    {
        ShaderAndThreads(pass);
        pass.Before(k_AnchorA);
        readLine = std::source_location::current().line() + 1;
        pass.Bind("Data", k_Data);
    };

    auto writeAfterA = [](loom::DispatchBuilder& pass)
    {
        BindData(pass);
        pass.After(k_AnchorA);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, readBeforeA);
    graph.AddDispatch(k_Second, k_EveryFrame, k_Graphics, writeAfterA);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered = FreezeAndOrderWithAccesses(graph, sink, { { k_Read }, { k_Write } });

    ASSERT_FALSE(ordered.has_value());
    EXPECT_EQ(sink.ErrorCount(), 1);
    ASSERT_EQ(sink.NoteCount(), 3);

    int32_t dataFlowNotes = 0;

    for (const loom::Diagnostic& diagnostic : sink.Diagnostics())
    {
        if (diagnostic.Severity == loom::DiagnosticSeverity::Note && diagnostic.Actual.Text == "DataFlow")
        {
            ++dataFlowNotes;
            EXPECT_EQ(diagnostic.Where.line(), readLine);
        }
    }

    EXPECT_EQ(dataFlowNotes, 1);
}
