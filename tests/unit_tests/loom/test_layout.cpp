#include "Declarations.hpp"
#include "ResourceFlags.hpp"
#include "PassStatements.hpp"
#include "size/Size.hpp"
#include "layout/Layout.hpp"
#include "layout/LaidOutGraph.hpp"
#include "size/SizedGraph.hpp"
#include "bind/BoundGraph.hpp"
#include "resolve/ResolvedGraph.hpp"
#include "freeze/FrozenGraph.hpp"
#include "GraphBuilder.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "LoomKeys.hpp"
#include "PassBuilders.hpp"
#include "ShaderBundle.hpp"
#include "ShaderManifestDevice.hpp"
#include "freeze_test_helpers.hpp"
#include "utility/Delegate.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <gtest/gtest.h>
#include <utility>

// The Layout stage, against LoomLayoutTest (tests/unit_tests/loom/shaders). GridCS runs 8x8 groups over Grid (an
// rgba32f storage image) and reads Weights. LayoutVS reads no vertex input; MeshVS reads POSITION. ColorPS writes one
// float4 and samples Source, TwoPS writes a float4 and a float2, IdPS one uint, and DepthPS a float4 and the depth.

using namespace freeze_tests;

namespace
{

    constexpr std::string_view k_Module = "LoomLayoutTest";

    constexpr loom::ResourceKey<loom::Image> k_Grid{ "Layout.Grid" };
    constexpr loom::ResourceKey<loom::Buffer> k_Weights{ "Layout.Weights" };
    constexpr loom::ResourceKey<loom::Image> k_Source{ "Layout.Source" };
    constexpr loom::ResourceKey<loom::Sampler> k_Linear{ "Layout.Linear" };
    constexpr loom::ResourceKey<loom::Image> k_Color{ "Layout.Color" };
    constexpr loom::ResourceKey<loom::Image> k_Normal{ "Layout.Normal" };
    constexpr loom::ResourceKey<loom::Image> k_Depth{ "Layout.Depth" };
    constexpr loom::PassKey k_Second{ "Test.Second" };
    constexpr loom::PassKey k_Third{ "Test.Third" };

    const rhi::ShaderBundle& SpirvBundle()
    {
        static std::optional<rhi::ShaderBundle> bundle;

        if (!bundle.has_value())
        {
            rhi::ShaderBundleResult<rhi::ShaderBundle> opened = rhi::ShaderBundle::Open(LOOM_TEST_BUNDLE, "spirv");
            EXPECT_TRUE(opened.has_value()) << rhi::ToString(opened.error());
            bundle.emplace(std::move(*opened));
        }

        return *bundle;
    }

    bool SupportsEverything(void*, std::string_view)
    {
        return true;
    }

    lodestone::manifest::DeviceLimits RoomyShaderLimits()
    {
        return lodestone::manifest::DeviceLimits
        {
            .MaxWorkgroupMemoryBytes = 49152u,
            .MaxInvocationsPerWorkgroup = 1024u,
            .MaxWorkgroupSizeX = 1024u,
            .MaxWorkgroupSizeY = 1024u,
            .MaxWorkgroupSizeZ = 64u,
            .MaxPushConstantBytes = 256u,
        };
    }

    // Each limit at or above the Vulkan minimum, so a test lowers the one limit that it checks
    loom::LayoutLimits RoomyLimits()
    {
        return loom::LayoutLimits
        {
            .MaxComputeWorkGroupCountX = 65535u,
            .MaxComputeWorkGroupCountY = 65535u,
            .MaxComputeWorkGroupCountZ = 65535u,
            .MaxColorAttachments = 8u,
            .MaxBoundDescriptorSets = 4u,
            .MaxPerStageDescriptorSamplers = 16u,
            .MaxPerStageDescriptorUniformBuffers = 12u,
            .MaxPerStageDescriptorStorageBuffers = 8u,
            .MaxPerStageDescriptorSampledImages = 16u,
            .MaxPerStageDescriptorStorageImages = 4u,
            .MaxPerStageResources = 128u,
            .MaxUniformBufferRange = 16384u,
            .MaxStorageBufferRange = 134217728u,
            .MaxImageDimension1D = 4096u,
            .MaxImageDimension2D = 4096u,
            .MaxImageDimension3D = 256u,
        };
    }

