#include "Declarations.hpp"
#include "freeze/FrozenGraph.hpp"
#include "GraphBuilder.hpp"
#include "ImageDataFormats.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "LoomKeys.hpp"
#include "PassBuilders.hpp"
#include "freeze_test_helpers.hpp"
#include "order/OrderContext.hpp"
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

    // The CPU writes it before the frame, so it has a source and needs no writer pass.
    loom::BufferDeclaration MakeUploadBuffer()
    {
        loom::BufferDeclaration buffer = MakeBuffer();
        buffer.Cpu = loom::CpuAccess::Upload;
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
// A sequence orders the three passes, because two unordered writers are LM502. The reader sits between the writers,
// because a reader first in the sequence would read before any write (LM503).
TEST(Order, TwoWritersAddNoDataEdges)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(MakeBuffer());

    auto buildTick = [](loom::SequenceBuilder& tick)
    {
        tick.AddDispatch(k_First, k_EveryFrame, k_Graphics, BindData);
        tick.AddDispatch(k_Second, k_EveryFrame, k_Graphics, BindData);
        tick.AddDispatch(k_Third, k_EveryFrame, k_Graphics, BindData);
    };

    graph.AddSequence(k_Tick, buildTick);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered =
        FreezeAndOrderWithAccesses(graph, sink, { { k_Write }, { k_Read }, { k_Write } });

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

// A ReadWrite pass is the writer and one of the readers. It gets no edge to itself, so there is no cycle. The buffer
// is an upload buffer: on a transient, a ReadWrite first writer reads undefined contents (LM503).
TEST(Order, ReadWritePassHasNoSelfEdge)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(MakeUploadBuffer());
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

// Hazards (LM502)

namespace
{

    constexpr loom::ResourceKey<loom::Buffer> k_Other{ "Test.Other" };

    loom::BufferDeclaration MakeOtherBuffer()
    {
        loom::BufferDeclaration buffer = MakeBuffer();
        buffer.Key = k_Other;
        return buffer;
    }

    void BindDataAndOther(loom::DispatchBuilder& pass)
    {
        ShaderAndThreads(pass);
        pass.Bind("Data", k_Data);
        pass.Bind("Other", k_Other);
    }

    std::vector<loom::Diagnostic> ErrorsOf(const loom::CollectingSink& sink)
    {
        std::vector<loom::Diagnostic> errors;

        for (const loom::Diagnostic& diagnostic : sink.Diagnostics())
        {
            if (diagnostic.Severity == loom::DiagnosticSeverity::Error)
            {
                errors.push_back(diagnostic);
            }
        }

        return errors;
    }

}

// Two writers with no stated order. One error, at the lower pass's Bind, that names the other pass and its Bind.
TEST(Order, UnorderedWritersAreLM502)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(MakeBuffer());
    uint32_t firstLine = 0;
    uint32_t secondLine = 0;

    auto writeFirst = [&firstLine](loom::DispatchBuilder& pass)
    {
        ShaderAndThreads(pass);
        firstLine = std::source_location::current().line() + 1;
        pass.Bind("Data", k_Data);
    };

    auto writeSecond = [&secondLine](loom::DispatchBuilder& pass)
    {
        ShaderAndThreads(pass);
        secondLine = std::source_location::current().line() + 1;
        pass.Bind("Data", k_Data);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, writeFirst);
    graph.AddDispatch(k_Second, k_EveryFrame, k_Graphics, writeSecond);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered = FreezeAndOrderWithAccesses(graph, sink, { { k_Write }, { k_Write } });

    ASSERT_FALSE(ordered.has_value());
    ASSERT_EQ(sink.Diagnostics().size(), 1u);

    const loom::Diagnostic& error = sink.Diagnostics()[0];
    EXPECT_EQ(error.Code, loom::DiagnosticCode::LM502);
    EXPECT_EQ(error.Severity, loom::DiagnosticSeverity::Error);
    EXPECT_EQ(error.Pass.Hash, k_First.Hash);
    EXPECT_EQ(error.Resource.Hash, k_Data.Hash);
    EXPECT_EQ(error.Actual.Text, k_Second.Name);
    EXPECT_EQ(error.Where.line(), firstLine);
    EXPECT_EQ(error.OtherWhere.line(), secondLine);
}

// Reader 0, writers 1 and 2, all unordered. The write-read loop finds (writer 1, reader 0), so the pair must be turned
// round: every error names the lower pass, and the errors come in pair order.
TEST(Order, HazardNamesTheLowerPassFirst)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(MakeBuffer());
    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, BindData);
    graph.AddDispatch(k_Second, k_EveryFrame, k_Graphics, BindData);
    graph.AddDispatch(k_Third, k_EveryFrame, k_Graphics, BindData);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered =
        FreezeAndOrderWithAccesses(graph, sink, { { k_Read }, { k_Write }, { k_Write } });

    ASSERT_FALSE(ordered.has_value());

    const std::vector<loom::Diagnostic> errors = ErrorsOf(sink);
    ASSERT_EQ(errors.size(), 3u);
    EXPECT_EQ(sink.NoteCount(), 0);

    // (0, 1), (0, 2), (1, 2)
    EXPECT_EQ(errors[0].Pass.Hash, k_First.Hash);
    EXPECT_EQ(errors[0].Actual.Text, k_Second.Name);
    EXPECT_EQ(errors[1].Pass.Hash, k_First.Hash);
    EXPECT_EQ(errors[1].Actual.Text, k_Third.Name);
    EXPECT_EQ(errors[2].Pass.Hash, k_Second.Hash);
    EXPECT_EQ(errors[2].Actual.Text, k_Third.Name);
}

