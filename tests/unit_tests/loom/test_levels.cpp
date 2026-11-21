#include "Declarations.hpp"
#include "PassStatements.hpp"
#include "Records.hpp"
#include "freeze/FrozenGraph.hpp"
#include "GraphBuilder.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "LoomKeys.hpp"
#include "PassBuilders.hpp"
#include "freeze_test_helpers.hpp"
#include "order/OrderedGraph.hpp"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>
#include <gtest/gtest.h>

// Step 5 additions to Freeze and Order: buffer imports (CC-R3), Kahn levels and the level-major run order, and the
// persistence rule for LM503 (a writer whose run policy is not EveryFrame keeps its contents across frames).

using namespace freeze_tests;

namespace
{

    constexpr loom::PassKey k_Second{ "Levels.Second" };
    constexpr loom::PassKey k_Third{ "Levels.Third" };
    constexpr loom::AnchorKey k_AnchorA{ "Levels.AnchorA" };
    constexpr loom::AnchorKey k_AnchorB{ "Levels.AnchorB" };
    constexpr loom::ResourceKey<loom::Buffer> k_Supplied{ "Levels.Supplied" };

    constexpr loom::ResourceAccess k_Read = loom::ResourceAccess::Read;
    constexpr loom::ResourceAccess k_Write = loom::ResourceAccess::Write;

    void ShaderAndThreads(loom::DispatchBuilder& pass)
    {
        pass.Shader("Test", "MainCS");
        pass.DispatchThreads(8, 8, 1);
    }

    void BindData(loom::DispatchBuilder& pass)
    {
        ShaderAndThreads(pass);
        pass.Bind("Data", k_Data);
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

    // One access row per pass in declaration order, one entry per Bind call, as the Bind stage would give.
    std::optional<loom::OrderedGraph> FreezeAndOrder(const loom::GraphBuilder& graph,
                                                     loom::CollectingSink& sink,
                                                     std::vector<std::vector<loom::ResourceAccess>> accesses)
    {
        const std::optional<loom::FrozenGraph> frozen = loom::Freeze(graph.Record(), sink);
        EXPECT_TRUE(frozen.has_value()) << "Freeze failed";

        if (!frozen.has_value())
        {
            return std::nullopt;
        }

        const loom::BindAccessTable binds{ .Accesses = std::move(accesses) };
        return loom::Order(*frozen, binds, sink);
    }

    bool HasCode(const loom::CollectingSink& sink, loom::DiagnosticCode code)
    {
        const auto matches = [code](const loom::Diagnostic& diagnostic)
        {
            return diagnostic.Code == code;
        };

        return std::ranges::any_of(sink.Diagnostics(), matches);
    }

    // First reads Data before AnchorA. Second writes it between the anchors, and Third writes it after AnchorB. Two
    // writers give no data edges (Q3h), so the anchors alone order the three passes, and the read comes first.
    void BuildReadBeforeWrites(loom::GraphBuilder& graph, loom::RunPolicy second_policy)
    {
        graph.Declare(MakeBuffer());
        graph.DeclareAnchor(k_AnchorA);
        graph.DeclareAnchor(k_AnchorB);
        graph.OrderAnchors(k_AnchorA, k_AnchorB);

        const auto first = [](loom::DispatchBuilder& pass)
        {
            BindData(pass);
            pass.Before(k_AnchorA);
        };

        const auto second = [](loom::DispatchBuilder& pass)
        {
            BindData(pass);
            pass.After(k_AnchorA);
            pass.Before(k_AnchorB);
        };

        const auto third = [](loom::DispatchBuilder& pass)
        {
            BindData(pass);
            pass.After(k_AnchorB);
        };

        graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, first);
        graph.AddDispatch(k_Second, second_policy, k_Graphics, second);
        graph.AddDispatch(k_Third, k_EveryFrame, k_Graphics, third);
    }

}

// First writes Data and Second reads it, so Second is one level down. Third touches nothing: it shares level 0 with
// First. Kahn's heap alone gives 0, 1, 2 here; the level-major order is 0, 2, 1.
TEST(Levels, RunOrderIsLevelMajor)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(MakeBuffer());
    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, BindData);
    graph.AddDispatch(k_Second, k_EveryFrame, k_Graphics, BindData);
    graph.AddDispatch(k_Third, k_EveryFrame, k_Graphics, ShaderAndThreads);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered = FreezeAndOrder(graph, sink, { { k_Write }, { k_Read }, {} });

    ASSERT_TRUE(ordered.has_value()) << sink.Diagnostics().size() << " diagnostics";
    EXPECT_EQ(ordered->Levels, (std::vector<int32_t>{ 0, 1, 0 }));
    EXPECT_EQ(ordered->LevelCount, 2);
    EXPECT_EQ(PassOrder(*ordered), (std::vector<int32_t>{ 0, 2, 1 }));
}

