#include "Declarations.hpp"
#include "FrozenGraph.hpp"
#include "GraphBuilder.hpp"
#include "ImageDataFormats.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "LoomKeys.hpp"
#include "PassBuilders.hpp"
#include "PassStatements.hpp"
#include "Records.hpp"
#include "ResourceTypes.hpp"
#include "RhiFlags.hpp"
#include "SequenceBuilder.hpp"
#include "utility/delegate.hpp"
#include <cstdint>
#include <iterator>
#include <optional>
#include <source_location>
#include <gtest/gtest.h>

// Freeze's own tests go here: one broken graph per LM1xx code, each checking the code and the line of the call.

TEST(FrozenGraph, IndicesStartInvalid)
{
    const loom::FrozenPass pass{};

    EXPECT_FALSE(pass.Sequence.IsValid());
    EXPECT_FALSE(pass.Dispatch.Resource.IsValid());
    EXPECT_TRUE(loom::k_BeginFrameAnchorIndex.IsValid());
    EXPECT_NE(loom::k_BeginFrameAnchorIndex, loom::k_EndFrameAnchorIndex);
}

namespace
{

    constexpr loom::ResourceKey<loom::Buffer> k_Data{ "Test.Data" };
    constexpr loom::ResourceKey<loom::Image> k_DataAsImage{ "Test.Data" };

    loom::BufferDeclaration MakeBuffer()
    {
        return loom::BufferDeclaration
        {
            .Key = k_Data,
            .Lifetime = loom::ResourceLifetime::Frame,
            .Size = loom::SizeSource::Bytes(64),
            .Cpu = loom::CpuAccess::None,
            .HistoryDepth = 0,
            .NoHistoryAction = loom::OnNoHistory::Invalid
        };
    }

}

TEST(Freeze, BuiltinAnchorsComeFirst)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.DeclareAnchor(loom::AnchorKey{ "Test.Anchor" });

    loom::CollectingSink sink;
    const std::optional<loom::FrozenGraph> frozen = loom::Freeze(graph.Record(), sink);

    ASSERT_TRUE(frozen.has_value());
    ASSERT_EQ(frozen->Anchors.size(), 3u);
    EXPECT_EQ(frozen->Anchors[loom::k_BeginFrameAnchorIndex.Value].Key.Hash, loom::BeginFrameAnchor.Hash);
    EXPECT_EQ(frozen->Anchors[loom::k_EndFrameAnchorIndex.Value].Key.Hash, loom::EndFrameAnchor.Hash);
    EXPECT_EQ(frozen->Anchors[2].Origin, loom::AnchorOrigin::Declared);
}

TEST(Freeze, SameKeyTwiceIsLM102WithBothLines)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };

    const uint32_t firstLine = std::source_location::current().line() + 1;
    graph.Declare(MakeBuffer());
    const uint32_t secondLine = std::source_location::current().line() + 1;
    graph.Declare(MakeBuffer());

    loom::CollectingSink sink;
    const std::optional<loom::FrozenGraph> frozen = loom::Freeze(graph.Record(), sink);

    ASSERT_FALSE(frozen.has_value());
    ASSERT_EQ(sink.Diagnostics().size(), 1u);
    EXPECT_EQ(sink.Diagnostics()[0].Code, loom::DiagnosticCode::LM102);
    EXPECT_EQ(sink.Diagnostics()[0].Where.line(), secondLine);
    EXPECT_EQ(sink.Diagnostics()[0].OtherWhere.line(), firstLine);
}

TEST(Freeze, SameKeyAsTwoKindsIsLM107)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(MakeBuffer());
    const ImageFormat format{ ImageComponentFormats::R8, ImageDataType::UNorm };
    graph.Import(loom::ImportDeclaration{ .Key = k_DataAsImage, .Expectation = loom::ImageExpectation::AnyExtent(format, 1) });

    loom::CollectingSink sink;
    const std::optional<loom::FrozenGraph> frozen = loom::Freeze(graph.Record(), sink);

    ASSERT_FALSE(frozen.has_value());
    ASSERT_EQ(sink.Diagnostics().size(), 1u);
    EXPECT_EQ(sink.Diagnostics()[0].Code, loom::DiagnosticCode::LM107);
}