    struct LayoutRun
    {
        std::optional<loom::FrozenGraph> Frozen;
        std::optional<loom::ResolvedGraph> Resolved;
        std::optional<loom::BoundGraph> Bound;
        std::optional<loom::SizedGraph> Sized;
        std::optional<loom::LaidOutGraph> LaidOut;
        loom::CollectingSink Sink;
    };

    // Freeze, Resolve, Bind, Size and Layout. Every stage before Layout must succeed: the tests break Layout only.
    void RunLayout(const loom::GraphBuilder& graph, const loom::LayoutLimits& limits, LayoutRun& run)
    {
        run.Frozen = loom::Freeze(graph.Record(), run.Sink);
        ASSERT_TRUE(run.Frozen.has_value()) << run.Sink.Diagnostics().size() << " Freeze diagnostics";

        const loom::ResolveInputs inputs
        {
            .Bundle = SpirvBundle(),
            .Supports = &SupportsEverything,
            .SupportsUser = nullptr,
            .Limits = RoomyShaderLimits(),
        };

        run.Resolved = loom::Resolve(*run.Frozen, inputs, run.Sink);
        ASSERT_TRUE(run.Resolved.has_value()) << run.Sink.Diagnostics().size() << " Resolve diagnostics";

        run.Bound = loom::Bind(*run.Frozen, *run.Resolved, run.Sink);
        ASSERT_TRUE(run.Bound.has_value()) << run.Sink.Diagnostics().size() << " Bind diagnostics";

        run.Sized = loom::Size(*run.Frozen, *run.Bound, run.Sink);
        ASSERT_TRUE(run.Sized.has_value()) << run.Sink.Diagnostics().size() << " Size diagnostics";

        run.LaidOut = loom::Layout(*run.Frozen, *run.Resolved, *run.Bound, *run.Sized, limits, run.Sink);
    }

    loom::DiagnosticCode OnlyCode(const loom::CollectingSink& sink)
    {
        if (sink.Diagnostics().size() != 1u)
        {
            ADD_FAILURE() << sink.Diagnostics().size() << " diagnostics, expected 1";
            return loom::DiagnosticCode::Invalid;
        }

        return sink.Diagnostics().front().Code;
    }

    loom::ImageDeclaration FixedImage(loom::ResourceKey<loom::Image> key, lodestone::TextureFormat format,
                                      int32_t width, int32_t height)
    {
        loom::ImageDeclaration image = MakeImageOfTarget(k_Backbuffer);
        image.Key = key;
        image.Format = format;
        image.Extent = loom::ExtentSource::Fixed(width, height, 1);
        return image;
    }

    loom::BufferDeclaration WeightsOfBytes(int64_t bytes)
    {
        loom::BufferDeclaration buffer = MakeBuffer();
        buffer.Key = k_Weights;
        buffer.Size = loom::SizeSource::Bytes(bytes);
        return buffer;
    }

    // Grid as a 64x32 rgba32f image, and 256 bytes of Weights
    void DeclareGrid(loom::GraphBuilder& graph)
    {
        graph.Declare(FixedImage(k_Grid, lodestone::TextureFormat::RGBA32Float, 64, 32));
        graph.Declare(WeightsOfBytes(256));
    }

    void GridShader(loom::DispatchBuilder& pass)
    {
        pass.Shader(k_Module, "GridCS");
        pass.Bind("Grid", k_Grid);
        pass.Bind("Weights", k_Weights);
    }

    void GridThreads(loom::DispatchBuilder& pass)
    {
        GridShader(pass);
        pass.DispatchThreads(64, 32, 1);
    }

    void GridOverImage(loom::DispatchBuilder& pass)
    {
        GridShader(pass);
        pass.DispatchThreadsOver(k_Grid);
    }

    // Source and its sampler, for ColorPS
    void DeclareSource(loom::GraphBuilder& graph)
    {
        graph.Declare(FixedImage(k_Source, lodestone::TextureFormat::RGBA8Unorm, 64, 32));
        graph.Declare(loom::SamplerDeclaration{ .Key = k_Linear, .CreateInfo = {} });
    }

