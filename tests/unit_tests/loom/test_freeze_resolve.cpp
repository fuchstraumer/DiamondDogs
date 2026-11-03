#include "freeze/FrozenGraph.hpp"
#include "GraphBuilder.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "LoomKeys.hpp"
#include "PassBuilders.hpp"
#include "SequenceBuilder.hpp"
#include "freeze_test_helpers.hpp"
#include <cstdint>
#include <iterator>
#include <optional>
#include <source_location>
#include <gtest/gtest.h>

// Resolve, Freeze's name-resolution step, for graphs and dispatch passes. Each error test builds a graph with exactly
// one broken name, so the sink holds exactly one diagnostic.

using namespace freeze_tests;

namespace
{

    constexpr loom::ResourceKey<loom::Buffer> k_Unpublished{ "Test.Unpublished" };
    constexpr loom::FrameTargetKey k_UnknownTarget{ "Test.UnknownTarget" };
    constexpr loom::AnchorKey k_Lit{ "Test.Lit" };
    constexpr loom::AnchorKey k_Undeclared{ "Test.Undeclared" };
    constexpr loom::PassKey k_Second{ "Test.Second" };
    constexpr loom::PassKey k_Third{ "Test.Third" };
    constexpr loom::SequenceKey k_Post{ "Test.Post" };

    // The two statements every dispatch needs, so a test breaks only the part it is about.
    void ShaderAndThreads(loom::DispatchBuilder& pass)
    {
        pass.Shader("Test", "MainCS");
        pass.DispatchThreads(8, 8, 1);
    }

}

// One graph that uses every kind of name Resolve handles. Each index must point at the row it names.
TEST(Resolve, ValidGraphResolvesEveryName)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(MakeBuffer());
    graph.Import(k_Backbuffer);
    graph.Declare(MakeImageOfTarget(k_Backbuffer));
    graph.Output(loom::OutputDeclaration{ .Target = k_Backbuffer, .OnSkip = loom::OutputFallback::Clear(0.0f, 0.0f, 0.0f, 1.0f) });
    graph.DeclareAnchor(k_Lit);
    graph.OrderAnchors(loom::BeginFrameAnchor, k_Lit);

    auto buildFirst = [](loom::DispatchBuilder& pass)
    {
        pass.Shader("Test", "MainCS");
        pass.After(k_Lit);
        pass.Bind("Data", k_Data);
        pass.DispatchThreadsOver(k_Albedo);
    };

    auto buildPost = [](loom::SequenceBuilder& sequence)
    {
        sequence.Before(loom::EndFrameAnchor);
        sequence.AddDispatch(k_Second, k_EveryFrame, k_Graphics, ShaderAndThreads);
        sequence.AddDispatch(k_Third, k_EveryFrame, k_Graphics, ShaderAndThreads);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, buildFirst);
    graph.AddSequence(k_Post, buildPost);

    loom::CollectingSink sink;
    const std::optional<loom::FrozenGraph> frozen = loom::Freeze(graph.Record(), sink);

    ASSERT_TRUE(frozen.has_value()) << sink.Diagnostics().size() << " diagnostics";
    ASSERT_EQ(frozen->Passes.size(), 3u);

    // The index of a pass is its declaration order.
    for (int32_t index = 0; index < std::ssize(frozen->Passes); ++index)
    {
        EXPECT_EQ(frozen->Passes[index].DeclarationOrder, index);
    }

    const int32_t litAnchor = 2;    // after the two built-in anchors
    const loom::FrozenPass& first = frozen->Passes[0];
    ASSERT_EQ(first.After.size(), 1u);
    EXPECT_EQ(first.After[0].Anchor.Value, litAnchor);
    ASSERT_EQ(first.Binds.size(), 1u);
    EXPECT_EQ(frozen->Resources[first.Binds[0].Resource.Value].Key.Hash, k_Data.Hash);
    EXPECT_EQ(frozen->Resources[first.Dispatch.Resource.Value].Key.Hash, k_Albedo.Hash);

    ASSERT_EQ(frozen->Sequences.size(), 1u);
    const loom::FrozenSequence& post = frozen->Sequences[0];
    EXPECT_EQ(post.FirstPass.Value, 1);
    EXPECT_EQ(post.PassCount, 2);
    ASSERT_EQ(post.Before.size(), 1u);
    EXPECT_EQ(post.Before[0].Anchor, loom::k_EndFrameAnchorIndex);

    ASSERT_EQ(frozen->Images.size(), 1u);
    ASSERT_EQ(frozen->Outputs.size(), 1u);
    EXPECT_EQ(frozen->FrameTargets[frozen->Images[0].ExtentTarget.Value].Key.Hash, k_Backbuffer.Hash);
    EXPECT_EQ(frozen->Outputs[0].Target, frozen->Images[0].ExtentTarget);

    ASSERT_EQ(frozen->AnchorOrders.size(), 1u);
    EXPECT_EQ(frozen->AnchorOrders[0].Earlier, loom::k_BeginFrameAnchorIndex);
    EXPECT_EQ(frozen->AnchorOrders[0].Later.Value, litAnchor);
}