namespace
{

    constexpr loom::ResourceKey<loom::Sampler> k_Sampler{ "Test.Sampler" };

    std::optional<loom::FrozenGraph> FreezeOneSampler(const SamplerCreateInfo& info, uint32_t& line,
                                                      loom::CollectingSink& sink)
    {
        loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };

        line = std::source_location::current().line() + 1;
        graph.Declare(loom::SamplerDeclaration{ .Key = k_Sampler, .CreateInfo = info });

        return loom::Freeze(graph.Record(), sink);
    }

}

TEST(Freeze, DefaultSamplerIsValid)
{
    loom::CollectingSink sink;
    uint32_t line = 0;
    const std::optional<loom::FrozenGraph> frozen = FreezeOneSampler(SamplerCreateInfo{}, line, sink);

    ASSERT_TRUE(frozen.has_value());
    EXPECT_EQ(sink.Diagnostics().size(), 0u);
    ASSERT_EQ(frozen->Samplers.size(), 1u);
}

TEST(Freeze, SamplerMaxLodBelowMinLodIsLM109AsReal)
{
    SamplerCreateInfo info{};
    info.MinLod = 4.0f;
    info.MaxLod = 2.5f;

    loom::CollectingSink sink;
    uint32_t line = 0;
    const std::optional<loom::FrozenGraph> frozen = FreezeOneSampler(info, line, sink);

    ASSERT_FALSE(frozen.has_value());
    ASSERT_EQ(sink.Diagnostics().size(), 1u);
    EXPECT_EQ(sink.Diagnostics()[0].Code, loom::DiagnosticCode::LM109);
    EXPECT_EQ(sink.Diagnostics()[0].Actual.FieldName, "MaxLod");
    EXPECT_EQ(sink.Diagnostics()[0].Actual.Kind, loom::DiagnosticValueKind::Real);
    EXPECT_EQ(sink.Diagnostics()[0].Actual.Real, 2.5);
    EXPECT_EQ(sink.Diagnostics()[0].Where.line(), line);
}

TEST(Freeze, BorderAddressWithNoBorderColorIsLM101)
{
    SamplerCreateInfo info{};
    info.AddressV = SamplerCreateInfo::AddressMode::ClampToBorder;

    loom::CollectingSink sink;
    uint32_t line = 0;
    const std::optional<loom::FrozenGraph> frozen = FreezeOneSampler(info, line, sink);

    ASSERT_FALSE(frozen.has_value());
    ASSERT_EQ(sink.Diagnostics().size(), 1u);
    EXPECT_EQ(sink.Diagnostics()[0].Code, loom::DiagnosticCode::LM101);
    EXPECT_EQ(sink.Diagnostics()[0].Actual.FieldName, "BorderColor");
}

// The defaults break three of the unnormalized rules: Linear mips, MaxLod 1e10, and Repeat on U and V.
TEST(Freeze, UnnormalizedSamplerReportsEachBrokenRule)
{
    SamplerCreateInfo info{};
    info.UnnormalizedCoordinates = true;

    loom::CollectingSink sink;
    uint32_t line = 0;
    const std::optional<loom::FrozenGraph> frozen = FreezeOneSampler(info, line, sink);

    ASSERT_FALSE(frozen.has_value());
    ASSERT_EQ(sink.Diagnostics().size(), 4u);
    EXPECT_EQ(sink.Diagnostics()[0].Actual.FieldName, "MipFilter");
    EXPECT_EQ(sink.Diagnostics()[1].Actual.FieldName, "MaxLod");
    EXPECT_EQ(sink.Diagnostics()[2].Actual.FieldName, "AddressU");
    EXPECT_EQ(sink.Diagnostics()[3].Actual.FieldName, "AddressV");
}