    void ColorShaders(loom::DrawBuilder& pass, loom::ResourceKey<loom::Image> source)
    {
        pass.Shaders(k_Module, "LayoutVS", "ColorPS");
        pass.Bind("Source", source);
        pass.Bind("Linear", k_Linear);
    }

    void ColorAt(loom::DrawBuilder& pass, int32_t slot, loom::ResourceKey<loom::Image> target)
    {
        pass.Color(slot, target, loom::ColorLoad::DontCare(), loom::AttachmentStore::Store, loom::BlendMode::Opaque);
    }

    void DepthOn(loom::DrawBuilder& pass, loom::DepthWrite write)
    {
        pass.Depth(k_Depth, loom::DepthLoad::Clear(1.0f), loom::AttachmentStore::Store, loom::CompareOp::Less, write);
    }

    // ColorPS onto Color at slot 0, sampling Source
    void ColorDraw(loom::DrawBuilder& pass)
    {
        ColorShaders(pass, k_Source);
        ColorAt(pass, 0, k_Color);
        pass.Draw(3, 1, 0, 0);
    }

    void RecordNothing(loom::RecordContext&)
    {
    }

}

// 64x32 threads over 8x8 groups is 8x4x1. 65 threads on X needs a ninth group, whose last 7 threads are past the end.
TEST(Layout, GroupCountsRoundUp)
{
    struct ThreadCase
    {
        int32_t X;
        int32_t ExpectedGroupsX;
    };

    for (const ThreadCase& threads : { ThreadCase{ 64, 8 }, ThreadCase{ 65, 9 } })
    {
        loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
        DeclareGrid(graph);

        const auto build = [&threads](loom::DispatchBuilder& pass)
        {
            GridShader(pass);
            pass.DispatchThreads(threads.X, 32, 1);
        };

        graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, build);

        LayoutRun run;
        RunLayout(graph, RoomyLimits(), run);

        ASSERT_TRUE(run.LaidOut.has_value());
        EXPECT_TRUE(run.Sink.Diagnostics().empty());

        const loom::LaidOutDispatch& dispatch = run.LaidOut->Passes[0].Dispatch;
        EXPECT_EQ(dispatch.Source, loom::GroupCountSource::Known);
        EXPECT_EQ(dispatch.Workgroup.X, 8u);
        EXPECT_EQ(dispatch.Workgroup.Y, 8u);
        EXPECT_EQ(dispatch.Workgroup.Z, 1u);
        EXPECT_EQ(dispatch.Groups.X, threads.ExpectedGroupsX);
        EXPECT_EQ(dispatch.Groups.Y, 4);
        EXPECT_EQ(dispatch.Groups.Z, 1);
    }
}

// DispatchThreadsOver divides the image's extent. An image sized from a frame target has no extent until Realize,
// which then divides by the workgroup size that Layout keeps.
TEST(Layout, ThreadsOverAnImageUseItsExtent)
{
    for (const bool fixed : { true, false })
    {
        loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
        graph.Import(k_Backbuffer);
        loom::ImageDeclaration grid = FixedImage(k_Grid, lodestone::TextureFormat::RGBA32Float, 64, 32);

        if (!fixed)
        {
            grid.Extent = loom::ExtentSource::OfTarget(k_Backbuffer, 1.0f);
        }

        graph.Declare(grid);
        graph.Declare(WeightsOfBytes(256));
        graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, &GridOverImage);

        LayoutRun run;
        RunLayout(graph, RoomyLimits(), run);

        ASSERT_TRUE(run.LaidOut.has_value());
        EXPECT_TRUE(run.Sink.Diagnostics().empty());

        const loom::LaidOutDispatch& dispatch = run.LaidOut->Passes[0].Dispatch;
        EXPECT_EQ(dispatch.Workgroup.X, 8u);

        if (fixed)
        {
            EXPECT_EQ(dispatch.Source, loom::GroupCountSource::Known);
            EXPECT_EQ(dispatch.Groups.X, 8);
            EXPECT_EQ(dispatch.Groups.Y, 4);
            continue;
        }

        EXPECT_EQ(dispatch.Source, loom::GroupCountSource::AtRealize);
    }
}