// An anchor between two passes orders them but adds no level of its own: levels count passes, not nodes.
TEST(Levels, AnchorsAddNoLevel)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.DeclareAnchor(k_AnchorA);
    graph.DeclareAnchor(k_AnchorB);
    graph.OrderAnchors(k_AnchorA, k_AnchorB);

    const auto before = [](loom::DispatchBuilder& pass)
    {
        ShaderAndThreads(pass);
        pass.Before(k_AnchorA);
    };

    const auto after = [](loom::DispatchBuilder& pass)
    {
        ShaderAndThreads(pass);
        pass.After(k_AnchorB);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, after);
    graph.AddDispatch(k_Second, k_EveryFrame, k_Graphics, before);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered = FreezeAndOrder(graph, sink, { {}, {} });

    ASSERT_TRUE(ordered.has_value()) << sink.Diagnostics().size() << " diagnostics";
    EXPECT_EQ(ordered->Levels, (std::vector<int32_t>{ 1, 0 }));
    EXPECT_EQ(ordered->LevelCount, 2);
    EXPECT_EQ(PassOrder(*ordered), (std::vector<int32_t>{ 1, 0 }));
}

// A read before every write in the frame, with only EveryFrame writers: the contents do not exist yet (LM503).
TEST(Levels, ReadBeforeEveryFrameWritersIsLM503)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    BuildReadBeforeWrites(graph, k_EveryFrame);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered = FreezeAndOrder(graph, sink, { { k_Read }, { k_Write }, { k_Write } });

    EXPECT_FALSE(ordered.has_value());
    EXPECT_TRUE(HasCode(sink, loom::DiagnosticCode::LM503));
}

// The same graph with a Once writer: its contents persist from the frame it ran, so the early read is not LM503.
TEST(Levels, PersistentWriterIsASource)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    BuildReadBeforeWrites(graph, loom::RunPolicy::Once);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered = FreezeAndOrder(graph, sink, { { k_Read }, { k_Write }, { k_Write } });

    ASSERT_TRUE(ordered.has_value()) << sink.Diagnostics().size() << " diagnostics";
    EXPECT_FALSE(HasCode(sink, loom::DiagnosticCode::LM503));
}

// An imported buffer is a resource row of kind Buffer, origin Imported, that points into BufferImports.
TEST(Levels, BufferImportIsAnImportedBufferRow)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Import(loom::BufferImportDeclaration{ .Key = k_Supplied, .ByteSize = 256 });

    loom::CollectingSink sink;
    const std::optional<loom::FrozenGraph> frozen = loom::Freeze(graph.Record(), sink);

    ASSERT_TRUE(frozen.has_value()) << sink.Diagnostics().size() << " diagnostics";
    ASSERT_EQ(frozen->Resources.size(), 1u);
    EXPECT_EQ(frozen->Resources[0].Kind, loom::ResourceKind::Buffer);
    EXPECT_EQ(frozen->Resources[0].Origin, loom::ResourceOrigin::Imported);
    ASSERT_EQ(frozen->BufferImports.size(), 1u);
    EXPECT_EQ(frozen->BufferImports[frozen->Resources[0].DeclarationIndex].ByteSize, 256);
}

TEST(Levels, BufferImportWithNoSizeIsLM109)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Import(loom::BufferImportDeclaration{ .Key = k_Supplied });

    loom::CollectingSink sink;
    const std::optional<loom::FrozenGraph> frozen = loom::Freeze(graph.Record(), sink);

    EXPECT_FALSE(frozen.has_value());
    ASSERT_EQ(sink.Diagnostics().size(), 1u);
    EXPECT_EQ(sink.Diagnostics().front().Code, loom::DiagnosticCode::LM109);
}

// An imported buffer has its contents at frame start, so a pass may read it with no writer in the graph.
TEST(Levels, ImportedBufferNeedsNoWriter)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Import(loom::BufferImportDeclaration{ .Key = k_Supplied, .ByteSize = 256 });

    const auto reads = [](loom::DispatchBuilder& pass)
    {
        ShaderAndThreads(pass);
        pass.Bind("Supplied", k_Supplied);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, reads);

    loom::CollectingSink sink;
    const std::optional<loom::OrderedGraph> ordered = FreezeAndOrder(graph, sink, { { k_Read } });

    ASSERT_TRUE(ordered.has_value()) << sink.Diagnostics().size() << " diagnostics";
    EXPECT_FALSE(HasCode(sink, loom::DiagnosticCode::LM503));
}