// Resolve: each test builds a graph with exactly one broken name, so the sink holds exactly one diagnostic.

namespace
{

    constexpr loom::ResourceKey<loom::Buffer> k_Unpublished{ "Test.Unpublished" };
    constexpr loom::ResourceKey<loom::Image> k_Albedo{ "Test.Albedo" };
    constexpr loom::FrameTargetKey k_Backbuffer{ "Test.Backbuffer" };
    constexpr loom::FrameTargetKey k_UnknownTarget{ "Test.UnknownTarget" };
    constexpr loom::AnchorKey k_Lit{ "Test.Lit" };
    constexpr loom::AnchorKey k_Undeclared{ "Test.Undeclared" };
    constexpr loom::PassKey k_First{ "Test.First" };
    constexpr loom::PassKey k_Second{ "Test.Second" };
    constexpr loom::PassKey k_Third{ "Test.Third" };
    constexpr loom::SequenceKey k_Post{ "Test.Post" };

    constexpr loom::RunPolicy k_EveryFrame = loom::RunPolicy::EveryFrame;
    constexpr loom::QueueAffinity k_Graphics = loom::QueueAffinity::Graphics;

    loom::ImageDeclaration MakeImageOfTarget(loom::FrameTargetKey target)
    {
        return loom::ImageDeclaration
        {
            .Key = k_Albedo,
            .Lifetime = loom::ResourceLifetime::Frame,
            .Format = ImageFormat{ ImageComponentFormats::R8, ImageDataType::UNorm },
            .Extent = loom::ExtentSource::OfTarget(target, 0.5f),
            .Mips = loom::MipSource::One(),
            .Layers = 1,
            .Cpu = loom::CpuAccess::None,
            .HistoryDepth = 0,
            .NoHistoryAction = loom::OnNoHistory::Invalid
        };
    }

    // The two statements every pass needs, so a test breaks only the part it is about.
    void ShaderAndThreads(loom::DispatchBuilder& pass)
    {
        pass.Shader("Test", "MainCS");
        pass.DispatchThreads(8, 8, 1);
    }