// LM601: 32 rows of threads in 8-row groups is 4 groups, one more than the limit. Expected is the limit.
TEST(Layout, GroupCountOverTheLimitIsLM601)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    DeclareGrid(graph);
    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, &GridThreads);

    loom::LayoutLimits limits = RoomyLimits();
    limits.MaxComputeWorkGroupCountY = 3u;

    LayoutRun run;
    RunLayout(graph, limits, run);

    EXPECT_FALSE(run.LaidOut.has_value());
    ASSERT_EQ(OnlyCode(run.Sink), loom::DiagnosticCode::LM601);

    const loom::Diagnostic& diagnostic = run.Sink.Diagnostics().front();
    EXPECT_EQ(diagnostic.Expected.FieldName, "MaxComputeWorkGroupCountY");
    EXPECT_EQ(diagnostic.Expected.Integer, 3);
    EXPECT_EQ(diagnostic.Actual.Integer, 4);
    EXPECT_EQ(diagnostic.Pass.Hash, k_First.Hash);
}

// Two dispatches of one shader share a pipeline; a draw has its own, with its color formats by slot.
TEST(Layout, PassesWithEqualPipelinesShareARow)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    DeclareGrid(graph);
    DeclareSource(graph);
    graph.Declare(FixedImage(k_Color, lodestone::TextureFormat::RGBA8Unorm, 64, 32));
    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, &GridThreads);
    graph.AddDispatch(k_Second, k_EveryFrame, k_Graphics, &GridOverImage);
    graph.AddDraw(k_Third, k_EveryFrame, k_Graphics, &ColorDraw);

    LayoutRun run;
    RunLayout(graph, RoomyLimits(), run);

    ASSERT_TRUE(run.LaidOut.has_value());
    EXPECT_TRUE(run.Sink.Diagnostics().empty());
    ASSERT_EQ(run.LaidOut->Pipelines.size(), 2u);
    EXPECT_EQ(run.LaidOut->Passes[0].Pipeline, 0);
    EXPECT_EQ(run.LaidOut->Passes[1].Pipeline, 0);
    EXPECT_EQ(run.LaidOut->Passes[2].Pipeline, 1);

    const loom::PipelineDescription& compute = run.LaidOut->Pipelines[0];
    EXPECT_EQ(compute.Module, k_Module);
    EXPECT_EQ(compute.EntryPoint, "GridCS");
    EXPECT_TRUE(compute.FragmentEntryPoint.empty());
    EXPECT_TRUE(compute.Colors.empty());

    const loom::PipelineDescription& draw = run.LaidOut->Pipelines[1];
    EXPECT_EQ(draw.EntryPoint, "LayoutVS");
    EXPECT_EQ(draw.FragmentEntryPoint, "ColorPS");
    ASSERT_EQ(draw.Colors.size(), 1u);
    EXPECT_EQ(draw.Colors[0].Slot, 0);
    EXPECT_EQ(draw.Colors[0].Format, lodestone::TextureFormat::RGBA8Unorm);
    EXPECT_EQ(draw.Colors[0].Blend, loom::BlendMode::Opaque);
    EXPECT_EQ(run.LaidOut->Passes[2].Dispatch.Source, loom::GroupCountSource::Invalid);
}