// Her HDR example. Writers {0, 1}, readers {1, 2}: pass 1 is ReadWrite. An anchor orders 0 before 2. The pair (0, 1)
// comes from the write-write loop and again from the write-read loop. It must be reported once.
TEST(Order, PairFromBothLoopsIsReportedOnce)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(MakeBuffer());
    graph.DeclareAnchor(k_AnchorA);

    auto writeBeforeA = [](loom::DispatchBuilder& pass)
    {
        BindData(pass);
        pass.Before(k_AnchorA);
    };

    auto readAfterA = [](loom::DispatchBuilder& pass)
    {
        BindData(pass);
        pass.After(k_AnchorA);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, writeBeforeA);
    graph.AddDispatch(k_Second, k_EveryFrame, k_Graphics, BindData);
    graph.AddDispatch(k_Third, k_EveryFrame, k_Graphics, readAfterA);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered =
        FreezeAndOrderWithAccesses(graph, sink, { { k_Write }, { k_ReadWrite }, { k_Read } });

    ASSERT_FALSE(ordered.has_value());
    EXPECT_EQ(sink.NoteCount(), 0) << "a duplicate of a pair's slot is dropped, not turned into a note";

    const std::vector<loom::Diagnostic> errors = ErrorsOf(sink);
    ASSERT_EQ(errors.size(), 2u);
    EXPECT_EQ(errors[0].Pass.Hash, k_First.Hash);
    EXPECT_EQ(errors[0].Actual.Text, k_Second.Name);
    EXPECT_EQ(errors[1].Pass.Hash, k_Second.Hash);
    EXPECT_EQ(errors[1].Actual.Text, k_Third.Name);
}

// One pair conflicts on two slots: one error for the pair, then a note for the second slot.
TEST(Order, SecondSlotOfAPairIsANote)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(MakeBuffer());
    graph.Declare(MakeOtherBuffer());
    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, BindDataAndOther);
    graph.AddDispatch(k_Second, k_EveryFrame, k_Graphics, BindDataAndOther);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered =
        FreezeAndOrderWithAccesses(graph, sink, { { k_Write, k_Write }, { k_Write, k_Write } });

    ASSERT_FALSE(ordered.has_value());
    ASSERT_EQ(sink.Diagnostics().size(), 2u);

    const loom::Diagnostic& error = sink.Diagnostics()[0];
    EXPECT_EQ(error.Severity, loom::DiagnosticSeverity::Error);
    EXPECT_EQ(error.Resource.Hash, k_Data.Hash);

    const loom::Diagnostic& note = sink.Diagnostics()[1];
    EXPECT_EQ(note.Code, loom::DiagnosticCode::LM502);
    EXPECT_EQ(note.Severity, loom::DiagnosticSeverity::Note);
    EXPECT_EQ(note.Resource.Hash, k_Other.Hash);
    EXPECT_EQ(note.Pass.Hash, k_First.Hash);
}

// The slot loop finds (0, 1) on Data, then (1, 2) on Data, then (0, 1) on Other. Only the sort puts the two (0, 1)
// records together, so that Other becomes a note under the pair's error and not a second error.
TEST(Order, SortGroupsAPairAcrossSlots)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(MakeBuffer());
    graph.Declare(MakeOtherBuffer());
    graph.DeclareAnchor(k_AnchorA);

    auto writeBeforeA = [](loom::DispatchBuilder& pass)
    {
        BindDataAndOther(pass);
        pass.Before(k_AnchorA);
    };

    auto writeAfterA = [](loom::DispatchBuilder& pass)
    {
        BindData(pass);
        pass.After(k_AnchorA);
    };

    // 0 -> anchor -> 2 orders the pair (0, 2), so Data's hazards are (0, 1) and (1, 2).
    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, writeBeforeA);
    graph.AddDispatch(k_Second, k_EveryFrame, k_Graphics, BindDataAndOther);
    graph.AddDispatch(k_Third, k_EveryFrame, k_Graphics, writeAfterA);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered =
        FreezeAndOrderWithAccesses(graph, sink, { { k_Write, k_Write }, { k_Write, k_Write }, { k_Write } });

    ASSERT_FALSE(ordered.has_value());
    EXPECT_EQ(sink.ErrorCount(), 2);
    ASSERT_EQ(sink.NoteCount(), 1);
    ASSERT_EQ(sink.Diagnostics().size(), 3u);

    EXPECT_EQ(sink.Diagnostics()[0].Resource.Hash, k_Data.Hash);
    EXPECT_EQ(sink.Diagnostics()[0].Actual.Text, k_Second.Name);
    EXPECT_EQ(sink.Diagnostics()[1].Severity, loom::DiagnosticSeverity::Note);
    EXPECT_EQ(sink.Diagnostics()[1].Resource.Hash, k_Other.Hash);
    EXPECT_EQ(sink.Diagnostics()[2].Pass.Hash, k_Second.Hash);
    EXPECT_EQ(sink.Diagnostics()[2].Actual.Text, k_Third.Name);
}

// A sequence orders a writer, a ReadWrite pass and a reader. The ReadWrite pass is in both lists, and the strict
// closure does not order a pass with itself, so the check must skip that self-pair.
TEST(Order, OrderedWritersAndReadWriteSelfPairAreClean)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(MakeBuffer());

    auto buildTick = [](loom::SequenceBuilder& tick)
    {
        tick.AddDispatch(k_First, k_EveryFrame, k_Graphics, BindData);
        tick.AddDispatch(k_Second, k_EveryFrame, k_Graphics, BindData);
        tick.AddDispatch(k_Third, k_EveryFrame, k_Graphics, BindData);
    };

    graph.AddSequence(k_Tick, buildTick);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered =
        FreezeAndOrderWithAccesses(graph, sink, { { k_Write }, { k_ReadWrite }, { k_Read } });

    ASSERT_TRUE(ordered.has_value()) << sink.Diagnostics().size() << " diagnostics";
    EXPECT_EQ(sink.ErrorCount(), 0) << "the subject is errors; LM504 and LM505 warnings may appear";
}

// The two writers are ordered. The BindPrevious reader is not ordered with either, but it reads last frame's
// version, so it is not a hazard.
TEST(Order, PreviousReadIsNotAHazard)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(MakeBufferWithHistory());

    auto readPrevious = [](loom::DispatchBuilder& pass)
    {
        ShaderAndThreads(pass);
        pass.BindPrevious("Data", k_Data);
    };

    auto buildTick = [](loom::SequenceBuilder& tick)
    {
        tick.AddDispatch(k_First, k_EveryFrame, k_Graphics, BindData);
        tick.AddDispatch(k_Second, k_EveryFrame, k_Graphics, BindData);
    };

    graph.AddSequence(k_Tick, buildTick);
    graph.AddDispatch(k_Third, k_EveryFrame, k_Graphics, readPrevious);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered =
        FreezeAndOrderWithAccesses(graph, sink, { { k_Write }, { k_Write }, { k_Read } });

    ASSERT_TRUE(ordered.has_value()) << sink.Diagnostics().size() << " diagnostics";
    EXPECT_EQ(sink.ErrorCount(), 0) << "the subject is errors; LM504 and LM505 warnings may appear";
}