    const loom::Diagnostic& OnlyDiagnostic(const loom::CollectingSink& sink)
    {
        EXPECT_EQ(sink.Diagnostics().size(), 1u);
        return sink.Diagnostics().front();
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

// Draw and Record passes: the same pattern, one broken statement per test.

namespace
{

    constexpr loom::ResourceKey<loom::Image> k_SceneDepth{ "Test.SceneDepth" };
    constexpr loom::ResourceKey<loom::Buffer> k_Indices{ "Test.Indices" };

    loom::ImageDeclaration MakeDepthImage()
    {
        loom::ImageDeclaration depth = MakeImageOfTarget(k_Backbuffer);
        depth.Key = k_SceneDepth;
        depth.Format = ImageFormat{ ImageComponentFormats::D32, ImageDataType::Float };
        return depth;
    }

    loom::BufferDeclaration MakeIndexBuffer()
    {
        loom::BufferDeclaration indices = MakeBuffer();
        indices.Key = k_Indices;
        return indices;
    }

    // The resources a draw test can name, each published once.
    void DeclareDrawResources(loom::GraphBuilder& graph)
    {
        graph.Import(k_Backbuffer);
        graph.Declare(MakeBuffer());
        graph.Declare(MakeIndexBuffer());
        graph.Declare(MakeImageOfTarget(k_Backbuffer));
        graph.Declare(MakeDepthImage());
    }

    // The statements every draw needs: one shader, one color target, one draw.
    void ShadersColorAndDraw(loom::DrawBuilder& pass)
    {
        pass.Shaders("Test", "MainVS", "MainPS");
        pass.Color(0, k_Albedo, loom::ColorLoad::DontCare(), loom::AttachmentStore::Store, loom::BlendMode::Opaque);
        pass.Draw(3, 1, 0, 0);
    }

    void RecordNothing(loom::RecordContext&)
    {
    }

}

// Every name a draw can hold resolves to the row it names.
TEST(Draw, ValidDrawResolvesEveryTarget)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    DeclareDrawResources(graph);

    const loom::RasterState raster{ .Topology = loom::PrimitiveTopology::TriangleStrip, .Polygon = loom::PolygonMode::Fill,
                                    .Cull = rhi::CullMode::Back, .Front = rhi::FrontFace::Clockwise };

    auto build = [&raster](loom::DrawBuilder& pass)
    {
        pass.Shaders("Test", "MainVS", "MainPS");
        pass.Color(0, k_Albedo, loom::ColorLoad::Clear(0.0f, 0.0f, 0.0f, 1.0f), loom::AttachmentStore::Store, loom::BlendMode::Opaque);
        pass.Color(1, k_Backbuffer, loom::ColorLoad::Keep(), loom::AttachmentStore::Store, loom::BlendMode::AlphaBlend);
        pass.Depth(k_SceneDepth, loom::DepthLoad::Clear(1.0f), loom::AttachmentStore::DontCare, rhi::CompareOp::Less,
                   loom::DepthWrite::Enabled);
        pass.Raster(raster);
        pass.VertexBuffer(0, k_Data);
        pass.IndexBuffer(k_Indices, loom::IndexType::UInt32);
        pass.DrawIndexed(36, 1, 0, 0, 0);
    };

    graph.AddDraw(k_First, k_EveryFrame, k_Graphics, build);

    loom::CollectingSink sink;
    const std::optional<loom::FrozenGraph> frozen = loom::Freeze(graph.Record(), sink);

    ASSERT_TRUE(frozen.has_value()) << sink.Diagnostics().size() << " diagnostics";
    ASSERT_EQ(frozen->Passes.size(), 1u);

    const loom::FrozenPass& pass = frozen->Passes[0];
    EXPECT_EQ(pass.Kind, loom::PassKind::Draw);
    EXPECT_EQ(pass.Shader.FragmentEntryPoint, "MainPS");
    EXPECT_EQ(pass.Draw.Form, loom::DrawForm::DrawIndexed);
    EXPECT_EQ(pass.Draw.VertexOrIndexCount, 36);

    ASSERT_EQ(pass.Colors.size(), 2u);
    EXPECT_EQ(frozen->Resources[pass.Colors[0].Image.Value].Key.Hash, k_Albedo.Hash);
    EXPECT_FALSE(pass.Colors[0].FrameTarget.IsValid());
    EXPECT_EQ(frozen->FrameTargets[pass.Colors[1].FrameTarget.Value].Key.Hash, k_Backbuffer.Hash);
    EXPECT_FALSE(pass.Colors[1].Image.IsValid());

    EXPECT_EQ(frozen->Resources[pass.Depth.Image.Value].Key.Hash, k_SceneDepth.Hash);
    EXPECT_EQ(pass.Depth.Write, loom::DepthWrite::Enabled);
    EXPECT_EQ(pass.Raster.Topology, loom::PrimitiveTopology::TriangleStrip);
    EXPECT_EQ(pass.Raster.Cull, rhi::CullMode::Back);

    ASSERT_EQ(pass.VertexBuffers.size(), 1u);
    EXPECT_EQ(frozen->Resources[pass.VertexBuffers[0].Buffer.Value].Key.Hash, k_Data.Hash);
    EXPECT_EQ(frozen->Resources[pass.Index.Buffer.Value].Key.Hash, k_Indices.Hash);
    EXPECT_EQ(pass.Index.Type, loom::IndexType::UInt32);
}

// A depth prepass: no fragment shader, no color target, and the default raster state.
TEST(Draw, DepthOnlyDrawIsValid)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    DeclareDrawResources(graph);

