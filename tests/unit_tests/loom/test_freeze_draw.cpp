#include "Declarations.hpp"
#include "freeze/FrozenGraph.hpp"
#include "GraphBuilder.hpp"
#include "ImageDataFormats.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "LoomKeys.hpp"
#include "PassBuilders.hpp"
#include "PassStatements.hpp"
#include "Records.hpp"
#include "RhiFlags.hpp"
#include "freeze_test_helpers.hpp"
#include "utility/delegate.hpp"
#include <cstdint>
#include <optional>
#include <source_location>
#include <gtest/gtest.h>

// Resolve for draw and Record passes: the same pattern as test_freeze_resolve.cpp, one broken statement per test.

using namespace freeze_tests;

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