// The sink already holds an error from an earlier stage. A clean graph must still order: Order() fails on the errors
// that its own checks add, not on the total.
TEST(Order, EarlierErrorsDoNotFailACleanGraph)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(MakeBuffer());
    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, BindData);

    loom::CollectingSink sink;
    const std::optional<loom::FrozenGraph> frozen = loom::Freeze(graph.Record(), sink);
    ASSERT_TRUE(frozen.has_value());

    const loom::Diagnostic earlier
    {
        .Code = loom::DiagnosticCode::LM501,
        .Severity = loom::DiagnosticSeverity::Error,
    };
    sink.Report(earlier);

    const loom::BindAccessTable binds{ .Accesses = { { k_Write } } };
    const std::optional<loom::OrderedGraph> ordered = loom::Order(*frozen, binds, sink);

    EXPECT_TRUE(ordered.has_value());
    EXPECT_EQ(sink.ErrorCount(), 1);
}

// Reads before writes (LM503)

namespace
{

    loom::BufferDeclaration MakeOtherUploadBuffer()
    {
        loom::BufferDeclaration buffer = MakeOtherBuffer();
        buffer.Cpu = loom::CpuAccess::Upload;
        return buffer;
    }

}

// A transient that no pass writes. The read sees undefined memory: one error at the read, with no writer to name.
TEST(Order, ReadWithNoWriterIsLM503)
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

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered = FreezeAndOrderWithAccesses(graph, sink, { { k_Read } });

    ASSERT_FALSE(ordered.has_value());
    ASSERT_EQ(sink.Diagnostics().size(), 1u);

    const loom::Diagnostic& error = sink.Diagnostics()[0];
    EXPECT_EQ(error.Code, loom::DiagnosticCode::LM503);
    EXPECT_EQ(error.Severity, loom::DiagnosticSeverity::Error);
    EXPECT_EQ(error.Pass.Hash, k_First.Hash);
    EXPECT_EQ(error.Resource.Hash, k_Data.Hash);
    EXPECT_EQ(error.Where.line(), readLine);
    EXPECT_EQ(error.OtherWhere.line(), 0u) << "no writer, so no second location";
    EXPECT_TRUE(error.Actual.Text.empty());
}

// A sequence runs the reader before both writers (two writers, so no data edge moves it). The error names the first
// writer and its Bind. Data is the reader's first access and the writer's second, so the writer's location must come
// from the writer's own access list.
TEST(Order, ReaderBeforeFirstWriterIsLM503)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(MakeBuffer());
    graph.Declare(MakeOtherUploadBuffer());
    uint32_t readLine = 0;
    uint32_t writeLine = 0;

    auto readDataThenOther = [&readLine](loom::DispatchBuilder& pass)
    {
        ShaderAndThreads(pass);
        readLine = std::source_location::current().line() + 1;
        pass.Bind("Data", k_Data);
        pass.Bind("Other", k_Other);
    };

    auto readOtherThenWriteData = [&writeLine](loom::DispatchBuilder& pass)
    {
        ShaderAndThreads(pass);
        pass.Bind("Other", k_Other);
        writeLine = std::source_location::current().line() + 1;
        pass.Bind("Data", k_Data);
    };

    auto buildTick = [&readDataThenOther, &readOtherThenWriteData](loom::SequenceBuilder& tick)
    {
        tick.AddDispatch(k_First, k_EveryFrame, k_Graphics, readDataThenOther);
        tick.AddDispatch(k_Second, k_EveryFrame, k_Graphics, readOtherThenWriteData);
        tick.AddDispatch(k_Third, k_EveryFrame, k_Graphics, BindData);
    };

    graph.AddSequence(k_Tick, buildTick);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered =
        FreezeAndOrderWithAccesses(graph, sink, { { k_Read, k_Read }, { k_Read, k_Write }, { k_Write } });

    ASSERT_FALSE(ordered.has_value());
    ASSERT_EQ(sink.Diagnostics().size(), 1u) << "Other is an upload buffer, so its read is clean";

    const loom::Diagnostic& error = sink.Diagnostics()[0];
    EXPECT_EQ(error.Code, loom::DiagnosticCode::LM503);
    EXPECT_EQ(error.Pass.Hash, k_First.Hash);
    EXPECT_EQ(error.Resource.Hash, k_Data.Hash);
    EXPECT_EQ(error.Where.line(), readLine);
    EXPECT_EQ(error.OtherWhere.line(), writeLine);
    EXPECT_EQ(error.Actual.Text, k_Second.Name);
}

// A ReadWrite pass (a Keep load, a blend, a `+=`) is the first writer and reads before it writes. Equal positions are
// LM503. The read and the write are one statement, so both locations are that Bind.
TEST(Order, ReadWriteFirstWriterIsLM503)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(MakeBuffer());
    uint32_t bindLine = 0;

    auto readWrite = [&bindLine](loom::DispatchBuilder& pass)
    {
        ShaderAndThreads(pass);
        bindLine = std::source_location::current().line() + 1;
        pass.Bind("Data", k_Data);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, readWrite);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered = FreezeAndOrderWithAccesses(graph, sink, { { k_ReadWrite } });

    ASSERT_FALSE(ordered.has_value());
    ASSERT_EQ(sink.Diagnostics().size(), 1u);
    EXPECT_EQ(sink.Diagnostics()[0].Code, loom::DiagnosticCode::LM503);
    EXPECT_EQ(sink.Diagnostics()[0].Where.line(), bindLine);
    EXPECT_EQ(sink.Diagnostics()[0].OtherWhere.line(), bindLine);
    EXPECT_EQ(sink.Diagnostics()[0].Actual.Text, k_First.Name);
}