    auto build = [](loom::DrawBuilder& pass)
    {
        pass.Shaders("Test", "DepthVS", "");
        pass.Depth(k_SceneDepth, loom::DepthLoad::Clear(1.0f), loom::AttachmentStore::Store, rhi::CompareOp::Less,
                   loom::DepthWrite::Enabled);
        pass.Draw(3, 1, 0, 0);
    };

    graph.AddDraw(k_First, k_EveryFrame, k_Graphics, build);

    loom::CollectingSink sink;
    const std::optional<loom::FrozenGraph> frozen = loom::Freeze(graph.Record(), sink);

    ASSERT_TRUE(frozen.has_value()) << sink.Diagnostics().size() << " diagnostics";
    EXPECT_TRUE(frozen->Passes[0].Colors.empty());
    EXPECT_EQ(frozen->Passes[0].Raster.Topology, loom::PrimitiveTopology::TriangleList);
    EXPECT_FALSE(frozen->Passes[0].Index.Buffer.IsValid());
}

TEST(Draw, DrawWithNoTargetIsLM101)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };

    auto build = [](loom::DrawBuilder& pass)
    {
        pass.Shaders("Test", "MainVS", "MainPS");
        pass.Draw(3, 1, 0, 0);
    };

    const uint32_t passLine = std::source_location::current().line() + 1;
    graph.AddDraw(k_First, k_EveryFrame, k_Graphics, build);

    loom::CollectingSink sink;
    const std::optional<loom::FrozenGraph> frozen = loom::Freeze(graph.Record(), sink);

    ASSERT_FALSE(frozen.has_value());
    const loom::Diagnostic& diag = OnlyDiagnostic(sink);
    EXPECT_EQ(diag.Code, loom::DiagnosticCode::LM101);
    EXPECT_EQ(diag.Actual.FieldName, "Color");
    EXPECT_EQ(diag.Where.line(), passLine);
}

TEST(Draw, DrawWithNoDrawStatementIsLM110)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    DeclareDrawResources(graph);

    auto build = [](loom::DrawBuilder& pass)
    {
        pass.Shaders("Test", "MainVS", "MainPS");
        pass.Color(0, k_Albedo, loom::ColorLoad::DontCare(), loom::AttachmentStore::Store, loom::BlendMode::Opaque);
    };

    graph.AddDraw(k_First, k_EveryFrame, k_Graphics, build);

    loom::CollectingSink sink;
    const std::optional<loom::FrozenGraph> frozen = loom::Freeze(graph.Record(), sink);

    ASSERT_FALSE(frozen.has_value());
    EXPECT_EQ(OnlyDiagnostic(sink).Code, loom::DiagnosticCode::LM110);
}

// Where is the second color in the slot, OtherWhere the first.
TEST(Draw, TwoColorsInOneSlotIsLM113WithBothLines)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    DeclareDrawResources(graph);
    uint32_t secondLine = 0;
    uint32_t firstLine = 0;

    auto build = [&firstLine, &secondLine](loom::DrawBuilder& pass)
    {
        pass.Shaders("Test", "MainVS", "MainPS");
        firstLine = std::source_location::current().line() + 1;
        pass.Color(0, k_Albedo, loom::ColorLoad::DontCare(), loom::AttachmentStore::Store, loom::BlendMode::Opaque);
        secondLine = std::source_location::current().line() + 1;
        pass.Color(0, k_Backbuffer, loom::ColorLoad::DontCare(), loom::AttachmentStore::Store, loom::BlendMode::Opaque);
        pass.Draw(3, 1, 0, 0);
    };

    graph.AddDraw(k_First, k_EveryFrame, k_Graphics, build);

    loom::CollectingSink sink;
    const std::optional<loom::FrozenGraph> frozen = loom::Freeze(graph.Record(), sink);

    ASSERT_FALSE(frozen.has_value());
    const loom::Diagnostic& diag = OnlyDiagnostic(sink);
    EXPECT_EQ(diag.Code, loom::DiagnosticCode::LM113);
    EXPECT_EQ(diag.Where.line(), secondLine);
    EXPECT_EQ(diag.OtherWhere.line(), firstLine);
}