// LM602 both ways. TwoPS writes slot 1 and the draw has no attachment there: Actual is the output's slot. ColorPS
// writes slot 0 only and the draw has an attachment at slot 1: Expected is the attachment's slot.
TEST(Layout, OutputsAndAttachmentsMustPair)
{
    for (const bool outputWithNoAttachment : { true, false })
    {
        loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
        DeclareSource(graph);
        graph.Declare(FixedImage(k_Color, lodestone::TextureFormat::RGBA8Unorm, 64, 32));
        graph.Declare(FixedImage(k_Normal, lodestone::TextureFormat::RG16Float, 64, 32));

        const auto two = [](loom::DrawBuilder& pass)
        {
            pass.Shaders(k_Module, "LayoutVS", "TwoPS");
            ColorAt(pass, 0, k_Color);
            pass.Draw(3, 1, 0, 0);
        };

        const auto extra = [](loom::DrawBuilder& pass)
        {
            ColorShaders(pass, k_Source);
            ColorAt(pass, 0, k_Color);
            ColorAt(pass, 1, k_Normal);
            pass.Draw(3, 1, 0, 0);
        };

        if (outputWithNoAttachment)
        {
            graph.AddDraw(k_First, k_EveryFrame, k_Graphics, two);
        }
        else
        {
            graph.AddDraw(k_First, k_EveryFrame, k_Graphics, extra);
        }

        LayoutRun run;
        RunLayout(graph, RoomyLimits(), run);

        EXPECT_FALSE(run.LaidOut.has_value());
        ASSERT_EQ(OnlyCode(run.Sink), loom::DiagnosticCode::LM602);

        const loom::Diagnostic& diagnostic = run.Sink.Diagnostics().front();
        const loom::DiagnosticValue& slot = outputWithNoAttachment ? diagnostic.Actual : diagnostic.Expected;
        const loom::DiagnosticValue& none = outputWithNoAttachment ? diagnostic.Expected : diagnostic.Actual;
        EXPECT_EQ(slot.FieldName, "Slot");
        EXPECT_EQ(slot.Integer, 1);
        EXPECT_EQ(none.Kind, loom::DiagnosticValueKind::None);
    }
}

// LM602: the output's scalar class must fit the format, and the shader must write every component of the format.
// TwoPS writes a float2 at slot 1: RG16Float fits, RGBA8Unorm leaves two components undefined. IdPS writes a uint.
TEST(Layout, OutputsMustFitTheirFormats)
{
    struct FormatCase
    {
        std::string_view Fragment;
        lodestone::TextureFormat Color;
        lodestone::TextureFormat Normal;
        std::string_view ExpectedField;    // empty when the formats fit
    };

    const std::array<FormatCase, 5u> cases
    {
        FormatCase{ "TwoPS", lodestone::TextureFormat::RGBA8Unorm, lodestone::TextureFormat::RG16Float, "" },
        FormatCase{ "TwoPS", lodestone::TextureFormat::RGBA8Unorm, lodestone::TextureFormat::RGBA8Unorm, "Components" },
        FormatCase{ "IdPS", lodestone::TextureFormat::R32Uint, lodestone::TextureFormat::Invalid, "" },
        FormatCase{ "IdPS", lodestone::TextureFormat::RGBA8Unorm, lodestone::TextureFormat::Invalid, "Format" },
        FormatCase{ "TwoPS", lodestone::TextureFormat::R32Uint, lodestone::TextureFormat::RG16Float, "Format" },
    };

    for (const FormatCase& formats : cases)
    {
        loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
        graph.Declare(FixedImage(k_Color, formats.Color, 64, 32));
        const bool twoTargets = formats.Normal != lodestone::TextureFormat::Invalid;

        if (twoTargets)
        {
            graph.Declare(FixedImage(k_Normal, formats.Normal, 64, 32));
        }

        const auto build = [&formats, twoTargets](loom::DrawBuilder& pass)
        {
            pass.Shaders(k_Module, "LayoutVS", formats.Fragment);
            ColorAt(pass, 0, k_Color);

            if (twoTargets)
            {
                ColorAt(pass, 1, k_Normal);
            }

            pass.Draw(3, 1, 0, 0);
        };

        graph.AddDraw(k_First, k_EveryFrame, k_Graphics, build);

        LayoutRun run;
        RunLayout(graph, RoomyLimits(), run);

        if (formats.ExpectedField.empty())
        {
            ASSERT_TRUE(run.LaidOut.has_value()) << formats.Fragment;
            EXPECT_TRUE(run.Sink.Diagnostics().empty());
            continue;
        }

        EXPECT_FALSE(run.LaidOut.has_value());
        ASSERT_EQ(OnlyCode(run.Sink), loom::DiagnosticCode::LM602) << formats.Fragment;
        EXPECT_EQ(run.Sink.Diagnostics().front().Expected.FieldName, formats.ExpectedField);
    }
}