// Anchors run the passes in reverse declaration order: Third (write), Second (read), First (write). The first writer
// is the one that runs first, Third, not the lowest pass index, First. Chosen by pass index, the reader would wrongly
// come before it.
TEST(Order, FirstWriterIsFoundByRunOrder)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(MakeBuffer());
    graph.DeclareAnchor(k_AnchorA);
    graph.DeclareAnchor(k_AnchorB);
    graph.OrderAnchors(k_AnchorA, k_AnchorB);

    auto writeAfterB = [](loom::DispatchBuilder& pass)
    {
        BindData(pass);
        pass.After(k_AnchorB);
    };

    auto readBetween = [](loom::DispatchBuilder& pass)
    {
        BindData(pass);
        pass.After(k_AnchorA);
        pass.Before(k_AnchorB);
    };

    auto writeBeforeA = [](loom::DispatchBuilder& pass)
    {
        BindData(pass);
        pass.Before(k_AnchorA);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, writeAfterB);
    graph.AddDispatch(k_Second, k_EveryFrame, k_Graphics, readBetween);
    graph.AddDispatch(k_Third, k_EveryFrame, k_Graphics, writeBeforeA);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered =
        FreezeAndOrderWithAccesses(graph, sink, { { k_Write }, { k_Read }, { k_Write } });

    ASSERT_TRUE(ordered.has_value()) << sink.Diagnostics().size() << " diagnostics";
    EXPECT_EQ(sink.ErrorCount(), 0) << "the subject is errors; LM504 and LM505 warnings may appear";
    EXPECT_EQ(PassOrder(*ordered), (std::vector<int32_t>{ 2, 1, 0 }));
}

// Two readers and no writer. The anchor runs Second first, so the error is at Second: the earliest reader by run
// order, not by pass index.
TEST(Order, ReportIsAtTheEarliestReaderByRunOrder)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(MakeBuffer());
    graph.DeclareAnchor(k_AnchorA);

    auto readAfterA = [](loom::DispatchBuilder& pass)
    {
        BindData(pass);
        pass.After(k_AnchorA);
    };

    auto readBeforeA = [](loom::DispatchBuilder& pass)
    {
        BindData(pass);
        pass.Before(k_AnchorA);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, readAfterA);
    graph.AddDispatch(k_Second, k_EveryFrame, k_Graphics, readBeforeA);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered = FreezeAndOrderWithAccesses(graph, sink, { { k_Read }, { k_Read } });

    ASSERT_FALSE(ordered.has_value());
    ASSERT_EQ(sink.Diagnostics().size(), 1u) << "one error per slot, not one per reader";
    EXPECT_EQ(sink.Diagnostics()[0].Code, loom::DiagnosticCode::LM503);
    EXPECT_EQ(sink.Diagnostics()[0].Pass.Hash, k_Second.Hash);
}

// The CPU writes an upload buffer before the frame: a source. A read with no writer pass is the normal case.
TEST(Order, UploadBufferNeedsNoWriter)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(MakeUploadBuffer());
    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, BindData);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered = FreezeAndOrderWithAccesses(graph, sink, { { k_Read } });

    ASSERT_TRUE(ordered.has_value()) << sink.Diagnostics().size() << " diagnostics";
    EXPECT_EQ(sink.ErrorCount(), 0) << "the subject is errors; LM504 and LM505 warnings may appear";
}

// An import arrives with its contents (a texture from disk, a content compiler artifact): a source.
TEST(Order, ImportNeedsNoWriter)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    const ImageFormat format{ ImageComponentFormats::R8, ImageDataType::UNorm };
    graph.Import(loom::ImportDeclaration{ .Key = k_DataAsImage, .Expectation = loom::ImageExpectation::AnyExtent(format, 1) });

    auto readImport = [](loom::DispatchBuilder& pass)
    {
        ShaderAndThreads(pass);
        pass.Bind("Data", k_DataAsImage);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, readImport);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered = FreezeAndOrderWithAccesses(graph, sink, { { k_Read } });

    ASSERT_TRUE(ordered.has_value()) << sink.Diagnostics().size() << " diagnostics";
    EXPECT_EQ(sink.ErrorCount(), 0) << "the subject is errors; LM504 and LM505 warnings may appear";
}

// Data has an LM502 hazard, so its positions mean nothing and LM503 skips it. Other is clean of hazards and read with
// no writer, so it still gets its LM503: the skip is per slot, not for the whole graph.
TEST(Order, HazardSkipsOnlyItsOwnSlot)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(MakeBuffer());
    graph.Declare(MakeOtherBuffer());
    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, BindDataAndOther);
    graph.AddDispatch(k_Second, k_EveryFrame, k_Graphics, BindData);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered =
        FreezeAndOrderWithAccesses(graph, sink, { { k_Write, k_Read }, { k_Write } });

    ASSERT_FALSE(ordered.has_value());

    const std::vector<loom::Diagnostic> errors = ErrorsOf(sink);
    ASSERT_EQ(errors.size(), 2u);
    EXPECT_EQ(errors[0].Code, loom::DiagnosticCode::LM502);
    EXPECT_EQ(errors[0].Resource.Hash, k_Data.Hash);
    EXPECT_EQ(errors[1].Code, loom::DiagnosticCode::LM503);
    EXPECT_EQ(errors[1].Resource.Hash, k_Other.Hash);
}

// Dead writes (LM504) and orphan passes (LM505)

namespace
{

    constexpr loom::ResourceKey<loom::Buffer> k_Mid{ "Test.Mid" };

    std::vector<loom::Diagnostic> WithCode(const loom::CollectingSink& sink, loom::DiagnosticCode code)
    {
        std::vector<loom::Diagnostic> matches;

        for (const loom::Diagnostic& diagnostic : sink.Diagnostics())
        {
            if (diagnostic.Code == code)
            {
                matches.push_back(diagnostic);
            }
        }

        return matches;
    }

    loom::BufferDeclaration MakeReadbackBuffer()
    {
        loom::BufferDeclaration buffer = MakeBuffer();
        buffer.Cpu = loom::CpuAccess::Readback;
        return buffer;
    }