TEST(Draw, TwoDepthTargetsIsLM113)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    DeclareDrawResources(graph);

    auto build = [](loom::DrawBuilder& pass)
    {
        ShadersColorAndDraw(pass);
        pass.Depth(k_SceneDepth, loom::DepthLoad::Keep(), loom::AttachmentStore::Store, rhi::CompareOp::Less,
                   loom::DepthWrite::Enabled);
        pass.Depth(k_SceneDepth, loom::DepthLoad::Keep(), loom::AttachmentStore::Store, rhi::CompareOp::Less,
                   loom::DepthWrite::Disabled);
    };

    graph.AddDraw(k_First, k_EveryFrame, k_Graphics, build);

    loom::CollectingSink sink;
    const std::optional<loom::FrozenGraph> frozen = loom::Freeze(graph.Record(), sink);

    ASSERT_FALSE(frozen.has_value());
    EXPECT_EQ(OnlyDiagnostic(sink).Code, loom::DiagnosticCode::LM113);
}

TEST(Draw, DrawIndexedWithNoIndexBufferIsLM101AtTheDraw)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    DeclareDrawResources(graph);
    uint32_t drawLine = 0;

    auto build = [&drawLine](loom::DrawBuilder& pass)
    {
        pass.Shaders("Test", "MainVS", "MainPS");
        pass.Color(0, k_Albedo, loom::ColorLoad::DontCare(), loom::AttachmentStore::Store, loom::BlendMode::Opaque);
        drawLine = std::source_location::current().line() + 1;
        pass.DrawIndexed(36, 1, 0, 0, 0);
    };

    graph.AddDraw(k_First, k_EveryFrame, k_Graphics, build);

    loom::CollectingSink sink;
    const std::optional<loom::FrozenGraph> frozen = loom::Freeze(graph.Record(), sink);

    ASSERT_FALSE(frozen.has_value());
    const loom::Diagnostic& diag = OnlyDiagnostic(sink);
    EXPECT_EQ(diag.Code, loom::DiagnosticCode::LM101);
    EXPECT_EQ(diag.Actual.FieldName, "IndexBuffer");
    EXPECT_EQ(diag.Where.line(), drawLine);
}

TEST(Draw, DepthClearAboveOneIsLM109AsReal)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    DeclareDrawResources(graph);

    auto build = [](loom::DrawBuilder& pass)
    {
        ShadersColorAndDraw(pass);
        pass.Depth(k_SceneDepth, loom::DepthLoad::Clear(2.0f), loom::AttachmentStore::Store, rhi::CompareOp::Less,
                   loom::DepthWrite::Enabled);
    };

    graph.AddDraw(k_First, k_EveryFrame, k_Graphics, build);

    loom::CollectingSink sink;
    const std::optional<loom::FrozenGraph> frozen = loom::Freeze(graph.Record(), sink);

    ASSERT_FALSE(frozen.has_value());
    const loom::Diagnostic& diag = OnlyDiagnostic(sink);
    EXPECT_EQ(diag.Code, loom::DiagnosticCode::LM109);
    EXPECT_EQ(diag.Actual.FieldName, "ClearDepth");
    EXPECT_EQ(diag.Actual.Real, 2.0);
}

// A color target names an image. A buffer key in that place is a kind clash, not a missing key.
TEST(Draw, ColorTargetOnBufferKeyIsLM107)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    DeclareDrawResources(graph);

    auto build = [](loom::DrawBuilder& pass)
    {
        pass.Shaders("Test", "MainVS", "MainPS");
        pass.Color(0, k_DataAsImage, loom::ColorLoad::DontCare(), loom::AttachmentStore::Store, loom::BlendMode::Opaque);
        pass.Draw(3, 1, 0, 0);
    };

    graph.AddDraw(k_First, k_EveryFrame, k_Graphics, build);

    loom::CollectingSink sink;
    const std::optional<loom::FrozenGraph> frozen = loom::Freeze(graph.Record(), sink);

    ASSERT_FALSE(frozen.has_value());
    EXPECT_EQ(OnlyDiagnostic(sink).Code, loom::DiagnosticCode::LM107);
}