// LM602: DepthPS writes the depth, so the draw needs a Depth statement. With one, the pipeline has its format.
TEST(Layout, DepthOutputNeedsADepthTarget)
{
    for (const bool withDepth : { false, true })
    {
        loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
        graph.Declare(FixedImage(k_Color, lodestone::TextureFormat::RGBA8Unorm, 64, 32));
        graph.Declare(FixedImage(k_Depth, lodestone::TextureFormat::Depth32Float, 64, 32));

        const auto build = [withDepth](loom::DrawBuilder& pass)
        {
            pass.Shaders(k_Module, "LayoutVS", "DepthPS");
            ColorAt(pass, 0, k_Color);

            if (withDepth)
            {
                DepthOn(pass, loom::DepthWrite::Enabled);
            }

            pass.Draw(3, 1, 0, 0);
        };

        graph.AddDraw(k_First, k_EveryFrame, k_Graphics, build);

        LayoutRun run;
        RunLayout(graph, RoomyLimits(), run);

        if (withDepth)
        {
            ASSERT_TRUE(run.LaidOut.has_value());
            EXPECT_TRUE(run.Sink.Diagnostics().empty());
            EXPECT_EQ(run.LaidOut->Pipelines[0].DepthFormat, lodestone::TextureFormat::Depth32Float);
            EXPECT_EQ(run.LaidOut->Pipelines[0].DepthWrites, loom::DepthWrite::Enabled);
            continue;
        }

        EXPECT_FALSE(run.LaidOut.has_value());
        ASSERT_EQ(OnlyCode(run.Sink), loom::DiagnosticCode::LM602);
        EXPECT_EQ(run.Sink.Diagnostics().front().Actual.Text, "Written");
    }
}

// LM603: MeshVS reads POSITION, and Loom has no vertex format. LayoutVS (SV_VertexID only) is in every other test.
TEST(Layout, VertexInputIsLM603)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    DeclareSource(graph);
    graph.Declare(FixedImage(k_Color, lodestone::TextureFormat::RGBA8Unorm, 64, 32));

    const auto build = [](loom::DrawBuilder& pass)
    {
        pass.Shaders(k_Module, "MeshVS", "ColorPS");
        pass.Bind("Source", k_Source);
        pass.Bind("Linear", k_Linear);
        ColorAt(pass, 0, k_Color);
        pass.Draw(3, 1, 0, 0);
    };

    graph.AddDraw(k_First, k_EveryFrame, k_Graphics, build);

    LayoutRun run;
    RunLayout(graph, RoomyLimits(), run);

    EXPECT_FALSE(run.LaidOut.has_value());
    ASSERT_EQ(OnlyCode(run.Sink), loom::DiagnosticCode::LM603);
    EXPECT_EQ(run.Sink.Diagnostics().front().Binding, "POSITION");
    EXPECT_FALSE(run.Sink.Diagnostics().front().Suggestion.empty());
}

// LM604: ColorPS samples the image that it writes as its color target. Where is the bind, OtherWhere the target.
TEST(Layout, SampledColorTargetIsLM604)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    DeclareSource(graph);
    graph.Declare(FixedImage(k_Color, lodestone::TextureFormat::RGBA8Unorm, 64, 32));

    const auto build = [](loom::DrawBuilder& pass)
    {
        ColorShaders(pass, k_Color);
        ColorAt(pass, 0, k_Color);
        pass.Draw(3, 1, 0, 0);
    };

    graph.AddDraw(k_First, k_EveryFrame, k_Graphics, build);

    LayoutRun run;
    RunLayout(graph, RoomyLimits(), run);

    EXPECT_FALSE(run.LaidOut.has_value());
    ASSERT_EQ(OnlyCode(run.Sink), loom::DiagnosticCode::LM604);

    const loom::Diagnostic& diagnostic = run.Sink.Diagnostics().front();
    EXPECT_EQ(diagnostic.Binding, "Source");
    EXPECT_EQ(diagnostic.Resource, k_Color.ToRef());
    EXPECT_EQ(diagnostic.Expected.Text, "ColorTarget");
    EXPECT_EQ(diagnostic.Actual.Text, "Sampled");
}