    loom::BufferDeclaration MakeOtherBufferWithHistory()
    {
        loom::BufferDeclaration buffer = MakeOtherBuffer();
        buffer.HistoryDepth = 1;
        buffer.NoHistoryAction = loom::OnNoHistory::Clear;
        return buffer;
    }

    loom::BufferDeclaration MakeMidBuffer()
    {
        loom::BufferDeclaration buffer = MakeBuffer();
        buffer.Key = k_Mid;
        return buffer;
    }

    void DrawToBackbuffer(loom::DrawBuilder& pass)
    {
        pass.Shaders("Test", "MainVS", "MainPS");
        pass.Color(0, k_Backbuffer, loom::ColorLoad::DontCare(), loom::AttachmentStore::Store, loom::BlendMode::Opaque);
        pass.Draw(3, 1, 0, 0);
    }

}

// Second overwrites First's write before anything reads it. The warning names the write and the pass that
// overwrote it. A warning does not fail Order().
TEST(Order, OverwrittenWriteIsLM504)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(MakeBufferWithHistory());
    uint32_t firstLine = 0;
    uint32_t secondLine = 0;

    auto writeFirst = [&firstLine](loom::DispatchBuilder& pass)
    {
        ShaderAndThreads(pass);
        firstLine = std::source_location::current().line() + 1;
        pass.Bind("Data", k_Data);
    };

    auto writeSecond = [&secondLine](loom::DispatchBuilder& pass)
    {
        ShaderAndThreads(pass);
        secondLine = std::source_location::current().line() + 1;
        pass.Bind("Data", k_Data);
    };

    auto buildTick = [&writeFirst, &writeSecond](loom::SequenceBuilder& tick)
    {
        tick.AddDispatch(k_First, k_EveryFrame, k_Graphics, writeFirst);
        tick.AddDispatch(k_Second, k_EveryFrame, k_Graphics, writeSecond);
    };

    graph.AddSequence(k_Tick, buildTick);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered = FreezeAndOrderWithAccesses(graph, sink, { { k_Write }, { k_Write } });

    ASSERT_TRUE(ordered.has_value()) << "warnings do not fail Order()";

    // History is a sink, but a sink sees only the last write: First is dead, Second is live
    const std::vector<loom::Diagnostic> dead = WithCode(sink, loom::DiagnosticCode::LM504);
    ASSERT_EQ(dead.size(), 1u);
    EXPECT_EQ(dead[0].Severity, loom::DiagnosticSeverity::Warning);
    EXPECT_EQ(dead[0].Pass.Hash, k_First.Hash);
    EXPECT_EQ(dead[0].Resource.Hash, k_Data.Hash);
    EXPECT_EQ(dead[0].Where.line(), firstLine);
    EXPECT_EQ(dead[0].OtherWhere.line(), secondLine);
    EXPECT_EQ(dead[0].Actual.Text, k_Second.Name);
}

// The only write, and nothing reads it: no reader, and a transient has no sink. No pass overwrote it.
TEST(Order, LastWriteWithNoSinkIsLM504)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(MakeBuffer());
    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, BindData);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered = FreezeAndOrderWithAccesses(graph, sink, { { k_Write } });

    ASSERT_TRUE(ordered.has_value());

    const std::vector<loom::Diagnostic> dead = WithCode(sink, loom::DiagnosticCode::LM504);
    ASSERT_EQ(dead.size(), 1u);
    EXPECT_EQ(dead[0].Pass.Hash, k_First.Hash);
    EXPECT_EQ(dead[0].OtherWhere.line(), 0u);
    EXPECT_TRUE(dead[0].Actual.Text.empty());
}

// The reader between the writers keeps First live; history keeps Second live. A reader after Second reads Second's
// write, not First's, so the windows must end at the next writer.
TEST(Order, ReaderInTheWindowKeepsTheWriteLive)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(MakeBufferWithHistory());

    auto buildTick = [](loom::SequenceBuilder& tick)
    {
        tick.AddDispatch(k_First, k_EveryFrame, k_Graphics, BindData);
        tick.AddDispatch(k_Second, k_EveryFrame, k_Graphics, BindData);
        tick.AddDispatch(k_Third, k_EveryFrame, k_Graphics, BindData);
    };

    graph.AddSequence(k_Tick, buildTick);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered =
        FreezeAndOrderWithAccesses(graph, sink, { { k_Write }, { k_Read }, { k_Write } });

    ASSERT_TRUE(ordered.has_value());
    EXPECT_TRUE(WithCode(sink, loom::DiagnosticCode::LM504).empty());
}

// The same three passes, with the reader last. Now no reader sits between the writers: First is dead.
TEST(Order, ReaderAfterTheNextWriteDoesNotSaveTheWrite)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(MakeBuffer());

    auto buildTick = [](loom::SequenceBuilder& tick)
    {
        tick.AddDispatch(k_First, k_EveryFrame, k_Graphics, BindData);
        tick.AddDispatch(k_Second, k_EveryFrame, k_Graphics, BindData);
        tick.AddDispatch(k_Third, k_EveryFrame, k_Graphics, BindData);
    };

    graph.AddSequence(k_Tick, buildTick);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered =
        FreezeAndOrderWithAccesses(graph, sink, { { k_Write }, { k_Write }, { k_Read } });

    ASSERT_TRUE(ordered.has_value());

    const std::vector<loom::Diagnostic> dead = WithCode(sink, loom::DiagnosticCode::LM504);
    ASSERT_EQ(dead.size(), 1u);
    EXPECT_EQ(dead[0].Pass.Hash, k_First.Hash);
}

// Second is ReadWrite (a blend onto First's clear): it reads First's write, so First is live. History keeps
// Second live. Every pass reaches the sink, so there is no warning at all.
TEST(Order, ReadWriteNextWriterKeepsTheWriteLive)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(MakeBufferWithHistory());

    auto buildTick = [](loom::SequenceBuilder& tick)
    {
        tick.AddDispatch(k_First, k_EveryFrame, k_Graphics, BindData);
        tick.AddDispatch(k_Second, k_EveryFrame, k_Graphics, BindData);
    };

    graph.AddSequence(k_Tick, buildTick);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered = FreezeAndOrderWithAccesses(graph, sink, { { k_Write }, { k_ReadWrite } });

    ASSERT_TRUE(ordered.has_value());
    EXPECT_TRUE(sink.Diagnostics().empty()) << sink.Diagnostics().size() << " diagnostics";
}