TEST(Resolve, BindOfUnpublishedKeyIsLM103AtTheBind)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    uint32_t bindLine = 0;

    auto build = [&bindLine](loom::DispatchBuilder& pass)
    {
        ShaderAndThreads(pass);
        bindLine = std::source_location::current().line() + 1;
        pass.Bind("Data", k_Unpublished);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, build);

    loom::CollectingSink sink;
    const std::optional<loom::FrozenGraph> frozen = loom::Freeze(graph.Record(), sink);

    ASSERT_FALSE(frozen.has_value());
    const loom::Diagnostic& diag = OnlyDiagnostic(sink);
    EXPECT_EQ(diag.Code, loom::DiagnosticCode::LM103);
    EXPECT_EQ(diag.Resource.Hash, k_Unpublished.Hash);
    EXPECT_EQ(diag.Where.line(), bindLine);
}

TEST(Resolve, UndeclaredAnchorIsLM104)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    uint32_t afterLine = 0;

    auto build = [&afterLine](loom::DispatchBuilder& pass)
    {
        ShaderAndThreads(pass);
        afterLine = std::source_location::current().line() + 1;
        pass.After(k_Undeclared);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, build);

    loom::CollectingSink sink;
    const std::optional<loom::FrozenGraph> frozen = loom::Freeze(graph.Record(), sink);

    ASSERT_FALSE(frozen.has_value());
    const loom::Diagnostic& diag = OnlyDiagnostic(sink);
    EXPECT_EQ(diag.Code, loom::DiagnosticCode::LM104);
    EXPECT_EQ(diag.Where.line(), afterLine);
}

// The key is published as a buffer and used as an image. OtherWhere points at the publisher.
TEST(Resolve, ImageUseOfBufferKeyIsLM107WithBothLines)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };

    const uint32_t declareLine = std::source_location::current().line() + 1;
    graph.Declare(MakeBuffer());

    uint32_t useLine = 0;

    auto build = [&useLine](loom::DispatchBuilder& pass)
    {
        pass.Shader("Test", "MainCS");
        useLine = std::source_location::current().line() + 1;
        pass.DispatchThreadsOver(k_DataAsImage);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, build);

    loom::CollectingSink sink;
    const std::optional<loom::FrozenGraph> frozen = loom::Freeze(graph.Record(), sink);

    ASSERT_FALSE(frozen.has_value());
    const loom::Diagnostic& diag = OnlyDiagnostic(sink);
    EXPECT_EQ(diag.Code, loom::DiagnosticCode::LM107);
    EXPECT_EQ(diag.Where.line(), useLine);
    EXPECT_EQ(diag.OtherWhere.line(), declareLine);
}

TEST(Resolve, PassWithNoShaderIsLM105)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };

    auto build = [](loom::DispatchBuilder& pass)
    {
        pass.DispatchThreads(8, 8, 1);
    };

    const uint32_t passLine = std::source_location::current().line() + 1;
    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, build);

    loom::CollectingSink sink;
    const std::optional<loom::FrozenGraph> frozen = loom::Freeze(graph.Record(), sink);

    ASSERT_FALSE(frozen.has_value());
    const loom::Diagnostic& diag = OnlyDiagnostic(sink);
    EXPECT_EQ(diag.Code, loom::DiagnosticCode::LM105);
    EXPECT_EQ(diag.Where.line(), passLine);
}

TEST(Resolve, PassWithTwoShadersIsLM105)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };

    auto build = [](loom::DispatchBuilder& pass)
    {
        ShaderAndThreads(pass);
        pass.Shader("Test", "OtherCS");
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, build);

    loom::CollectingSink sink;
    const std::optional<loom::FrozenGraph> frozen = loom::Freeze(graph.Record(), sink);

    ASSERT_FALSE(frozen.has_value());
    EXPECT_EQ(OnlyDiagnostic(sink).Code, loom::DiagnosticCode::LM105);
}