// A depth target with depth writes off is read-only, so the pass may sample it. With writes on, it is LM604.
TEST(Layout, ReadOnlyDepthMaySampleItsTarget)
{
    for (const loom::DepthWrite write : { loom::DepthWrite::Disabled, loom::DepthWrite::Enabled })
    {
        loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
        DeclareSource(graph);
        graph.Declare(FixedImage(k_Color, lodestone::TextureFormat::RGBA8Unorm, 64, 32));
        graph.Declare(FixedImage(k_Depth, lodestone::TextureFormat::Depth32Float, 64, 32));

        const auto build = [write](loom::DrawBuilder& pass)
        {
            ColorShaders(pass, k_Depth);
            ColorAt(pass, 0, k_Color);
            DepthOn(pass, write);
            pass.Draw(3, 1, 0, 0);
        };

        graph.AddDraw(k_First, k_EveryFrame, k_Graphics, build);

        LayoutRun run;
        RunLayout(graph, RoomyLimits(), run);

        if (write == loom::DepthWrite::Disabled)
        {
            ASSERT_TRUE(run.LaidOut.has_value());
            EXPECT_TRUE(run.Sink.Diagnostics().empty());
            continue;
        }

        EXPECT_FALSE(run.LaidOut.has_value());
        ASSERT_EQ(OnlyCode(run.Sink), loom::DiagnosticCode::LM604);
        EXPECT_EQ(run.Sink.Diagnostics().front().Expected.Text, "DepthTarget");
    }
}

// LM604 in a Record pass: both sides are uses. A read-only depth target use writes nothing, so it may be sampled.
TEST(Layout, RecordUsesCanLoop)
{
    for (const bool depthReadOnly : { false, true })
    {
        loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
        const lodestone::TextureFormat format = depthReadOnly ? lodestone::TextureFormat::Depth32Float :
                                                                lodestone::TextureFormat::RGBA8Unorm;
        graph.Declare(FixedImage(k_Color, format, 64, 32));

        const auto build = [depthReadOnly](loom::RecordBuilder& pass)
        {
            if (depthReadOnly)
            {
                pass.Uses(k_Color, loom::ResourceAccess::Read, loom::ImageUse::DepthTarget);
            }
            else
            {
                pass.Uses(k_Color, loom::ResourceAccess::Write, loom::ImageUse::ColorTarget);
            }

            pass.Uses(k_Color, loom::ResourceAccess::Read, loom::ImageUse::Sampled);
            pass.Record(Delegate<void(loom::RecordContext&)>::Create<&RecordNothing>());
        };

        graph.AddRecord(k_First, k_EveryFrame, k_Graphics, build);

        LayoutRun run;
        RunLayout(graph, RoomyLimits(), run);

        if (depthReadOnly)
        {
            ASSERT_TRUE(run.LaidOut.has_value());
            EXPECT_TRUE(run.Sink.Diagnostics().empty());
            EXPECT_EQ(run.LaidOut->Passes[0].Pipeline, loom::k_NoPipeline);
            continue;
        }

        EXPECT_FALSE(run.LaidOut.has_value());
        ASSERT_EQ(OnlyCode(run.Sink), loom::DiagnosticCode::LM604);
        EXPECT_EQ(run.Sink.Diagnostics().front().Actual.Text, "Sampled");
    }
}