// A Record pass's uses resolve like binds, and a frame target use resolves into the frame target table.
TEST(Record, ValidRecordResolvesUses)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    DeclareDrawResources(graph);

    auto build = [](loom::RecordBuilder& pass)
    {
        pass.Uses(k_Albedo, loom::ResourceAccess::Read, loom::ImageUse::Sampled);
        pass.Uses(k_Backbuffer, loom::ResourceAccess::ReadWrite, loom::ImageUse::ColorTarget);
        pass.Uses(k_Data, loom::ResourceAccess::Read, loom::BufferUse::Vertex);
        pass.Record(Delegate<void(loom::RecordContext&)>::Create<&RecordNothing>());
    };

    graph.AddRecord(k_First, k_EveryFrame, k_Graphics, build);

    loom::CollectingSink sink;
    const std::optional<loom::FrozenGraph> frozen = loom::Freeze(graph.Record(), sink);

    ASSERT_TRUE(frozen.has_value()) << sink.Diagnostics().size() << " diagnostics";

    const loom::FrozenPass& pass = frozen->Passes[0];
    EXPECT_EQ(pass.Kind, loom::PassKind::Record);
    EXPECT_FALSE(pass.Recorder.Empty());
    ASSERT_EQ(pass.Uses.size(), 3u);
    EXPECT_EQ(frozen->Resources[pass.Uses[0].Resource.Value].Key.Hash, k_Albedo.Hash);
    EXPECT_EQ(frozen->FrameTargets[pass.Uses[1].FrameTarget.Value].Key.Hash, k_Backbuffer.Hash);
    EXPECT_FALSE(pass.Uses[1].Resource.IsValid());
    EXPECT_EQ(frozen->Resources[pass.Uses[2].Resource.Value].Key.Hash, k_Data.Hash);
}

TEST(Record, RecordWithNoRecorderIsLM105)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    DeclareDrawResources(graph);

    auto build = [](loom::RecordBuilder& pass)
    {
        pass.Uses(k_Albedo, loom::ResourceAccess::Read, loom::ImageUse::Sampled);
    };

    const uint32_t passLine = std::source_location::current().line() + 1;
    graph.AddRecord(k_First, k_EveryFrame, k_Graphics, build);

    loom::CollectingSink sink;
    const std::optional<loom::FrozenGraph> frozen = loom::Freeze(graph.Record(), sink);

    ASSERT_FALSE(frozen.has_value());
    const loom::Diagnostic& diag = OnlyDiagnostic(sink);
    EXPECT_EQ(diag.Code, loom::DiagnosticCode::LM105);
    EXPECT_EQ(diag.Where.line(), passLine);
}

TEST(Record, UseWithInvalidAccessIsLM101)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    DeclareDrawResources(graph);

    auto build = [](loom::RecordBuilder& pass)
    {
        pass.Uses(k_Albedo, loom::ResourceAccess::Invalid, loom::ImageUse::Sampled);
        pass.Record(Delegate<void(loom::RecordContext&)>::Create<&RecordNothing>());
    };

    graph.AddRecord(k_First, k_EveryFrame, k_Graphics, build);

    loom::CollectingSink sink;
    const std::optional<loom::FrozenGraph> frozen = loom::Freeze(graph.Record(), sink);

    ASSERT_FALSE(frozen.has_value());
    const loom::Diagnostic& diag = OnlyDiagnostic(sink);
    EXPECT_EQ(diag.Code, loom::DiagnosticCode::LM101);
    EXPECT_EQ(diag.Actual.FieldName, "Access");
}