// A ReadWrite pass's read sees the write before it, never its own. On an upload buffer (a source, so not LM503) with
// no sink, its write is dead.
TEST(Order, ReadWriteDoesNotReadItsOwnWrite)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(MakeUploadBuffer());
    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, BindData);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered = FreezeAndOrderWithAccesses(graph, sink, { { k_ReadWrite } });

    ASSERT_TRUE(ordered.has_value());
    EXPECT_EQ(WithCode(sink, loom::DiagnosticCode::LM504).size(), 1u);
}

// Each kind of sink keeps a last write live: readback (the CPU reads it), an import (its owner reads it), and a frame
// target that the graph outputs (the present reads it).
TEST(Order, ReadbackKeepsTheLastWriteLive)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(MakeReadbackBuffer());
    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, BindData);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered = FreezeAndOrderWithAccesses(graph, sink, { { k_Write } });

    ASSERT_TRUE(ordered.has_value());
    EXPECT_TRUE(sink.Diagnostics().empty()) << sink.Diagnostics().size() << " diagnostics";
}

TEST(Order, ImportKeepsTheLastWriteLive)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    const ImageFormat format{ ImageComponentFormats::R8, ImageDataType::UNorm };
    graph.Import(loom::ImportDeclaration{ .Key = k_DataAsImage, .Expectation = loom::ImageExpectation::AnyExtent(format, 1) });

    auto writeImport = [](loom::DispatchBuilder& pass)
    {
        ShaderAndThreads(pass);
        pass.Bind("Data", k_DataAsImage);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, writeImport);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered = FreezeAndOrderWithAccesses(graph, sink, { { k_Write } });

    ASSERT_TRUE(ordered.has_value());
    EXPECT_TRUE(sink.Diagnostics().empty()) << sink.Diagnostics().size() << " diagnostics";
}

TEST(Order, OutputFrameTargetKeepsTheLastWriteLive)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Import(k_Backbuffer);
    graph.Output(loom::OutputDeclaration{ .Target = k_Backbuffer, .OnSkip = loom::OutputFallback::Clear(0.0f, 0.0f, 0.0f, 1.0f) });
    graph.AddDraw(k_First, k_EveryFrame, k_Graphics, DrawToBackbuffer);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered = FreezeAndOrderWithAccesses(graph, sink, { {} });

    ASSERT_TRUE(ordered.has_value());
    EXPECT_TRUE(sink.Diagnostics().empty()) << sink.Diagnostics().size() << " diagnostics";
}

// The same draw with no Output: nothing presents the frame target, so the write is dead and the pass is an orphan.
TEST(Order, FrameTargetWithoutOutputIsNotASink)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Import(k_Backbuffer);
    graph.AddDraw(k_First, k_EveryFrame, k_Graphics, DrawToBackbuffer);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered = FreezeAndOrderWithAccesses(graph, sink, { {} });

    ASSERT_TRUE(ordered.has_value());
    EXPECT_EQ(WithCode(sink, loom::DiagnosticCode::LM504).size(), 1u);
    EXPECT_EQ(WithCode(sink, loom::DiagnosticCode::LM505).size(), 1u);
}

// A frame target has no source (its contents are undefined after acquire). A Keep load is a ReadWrite first writer:
// LM503 on the frame-target slot.
TEST(Order, KeepLoadOfAFrameTargetIsLM503)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Import(k_Backbuffer);
    graph.Output(loom::OutputDeclaration{ .Target = k_Backbuffer, .OnSkip = loom::OutputFallback::Clear(0.0f, 0.0f, 0.0f, 1.0f) });

    auto keepBackbuffer = [](loom::DrawBuilder& pass)
    {
        pass.Shaders("Test", "MainVS", "MainPS");
        pass.Color(0, k_Backbuffer, loom::ColorLoad::Keep(), loom::AttachmentStore::Store, loom::BlendMode::Opaque);
        pass.Draw(3, 1, 0, 0);
    };

    graph.AddDraw(k_First, k_EveryFrame, k_Graphics, keepBackbuffer);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered = FreezeAndOrderWithAccesses(graph, sink, { {} });

    ASSERT_FALSE(ordered.has_value());
    ASSERT_EQ(sink.Diagnostics().size(), 1u) << "the warnings do not run on a graph with errors";
    EXPECT_EQ(sink.Diagnostics()[0].Code, loom::DiagnosticCode::LM503);
    EXPECT_EQ(sink.Diagnostics()[0].Resource.Hash, k_Backbuffer.Hash);
}

// First feeds Second, Second feeds Third, and Third writes a history buffer (a sink). The walk goes back from Third
// through two hops, so no pass is an orphan.
TEST(Order, ChainToASinkHasNoOrphans)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(MakeBuffer());
    graph.Declare(MakeMidBuffer());
    graph.Declare(MakeOtherBufferWithHistory());

    auto readDataWriteMid = [](loom::DispatchBuilder& pass)
    {
        ShaderAndThreads(pass);
        pass.Bind("Data", k_Data);
        pass.Bind("Mid", k_Mid);
    };

    auto readMidWriteOther = [](loom::DispatchBuilder& pass)
    {
        ShaderAndThreads(pass);
        pass.Bind("Mid", k_Mid);
        pass.Bind("Other", k_Other);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, BindData);
    graph.AddDispatch(k_Second, k_EveryFrame, k_Graphics, readDataWriteMid);
    graph.AddDispatch(k_Third, k_EveryFrame, k_Graphics, readMidWriteOther);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered =
        FreezeAndOrderWithAccesses(graph, sink, { { k_Write }, { k_Read, k_Write }, { k_Read, k_Write } });

    ASSERT_TRUE(ordered.has_value());
    EXPECT_TRUE(sink.Diagnostics().empty()) << sink.Diagnostics().size() << " diagnostics";
}