// Where is the first dispatch, OtherWhere the second, so both calls can be found.
TEST(Resolve, TwoDispatchesIsLM106WithBothLines)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    uint32_t firstLine = 0;
    uint32_t secondLine = 0;

    auto build = [&firstLine, &secondLine](loom::DispatchBuilder& pass)
    {
        pass.Shader("Test", "MainCS");
        firstLine = std::source_location::current().line() + 1;
        pass.DispatchThreads(8, 8, 1);
        secondLine = std::source_location::current().line() + 1;
        pass.DispatchThreads(4, 4, 1);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, build);

    loom::CollectingSink sink;
    const std::optional<loom::FrozenGraph> frozen = loom::Freeze(graph.Record(), sink);

    ASSERT_FALSE(frozen.has_value());
    const loom::Diagnostic& diag = OnlyDiagnostic(sink);
    EXPECT_EQ(diag.Code, loom::DiagnosticCode::LM106);
    EXPECT_EQ(diag.Where.line(), firstLine);
    EXPECT_EQ(diag.OtherWhere.line(), secondLine);
}

TEST(Resolve, PassWithNoDispatchIsLM110)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };

    auto build = [](loom::DispatchBuilder& pass)
    {
        pass.Shader("Test", "MainCS");
    };

    const uint32_t passLine = std::source_location::current().line() + 1;
    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, build);

    loom::CollectingSink sink;
    const std::optional<loom::FrozenGraph> frozen = loom::Freeze(graph.Record(), sink);

    ASSERT_FALSE(frozen.has_value());
    const loom::Diagnostic& diag = OnlyDiagnostic(sink);
    EXPECT_EQ(diag.Code, loom::DiagnosticCode::LM110);
    EXPECT_EQ(diag.Where.line(), passLine);
}

TEST(Resolve, EmptySequenceIsLM111)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };

    auto build = [](loom::SequenceBuilder& sequence)
    {
        sequence.After(loom::BeginFrameAnchor);
    };

    const uint32_t sequenceLine = std::source_location::current().line() + 1;
    graph.AddSequence(k_Post, build);

    loom::CollectingSink sink;
    const std::optional<loom::FrozenGraph> frozen = loom::Freeze(graph.Record(), sink);

    ASSERT_FALSE(frozen.has_value());
    const loom::Diagnostic& diag = OnlyDiagnostic(sink);
    EXPECT_EQ(diag.Code, loom::DiagnosticCode::LM111);
    EXPECT_EQ(diag.Where.line(), sequenceLine);
}

// Where is the second output, OtherWhere the first one, which claimed the target.
TEST(Resolve, TwoOutputsOnOneTargetIsLM112WithBothLines)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Import(k_Backbuffer);

    const loom::OutputDeclaration output{ .Target = k_Backbuffer, .OnSkip = loom::OutputFallback::KeepLast() };
    const uint32_t firstLine = std::source_location::current().line() + 1;
    graph.Output(output);
    const uint32_t secondLine = std::source_location::current().line() + 1;
    graph.Output(output);

    loom::CollectingSink sink;
    const std::optional<loom::FrozenGraph> frozen = loom::Freeze(graph.Record(), sink);

    ASSERT_FALSE(frozen.has_value());
    const loom::Diagnostic& diag = OnlyDiagnostic(sink);
    EXPECT_EQ(diag.Code, loom::DiagnosticCode::LM112);
    EXPECT_EQ(diag.Where.line(), secondLine);
    EXPECT_EQ(diag.OtherWhere.line(), firstLine);
}

TEST(Resolve, ImageExtentOfUnknownTargetIsLM103AtTheImage)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };

    const uint32_t imageLine = std::source_location::current().line() + 1;
    graph.Declare(MakeImageOfTarget(k_UnknownTarget));

    loom::CollectingSink sink;
    const std::optional<loom::FrozenGraph> frozen = loom::Freeze(graph.Record(), sink);

    ASSERT_FALSE(frozen.has_value());
    const loom::Diagnostic& diag = OnlyDiagnostic(sink);
    EXPECT_EQ(diag.Code, loom::DiagnosticCode::LM103);
    EXPECT_EQ(diag.Resource.Hash, k_UnknownTarget.Hash);
    EXPECT_EQ(diag.Where.line(), imageLine);
}