// LM601 for the descriptor limits. Each case lowers one limit under what the pass uses. GridCS has one storage image
// and one storage buffer in set 0. ColorDraw's fragment stage has one sampled image, one sampler and one color
// attachment; maxPerStageResources counts the image and the attachment, not the sampler.
TEST(Layout, DescriptorLimitsAreLM601)
{
    struct LimitCase
    {
        bool Draw;
        uint32_t loom::LayoutLimits::* Limit;
        uint32_t Value;
        std::string_view Name;
        int64_t Used;
    };

    using L = loom::LayoutLimits;
    const std::array<LimitCase, 8u> cases
    {
        LimitCase{ false, &L::MaxPerStageDescriptorStorageImages, 0u, "MaxPerStageDescriptorStorageImages", 1 },
        LimitCase{ false, &L::MaxPerStageDescriptorStorageBuffers, 0u, "MaxPerStageDescriptorStorageBuffers", 1 },
        LimitCase{ false, &L::MaxBoundDescriptorSets, 0u, "MaxBoundDescriptorSets", 1 },
        LimitCase{ false, &L::MaxPerStageResources, 1u, "MaxPerStageResources", 2 },
        LimitCase{ true, &L::MaxPerStageDescriptorSampledImages, 0u, "MaxPerStageDescriptorSampledImages", 1 },
        LimitCase{ true, &L::MaxPerStageDescriptorSamplers, 0u, "MaxPerStageDescriptorSamplers", 1 },
        LimitCase{ true, &L::MaxColorAttachments, 0u, "MaxColorAttachments", 1 },
        LimitCase{ true, &L::MaxPerStageResources, 1u, "MaxPerStageResources", 2 },
    };

    for (const LimitCase& limit : cases)
    {
        loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };

        if (limit.Draw)
        {
            DeclareSource(graph);
            graph.Declare(FixedImage(k_Color, lodestone::TextureFormat::RGBA8Unorm, 64, 32));
            graph.AddDraw(k_First, k_EveryFrame, k_Graphics, &ColorDraw);
        }
        else
        {
            DeclareGrid(graph);
            graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, &GridThreads);
        }

        loom::LayoutLimits limits = RoomyLimits();
        limits.*limit.Limit = limit.Value;

        LayoutRun run;
        RunLayout(graph, limits, run);

        EXPECT_FALSE(run.LaidOut.has_value()) << limit.Name;
        ASSERT_EQ(OnlyCode(run.Sink), loom::DiagnosticCode::LM601) << limit.Name;

        const loom::Diagnostic& diagnostic = run.Sink.Diagnostics().front();
        EXPECT_EQ(diagnostic.Expected.FieldName, limit.Name);
        EXPECT_EQ(diagnostic.Expected.Integer, static_cast<int64_t>(limit.Value));
        EXPECT_EQ(diagnostic.Actual.Integer, limit.Used) << limit.Name;
    }
}

// LM601: Bind gives no range, so a descriptor covers the whole buffer: 256 bytes of Weights against a 128-byte range.
TEST(Layout, BufferRangeIsLM601)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    DeclareGrid(graph);
    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, &GridThreads);

    loom::LayoutLimits limits = RoomyLimits();
    limits.MaxStorageBufferRange = 128u;

    LayoutRun run;
    RunLayout(graph, limits, run);

    EXPECT_FALSE(run.LaidOut.has_value());
    ASSERT_EQ(OnlyCode(run.Sink), loom::DiagnosticCode::LM601);

    const loom::Diagnostic& diagnostic = run.Sink.Diagnostics().front();
    EXPECT_EQ(diagnostic.Expected.FieldName, "MaxStorageBufferRange");
    EXPECT_EQ(diagnostic.Actual.Integer, 256);
    EXPECT_EQ(diagnostic.Resource, k_Weights.ToRef());
}

// LM601: a 64x32 2D image against a 2D limit of 32. Actual is the largest axis; the report has no pass.
TEST(Layout, ImageExtentIsLM601)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    DeclareGrid(graph);
    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, &GridThreads);

    loom::LayoutLimits limits = RoomyLimits();
    limits.MaxImageDimension2D = 32u;

    LayoutRun run;
    RunLayout(graph, limits, run);

    EXPECT_FALSE(run.LaidOut.has_value());
    ASSERT_EQ(OnlyCode(run.Sink), loom::DiagnosticCode::LM601);

    const loom::Diagnostic& diagnostic = run.Sink.Diagnostics().front();
    EXPECT_EQ(diagnostic.Expected.FieldName, "MaxImageDimension2D");
    EXPECT_EQ(diagnostic.Actual.Integer, 64);
    EXPECT_EQ(diagnostic.Resource, k_Grid.ToRef());
}