// The same chain, but Third writes a transient. Its write is dead, and nothing reaches a sink: every pass is an
// orphan, each reported at its declaration.
TEST(Order, ChainToADeadEndIsAllOrphans)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(MakeBuffer());
    graph.Declare(MakeMidBuffer());
    graph.Declare(MakeOtherBuffer());

    auto readDataWriteMid = [](loom::DispatchBuilder& pass)
    {
        ShaderAndThreads(pass);
        pass.Bind("Data", k_Data);
        pass.Bind("Mid", k_Mid);
    };

    auto readMidWriteOther = [](loom::DispatchBuilder& pass)
    {
        ShaderAndThreads(pass);
        pass.Bind("Mid", k_Mid);
        pass.Bind("Other", k_Other);
    };

    const uint32_t firstLine = std::source_location::current().line() + 1;
    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, BindData);
    graph.AddDispatch(k_Second, k_EveryFrame, k_Graphics, readDataWriteMid);
    graph.AddDispatch(k_Third, k_EveryFrame, k_Graphics, readMidWriteOther);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered =
        FreezeAndOrderWithAccesses(graph, sink, { { k_Write }, { k_Read, k_Write }, { k_Read, k_Write } });

    ASSERT_TRUE(ordered.has_value());
    EXPECT_EQ(WithCode(sink, loom::DiagnosticCode::LM504).size(), 1u) << "only Third's write is dead";

    const std::vector<loom::Diagnostic> orphans = WithCode(sink, loom::DiagnosticCode::LM505);
    ASSERT_EQ(orphans.size(), 3u);
    EXPECT_EQ(orphans[0].Severity, loom::DiagnosticSeverity::Warning);
    EXPECT_EQ(orphans[0].Pass.Hash, k_First.Hash);
    EXPECT_EQ(orphans[0].Where.line(), firstLine);
    EXPECT_EQ(orphans[1].Pass.Hash, k_Second.Hash);
    EXPECT_EQ(orphans[2].Pass.Hash, k_Third.Hash);
}

// Anchors run four passes in reverse declaration order: Fourth writes, Third reads, Second writes, First reads.
// Writers[slot] and Readers[slot] come in pass-index order, so the check must sort both by run order. Sorted, each
// write has its reader and the graph is clean.
TEST(Order, WindowsFollowRunOrderNotDeclarationOrder)
{
    constexpr loom::PassKey k_Fourth{ "Test.Fourth" };
    constexpr loom::AnchorKey k_AnchorC{ "Test.AnchorC" };

    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(MakeBufferWithHistory());
    graph.DeclareAnchor(k_AnchorA);
    graph.DeclareAnchor(k_AnchorB);
    graph.DeclareAnchor(k_AnchorC);
    graph.OrderAnchors(k_AnchorA, k_AnchorB);
    graph.OrderAnchors(k_AnchorB, k_AnchorC);

    auto afterC = [k_AnchorC](loom::DispatchBuilder& pass)
    {
        BindData(pass);
        pass.After(k_AnchorC);
    };

    auto betweenBAndC = [k_AnchorC](loom::DispatchBuilder& pass)
    {
        BindData(pass);
        pass.After(k_AnchorB);
        pass.Before(k_AnchorC);
    };

    auto betweenAAndB = [](loom::DispatchBuilder& pass)
    {
        BindData(pass);
        pass.After(k_AnchorA);
        pass.Before(k_AnchorB);
    };

    auto beforeA = [](loom::DispatchBuilder& pass)
    {
        BindData(pass);
        pass.Before(k_AnchorA);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, afterC);
    graph.AddDispatch(k_Second, k_EveryFrame, k_Graphics, betweenBAndC);
    graph.AddDispatch(k_Third, k_EveryFrame, k_Graphics, betweenAAndB);
    graph.AddDispatch(k_Fourth, k_EveryFrame, k_Graphics, beforeA);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered =
        FreezeAndOrderWithAccesses(graph, sink, { { k_Read }, { k_Write }, { k_Read }, { k_Write } });

    ASSERT_TRUE(ordered.has_value());
    EXPECT_EQ(PassOrder(*ordered), (std::vector<int32_t>{ 3, 2, 1, 0 }));
    EXPECT_TRUE(WithCode(sink, loom::DiagnosticCode::LM504).empty());

    // First and Third only read, so they write nothing a sink sees; Fourth feeds only Third. Second reaches the sink.
    const std::vector<loom::Diagnostic> orphans = WithCode(sink, loom::DiagnosticCode::LM505);
    ASSERT_EQ(orphans.size(), 3u);
    EXPECT_EQ(orphans[0].Pass.Hash, k_First.Hash);
    EXPECT_EQ(orphans[1].Pass.Hash, k_Third.Hash);
    EXPECT_EQ(orphans[2].Pass.Hash, k_Fourth.Hash);
}

// Reachability tests build the edges, successors and topological order by hand. A failure here is in
// ComputeReachability, not in the edge builders or the sort.
namespace
{

    struct TestEdge
    {
        int32_t From{ -1 };
        int32_t To{ -1 };
    };

    loom::order::Reachability ReachabilityOf(int32_t pass_count,
                                             int32_t node_count,
                                             const std::vector<TestEdge>& edges,
                                             const std::vector<int32_t>& topological_order)
    {
        const loom::FrozenGraph frozen{};
        const loom::BindAccessTable binds{};
        loom::CollectingSink sink;
        loom::order::OrderContext context{
            .Frozen = frozen,
            .Binds = binds,
            .Sink = sink,
            .PassCount = pass_count,
            .NodeCount = node_count,
            .SlotCount = 0,
        };
        context.Successors.resize(static_cast<size_t>(node_count));

        for (const TestEdge& edge : edges)
        {
            context.Successors[edge.From].push_back(static_cast<int32_t>(context.Edges.size()));
            context.Edges.push_back(loom::OrderEdge{
                .From = loom::NodeIndex{ edge.From },
                .To = loom::NodeIndex{ edge.To },
                .Reason = loom::EdgeReason::After,
            });
        }

        std::vector<loom::NodeIndex> ordered;

        for (const int32_t node : topological_order)
        {
            ordered.push_back(loom::NodeIndex{ node });
        }

        return loom::order::ComputeReachability(context, ordered);
    }

    bool Reaches(const loom::order::Reachability& reach, int32_t from, int32_t to)
    {
        return loom::order::Reaches(reach, loom::NodeIndex{ from }, loom::NodeIndex{ to });
    }

    bool Ordered(const loom::order::Reachability& reach, int32_t lhs, int32_t rhs)
    {
        return loom::order::Ordered(reach, loom::NodeIndex{ lhs }, loom::NodeIndex{ rhs });
    }

    // A chain 0 -> 1 -> ... -> (count - 1), in index order.
    std::vector<TestEdge> ChainEdges(int32_t count)
    {
        std::vector<TestEdge> edges;

        for (int32_t node = 0; node + 1 < count; ++node)
        {
            edges.push_back(TestEdge{ .From = node, .To = node + 1 });
        }

        return edges;
    }

    std::vector<int32_t> IndexOrder(int32_t count)
    {
        std::vector<int32_t> order;

        for (int32_t node = 0; node < count; ++node)
        {
            order.push_back(node);
        }

        return order;
    }

}

// The row of a node holds its successors and everything they reach. Nothing reaches backwards.
TEST(OrderReachability, ChainReachesTransitively)
{
    const loom::order::Reachability reach = ReachabilityOf(3, 3, ChainEdges(3), IndexOrder(3));

    EXPECT_TRUE(Reaches(reach, 0, 1));
    EXPECT_TRUE(Reaches(reach, 1, 2));
    EXPECT_TRUE(Reaches(reach, 0, 2)) << "0 reaches 2 only through 1";
    EXPECT_FALSE(Reaches(reach, 1, 0));
    EXPECT_FALSE(Reaches(reach, 2, 0));
    EXPECT_FALSE(Reaches(reach, 2, 1));
}

// The topological order is not the index order: 2 -> 0 -> 1. The fill must follow the order it is given.
TEST(OrderReachability, FollowsTheGivenOrderNotTheIndexOrder)
{
    const std::vector<TestEdge> edges{ { .From = 2, .To = 0 }, { .From = 0, .To = 1 } };
    const loom::order::Reachability reach = ReachabilityOf(3, 3, edges, { 2, 0, 1 });

    EXPECT_TRUE(Reaches(reach, 2, 1));
    EXPECT_FALSE(Reaches(reach, 1, 2));
}

// 0 -> 1 -> 3 and 0 -> 2 -> 3. The two middle nodes have no path between them, so they are unordered.
TEST(OrderReachability, DiamondSidesAreUnordered)
{
    const std::vector<TestEdge> edges{
        { .From = 0, .To = 1 },
        { .From = 0, .To = 2 },
        { .From = 1, .To = 3 },
        { .From = 2, .To = 3 },
    };
    const loom::order::Reachability reach = ReachabilityOf(4, 4, edges, IndexOrder(4));

    EXPECT_TRUE(Reaches(reach, 0, 3));
    EXPECT_TRUE(Ordered(reach, 3, 0)) << "Ordered is symmetric";
    EXPECT_FALSE(Ordered(reach, 1, 2));
}

// A node with no edges is ordered with nothing.
TEST(OrderReachability, IsolatedNodeIsUnordered)
{
    const std::vector<TestEdge> edges{ { .From = 0, .To = 1 } };
    const loom::order::Reachability reach = ReachabilityOf(3, 3, edges, IndexOrder(3));

    EXPECT_FALSE(Ordered(reach, 0, 2));
    EXPECT_FALSE(Ordered(reach, 1, 2));
}

// Passes 0 and 1, anchor node 2: 0 -> anchor -> 1. The anchor does no work, but it orders the passes.
TEST(OrderReachability, PathThroughAnchorOrdersPasses)
{
    const std::vector<TestEdge> edges{ { .From = 0, .To = 2 }, { .From = 2, .To = 1 } };
    const loom::order::Reachability reach = ReachabilityOf(2, 3, edges, { 0, 2, 1 });

    EXPECT_TRUE(Reaches(reach, 0, 1));
    EXPECT_FALSE(Reaches(reach, 1, 0));
}

// The closure is strict: in a DAG no node reaches itself.
TEST(OrderReachability, NoNodeReachesItself)
{
    const loom::order::Reachability reach = ReachabilityOf(4, 4, ChainEdges(4), IndexOrder(4));

    for (int32_t node = 0; node < 4; ++node)
    {
        EXPECT_FALSE(Reaches(reach, node, node)) << "node " << node;
    }
}

// A row is ceil(NodeCount / 64) words: exactly one word at 64 nodes, two at 65.
TEST(OrderReachability, WordsPerNodeRoundsUp)
{
    EXPECT_EQ(ReachabilityOf(1, 1, {}, IndexOrder(1)).WordsPerNode, 1);
    EXPECT_EQ(ReachabilityOf(64, 64, {}, IndexOrder(64)).WordsPerNode, 1);
    EXPECT_EQ(ReachabilityOf(65, 65, {}, IndexOrder(65)).WordsPerNode, 2);
}

// 130 nodes make three words per row. Bits 63, 64, 127, 128 and 129 cross the word boundaries, and bits past 31
// fail if a shift is done in 32 bits.
TEST(OrderReachability, ChainCrossesWordBoundaries)
{
    const int32_t count = 130;
    const loom::order::Reachability reach = ReachabilityOf(count, count, ChainEdges(count), IndexOrder(count));

    ASSERT_EQ(reach.WordsPerNode, 3);

    for (const int32_t target : { 1, 31, 32, 40, 63, 64, 127, 128, 129 })
    {
        EXPECT_TRUE(Reaches(reach, 0, target)) << "0 to " << target;
    }

    EXPECT_TRUE(Reaches(reach, 63, 64));
    EXPECT_TRUE(Reaches(reach, 64, 129));
    EXPECT_FALSE(Reaches(reach, 64, 63));
    EXPECT_FALSE(Reaches(reach, 129, 0));

    // Spot-check every pair: i reaches j exactly when i < j.
    for (int32_t from = 0; from < count; from += 7)
    {
        for (int32_t to = 0; to < count; to += 5)
        {
            EXPECT_EQ(Reaches(reach, from, to), from < to) << from << " to " << to;
        }
    }
}
