#include "Compile.hpp"
#include "CompiledGraph.hpp"
#include "Declarations.hpp"
#include "DeviceCapability.hpp"
#include "DeviceRequirements.hpp"
#include "GraphBuilder.hpp"
#include "GraphDeviceNeeds.hpp"
#include "LoomDeviceLimits.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "LoomKeys.hpp"
#include "PassBuilders.hpp"
#include "PathListBuilder.hpp"
#include "ShaderBundle.hpp"
#include "ShaderLibraryTypes.hpp"
#include "compile/CompiledData.hpp"
#include "compile/PathChoice.hpp"
#include "freeze_test_helpers.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <gtest/gtest.h>

// Path lists (step 5) with no GPU. The graph is LoomDrawTest's, with the cull and the depth prepass in a list: the
// first path draws with DrawIndirectCount, the fallback with DrawIndirect. Each path declares its own argument and
// count buffers (the same keys), and the list produces the depth image that the GBuffer pass reads.

using namespace freeze_tests;

namespace
{

    using bootstrap::DeviceCapability;

    constexpr loom::ResourceKey<loom::Buffer> k_Positions{ "Paths.Positions" };
    constexpr loom::ResourceKey<loom::Buffer> k_Arguments{ "Paths.Arguments" };
    constexpr loom::ResourceKey<loom::Buffer> k_DrawCount{ "Paths.DrawCount" };
    constexpr loom::ResourceKey<loom::Image> k_Depth{ "Paths.Depth" };
    constexpr loom::ResourceKey<loom::Image> k_GAlbedo{ "Paths.Albedo" };
    constexpr loom::ResourceKey<loom::Image> k_GNormal{ "Paths.Normal" };
    constexpr loom::ResourceKey<loom::Sampler> k_Point{ "Paths.Point" };
    constexpr loom::FrameTargetKey k_Screen{ "Paths.Screen" };
    constexpr loom::PassKey k_Cull{ "Paths.Cull" };
    constexpr loom::PassKey k_PrepassCount{ "Paths.PrepassCount" };
    constexpr loom::PassKey k_PrepassSingle{ "Paths.PrepassSingle" };
    constexpr loom::PassKey k_GBuffer{ "Paths.GBuffer" };
    constexpr loom::PassKey k_Light{ "Paths.Light" };
    constexpr loom::PathListKey k_DepthList{ "Paths.DepthList" };
    constexpr loom::PathKey k_CountPath{ "Paths.CountPath" };
    constexpr loom::PathKey k_SinglePath{ "Paths.SinglePath" };
    constexpr loom::PathKey k_MissingPath{ "Paths.MissingPath" };
    constexpr loom::AnchorKey k_AfterCull{ "Paths.AfterCull" };

    constexpr std::array k_WithDrawIndirectCount{ DeviceCapability::DrawIndirectCount };

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

    loom::CompileInputs Inputs(std::span<const DeviceCapability> capabilities, std::span<const loom::ForcedPath> forced)
    {
        return loom::CompileInputs
        {
            .Bundle = SpirvBundle(),
            .Supports = &SupportsEverything,
            .SupportsUser = nullptr,
            .ShaderLimits = loom::ShaderDeviceLimits
            {
                .MaxWorkgroupMemoryBytes = 49152u,
                .MaxInvocationsPerWorkgroup = 1024u,
                .MaxWorkgroupSizeX = 1024u,
                .MaxWorkgroupSizeY = 1024u,
                .MaxWorkgroupSizeZ = 64u,
                .MaxPushConstantBytes = 256u,
            },
            .Limits = loom::LayoutLimits
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
            },
            .Capabilities = capabilities,
            .ForcedPaths = forced,
        };
    }

    loom::ImageDeclaration TargetSized(loom::ResourceKey<loom::Image> key, lodestone::TextureFormat format)
    {
        return loom::ImageDeclaration
        {
            .Key = key,
            .Lifetime = loom::ResourceLifetime::Frame,
            .Format = format,
            .Extent = loom::ExtentSource::OfTarget(k_Screen, 1.0f),
            .Mips = loom::MipSource::One(),
            .Layers = loom::LayerSource::Fixed(1),
            .Cpu = loom::CpuAccess::None,
            .HistoryDepth = 0,
            .NoHistoryAction = loom::OnNoHistory::Invalid,
        };
    }

    loom::BufferDeclaration Bytes(loom::ResourceKey<loom::Buffer> key, int64_t bytes)
    {
        return loom::BufferDeclaration
        {
            .Key = key,
            .Lifetime = loom::ResourceLifetime::Frame,
            .Size = loom::SizeSource::Bytes(bytes),
            .Cpu = loom::CpuAccess::None,
            .HistoryDepth = 0,
            .NoHistoryAction = loom::OnNoHistory::Invalid,
        };
    }

    // The plants: each one breaks one rule of the list contract
    struct PathGraphOptions
    {
        bool ProducesDepth{ true };         // false: the paths write an outside key that the list does not produce (LM309)
        bool ProducesNormal{ false };       // true: the list produces a key that no path writes (LM308)
        bool CountPathExposesAnchor{ false };   // true: only one path exposes an anchor (LM116)
        bool HasSinglePath{ true };         // false: the list has no fallback (LM115 without DrawIndirectCount)
        bool HasPaths{ true };              // false: an empty list (LM114)
    };

    void Cull(loom::DispatchBuilder& pass)
    {
        pass.Shader("LoomDraw", "CullCS");
        pass.Bind("DrawArguments", k_Arguments, loom::AccessModifier::WriteOnly);
        pass.Bind("DrawCount", k_DrawCount, loom::AccessModifier::WriteOnly);
        pass.DispatchThreads(1, 1, 1);
    }

    void DepthOnly(loom::DrawBuilder& pass)
    {
        pass.Shaders("LoomDraw", "SceneVS", "");
        pass.Bind("Positions", k_Positions);
        pass.Depth(k_Depth, loom::DepthLoad::Clear(1.0f), loom::AttachmentStore::Store, loom::CompareOp::Less,
                   loom::DepthWrite::Enabled);
    }

    void PrepassCount(loom::DrawBuilder& pass)
    {
        DepthOnly(pass);
        pass.DrawIndirectCount(k_Arguments, 0, k_DrawCount, 0, 1);
    }

    void PrepassSingle(loom::DrawBuilder& pass)
    {
        DepthOnly(pass);
        pass.DrawIndirect(k_Arguments);
    }

    void DeclareCullBuffers(loom::PathBuilder& path)
    {
        path.Declare(Bytes(k_Arguments, 16));
        path.Declare(Bytes(k_DrawCount, 4));
    }

    void BuildPathGraph(loom::GraphBuilder& graph, const PathGraphOptions& options)
    {
        graph.Import(k_Screen);
        graph.Output(loom::OutputDeclaration{ .Target = k_Screen, .OnSkip = loom::OutputFallback::Clear(0.1f, 0.2f, 0.3f, 1.0f) });
        graph.Import(loom::BufferImportDeclaration{ .Key = k_Positions, .ByteSize = 96 });
        graph.Declare(TargetSized(k_Depth, lodestone::TextureFormat::Depth32Float));
        graph.Declare(TargetSized(k_GAlbedo, lodestone::TextureFormat::RGBA8Unorm));
        graph.Declare(TargetSized(k_GNormal, lodestone::TextureFormat::RGBA16Float));
        graph.Declare(loom::SamplerDeclaration{ .Key = k_Point, .CreateInfo = SamplerCreateInfo{} });
        graph.DeclareAnchor(k_AfterCull);

        const auto countPath = [&options](loom::PathBuilder& path)
        {
            DeclareCullBuffers(path);
            path.AddDispatch(k_Cull, k_EveryFrame, k_Graphics, Cull);

            if (options.CountPathExposesAnchor)
            {
                path.ExposeAnchor(k_AfterCull);
            }

            path.AddDraw(k_PrepassCount, k_EveryFrame, k_Graphics, PrepassCount);
        };

        const auto singlePath = [](loom::PathBuilder& path)
        {
            DeclareCullBuffers(path);
            path.AddDispatch(k_Cull, k_EveryFrame, k_Graphics, Cull);
            path.AddDraw(k_PrepassSingle, k_EveryFrame, k_Graphics, PrepassSingle);
        };

        const auto paths = [&](loom::PathListBuilder& list)
        {
            if (options.ProducesDepth)
            {
                list.Produces(k_Depth);
            }

            if (options.ProducesNormal)
            {
                list.Produces(k_GNormal);
            }

            if (!options.HasPaths)
            {
                return;
            }

            list.Path(k_CountPath, countPath);

            if (options.HasSinglePath)
            {
                list.Path(k_SinglePath, singlePath);
            }

        };

        const auto gbuffer = [](loom::DrawBuilder& pass)
        {
            pass.Shaders("LoomDraw", "SceneVS", "GBufferPS");
            pass.Bind("Positions", k_Positions);
            pass.Color(0, k_GAlbedo, loom::ColorLoad::Clear(0.0f, 0.0f, 0.0f, 0.0f), loom::AttachmentStore::Store,
                       loom::BlendMode::Opaque);
            pass.Color(1, k_GNormal, loom::ColorLoad::Clear(0.0f, 0.0f, 0.0f, 0.0f), loom::AttachmentStore::Store,
                       loom::BlendMode::Opaque);
            pass.Depth(k_Depth, loom::DepthLoad::Keep(), loom::AttachmentStore::Store, loom::CompareOp::Equal,
                       loom::DepthWrite::Disabled);
            pass.Draw(3, 1, 0, 0);
        };

        const auto light = [](loom::DrawBuilder& pass)
        {
            pass.Shaders("LoomDraw", "FullscreenVS", "LightPS");
            pass.Bind("Albedo", k_GAlbedo);
            pass.Bind("Normal", k_GNormal);
            pass.Bind("PointSampler", k_Point);
            pass.Color(0, k_Screen, loom::ColorLoad::DontCare(), loom::AttachmentStore::Store, loom::BlendMode::Opaque);
            pass.Draw(3, 1, 0, 0);
        };

        graph.AddPaths(k_DepthList, paths);
        graph.AddDraw(k_GBuffer, k_EveryFrame, k_Graphics, gbuffer);
        graph.AddDraw(k_Light, k_EveryFrame, k_Graphics, light);
    }

    struct PathCompile
    {
        loom::CollectingSink Sink;
        std::optional<loom::CompiledGraph> Compiled;
    };

    void CompilePaths(const PathGraphOptions& options,
                      std::span<const DeviceCapability> capabilities,
                      std::span<const loom::ForcedPath> forced,
                      PathCompile& run)
    {
        loom::GraphBuilder graph{ "Paths.Graph", "Paths.Library" };
        BuildPathGraph(graph, options);
        run.Compiled = loom::Compile(graph, Inputs(capabilities, forced), run.Sink);
    }

    int32_t CountOf(const loom::CollectingSink& sink, loom::DiagnosticCode code)
    {
        return static_cast<int32_t>(std::ranges::count(sink.Diagnostics(), code, &loom::Diagnostic::Code));
    }

    const loom::Diagnostic* FirstOf(const loom::CollectingSink& sink, loom::DiagnosticCode code)
    {
        const auto found = std::ranges::find(sink.Diagnostics(), code, &loom::Diagnostic::Code);
        return found == sink.Diagnostics().end() ? nullptr : &*found;
    }

}

// The device has DrawIndirectCount: the first path wins, and its passes are the only list passes in the graph.
TEST(Paths, TheFirstSatisfiedPathIsChosen)
{
    PathCompile run;
    CompilePaths(PathGraphOptions{}, k_WithDrawIndirectCount, {}, run);

    ASSERT_TRUE(run.Compiled.has_value()) << run.Sink.Diagnostics().size() << " diagnostics";
    EXPECT_EQ(run.Sink.ErrorCount(), 0);
    EXPECT_TRUE(run.Compiled->IsChosen(k_DepthList, k_CountPath));
    EXPECT_EQ(run.Compiled->ChosenPath(k_DepthList).Hash, k_CountPath.Hash);
    EXPECT_GE(run.Compiled->PassIndexOf(k_PrepassCount), 0);
    EXPECT_EQ(run.Compiled->PassIndexOf(k_PrepassSingle), -1);
    EXPECT_EQ(loom::ExplainPaths(*run.Compiled), "Paths.DepthList: chose Paths.CountPath\n");
}

// Without DrawIndirectCount the fallback wins, and Explain names the capability that the first path lacks.
TEST(Paths, TheFallbackIsChosenWithoutTheCapability)
{
    PathCompile run;
    CompilePaths(PathGraphOptions{}, {}, {}, run);

    ASSERT_TRUE(run.Compiled.has_value()) << run.Sink.Diagnostics().size() << " diagnostics";
    EXPECT_EQ(run.Sink.ErrorCount(), 0);
    EXPECT_TRUE(run.Compiled->IsChosen(k_DepthList, k_SinglePath));
    EXPECT_EQ(run.Compiled->PassIndexOf(k_PrepassCount), -1);
    EXPECT_GE(run.Compiled->PassIndexOf(k_PrepassSingle), 0);

    const std::vector<loom::PathChoice>& choices = run.Compiled->Data().PathChoices;
    ASSERT_EQ(choices.size(), 1u);
    ASSERT_EQ(choices[0].Rejected.size(), 1u);
    EXPECT_EQ(choices[0].Rejected[0].Reason, loom::PathRejection::Capability);
    EXPECT_EQ(choices[0].Rejected[0].Capability, DeviceCapability::DrawIndirectCount);
    EXPECT_EQ(choices[0].Rejected[0].Pass.Hash, k_PrepassCount.Hash);

    EXPECT_EQ(loom::ExplainPaths(*run.Compiled),
              "Paths.DepthList: chose Paths.SinglePath\n"
              "  passed over Paths.CountPath: pass 'Paths.PrepassCount' needs DrawIndirectCount\n");
}

// Both paths declare the argument and count buffers under the same keys. Only the chosen path's declarations join the
// graph, so each key has one row and no LM102.
TEST(Paths, OnlyTheChosenPathDeclares)
{
    PathCompile run;
    CompilePaths(PathGraphOptions{}, {}, {}, run);

    ASSERT_TRUE(run.Compiled.has_value()) << run.Sink.Diagnostics().size() << " diagnostics";
    EXPECT_EQ(CountOf(run.Sink, loom::DiagnosticCode::LM102), 0);
    EXPECT_GE(run.Compiled->ResourceIndexOf(k_Arguments.ToRef()), 0);
    EXPECT_GE(run.Compiled->ResourceIndexOf(k_DrawCount.ToRef()), 0);

    const loom::PathChoice& choice = run.Compiled->Data().PathChoices[0];
    EXPECT_EQ(choice.LocalResources.size(), 2u);
    EXPECT_EQ(choice.Passes.size(), 2u);
}

// The list sits in the graph like a sequence: its passes run before the GBuffer pass, because the GBuffer pass reads
// the depth that the list produces (Order's data flow, no new rule).
TEST(Paths, AProducedKeyOrdersTheReaderAfterThePath)
{
    PathCompile run;
    CompilePaths(PathGraphOptions{}, k_WithDrawIndirectCount, {}, run);

    ASSERT_TRUE(run.Compiled.has_value()) << run.Sink.Diagnostics().size() << " diagnostics";

    const std::vector<loom::PassIndex>& order = run.Compiled->Data().Ordered.Passes;

    const auto positionOf = [&](loom::PassKey pass)
    {
        const int32_t index = run.Compiled->PassIndexOf(pass);

        const auto isPass = [index](loom::PassIndex ordered)
        {
            return ordered.Value == index;
        };

        return std::ranges::find_if(order, isPass) - order.begin();
    };

    EXPECT_LT(positionOf(k_Cull), positionOf(k_PrepassCount));
    EXPECT_LT(positionOf(k_PrepassCount), positionOf(k_GBuffer));
}

// A forced path wins over the device: tests compile every path, whatever the machine has.
TEST(Paths, EachForcedPathCompiles)
{
    const std::array<loom::PathKey, 2> paths{ k_CountPath, k_SinglePath };

    for (const loom::PathKey path : paths)
    {
        const std::array forced{ loom::ForcedPath{ .List = k_DepthList, .Path = path } };
        PathCompile run;
        CompilePaths(PathGraphOptions{}, {}, forced, run);

        ASSERT_TRUE(run.Compiled.has_value()) << path.Name << ": " << run.Sink.Diagnostics().size() << " diagnostics";
        EXPECT_EQ(run.Sink.ErrorCount(), 0) << path.Name;
        EXPECT_TRUE(run.Compiled->IsChosen(k_DepthList, path));
        EXPECT_TRUE(run.Compiled->Data().PathChoices[0].Forced);
        EXPECT_TRUE(run.Compiled->Data().PathChoices[0].Rejected.empty());
    }

}

TEST(Paths, AForcedPathThatNamesNoPathIsLM103)
{
    const std::array forced{ loom::ForcedPath{ .List = k_DepthList, .Path = k_MissingPath } };
    PathCompile run;
    CompilePaths(PathGraphOptions{}, {}, forced, run);

    EXPECT_FALSE(run.Compiled.has_value());
    const loom::Diagnostic* diagnostic = FirstOf(run.Sink, loom::DiagnosticCode::LM103);
    ASSERT_NE(diagnostic, nullptr);
    EXPECT_EQ(diagnostic->Actual.Text, k_MissingPath.Name);
}

TEST(Paths, AnEmptyListIsLM114)
{
    PathCompile run;
    CompilePaths(PathGraphOptions{ .HasPaths = false }, k_WithDrawIndirectCount, {}, run);

    EXPECT_FALSE(run.Compiled.has_value());
    EXPECT_EQ(CountOf(run.Sink, loom::DiagnosticCode::LM114), 1);
}

// No fallback, and no DrawIndirectCount: each path reports its first missing capability.
TEST(Paths, NoSatisfiedPathIsLM115)
{
    PathCompile run;
    CompilePaths(PathGraphOptions{ .HasSinglePath = false }, {}, {}, run);

    EXPECT_FALSE(run.Compiled.has_value());
    ASSERT_EQ(CountOf(run.Sink, loom::DiagnosticCode::LM115), 1);

    const loom::Diagnostic& diagnostic = *FirstOf(run.Sink, loom::DiagnosticCode::LM115);
    EXPECT_EQ(diagnostic.Pass.Hash, k_PrepassCount.Hash);
    EXPECT_EQ(diagnostic.Expected.Text, "DrawIndirectCount");
}

TEST(Paths, PathsThatExposeDifferentAnchorsAreLM116)
{
    PathCompile run;
    CompilePaths(PathGraphOptions{ .CountPathExposesAnchor = true }, k_WithDrawIndirectCount, {}, run);

    EXPECT_FALSE(run.Compiled.has_value());
    ASSERT_EQ(CountOf(run.Sink, loom::DiagnosticCode::LM116), 1);

    // The first path is the reference: the fallback lacks the anchor
    const loom::Diagnostic& diagnostic = *FirstOf(run.Sink, loom::DiagnosticCode::LM116);
    EXPECT_EQ(diagnostic.Expected.Text, k_AfterCull.Name);
}

// The plant for the contract: the list produces the normal image, and no path writes it. Every forced path must fail,
// so a tier that drops an output fails a test.
TEST(Paths, AProducedKeyWithNoWriterIsLM308OnEveryPath)
{
    const std::array<loom::PathKey, 2> paths{ k_CountPath, k_SinglePath };

    for (const loom::PathKey path : paths)
    {
        const std::array forced{ loom::ForcedPath{ .List = k_DepthList, .Path = path } };
        PathCompile run;
        CompilePaths(PathGraphOptions{ .ProducesNormal = true }, {}, forced, run);

        EXPECT_FALSE(run.Compiled.has_value()) << path.Name;
        ASSERT_EQ(CountOf(run.Sink, loom::DiagnosticCode::LM308), 1) << path.Name;

        const loom::Diagnostic& diagnostic = *FirstOf(run.Sink, loom::DiagnosticCode::LM308);
        EXPECT_EQ(diagnostic.Resource.Hash, k_GNormal.Hash);
        EXPECT_EQ(diagnostic.Actual.Text, path.Name);
    }

}

// The paths write the depth image, which the graph declares, and the list does not produce it.
TEST(Paths, AnUndeclaredOutputIsLM309)
{
    PathCompile run;
    CompilePaths(PathGraphOptions{ .ProducesDepth = false }, k_WithDrawIndirectCount, {}, run);

    EXPECT_FALSE(run.Compiled.has_value());
    ASSERT_EQ(CountOf(run.Sink, loom::DiagnosticCode::LM309), 1);

    const loom::Diagnostic& diagnostic = *FirstOf(run.Sink, loom::DiagnosticCode::LM309);
    EXPECT_EQ(diagnostic.Resource.Hash, k_Depth.Hash);
    EXPECT_EQ(diagnostic.Pass.Hash, k_PrepassCount.Hash);
}

// The higher path's capability is Wanted, so a device without it still boots and takes the fallback. Both paths run
// the same entry points, so no entry point is Wanted only.
TEST(Paths, AHigherPathsCapabilityIsWanted)
{
    loom::GraphBuilder graph{ "Paths.Graph", "Paths.Library" };
    BuildPathGraph(graph, PathGraphOptions{});

    const loom::GraphDeviceNeeds needs = loom::GatherDeviceNeeds(graph, "Paths.ldmanifest");

    ASSERT_EQ(needs.Needs.size(), 1u);
    EXPECT_EQ(needs.Needs[0].Kind, bootstrap::NeedKind::NeutralCapability);
    EXPECT_EQ(needs.Needs[0].Neutral, DeviceCapability::DrawIndirectCount);
    EXPECT_EQ(needs.Needs[0].Level, bootstrap::NeedLevel::Wanted);
    EXPECT_EQ(needs.Needs[0].Source.Module, "Paths.Graph");
    EXPECT_TRUE(needs.WantedOnly.empty());
}

// The last path is the fallback, so its capability is Required: with only the DrawIndirectCount path, boot must fail
// on a device without it, not Compile.
TEST(Paths, TheFallbacksCapabilityIsRequired)
{
    loom::GraphBuilder graph{ "Paths.Graph", "Paths.Library" };
    BuildPathGraph(graph, PathGraphOptions{ .HasSinglePath = false });

    const loom::GraphDeviceNeeds needs = loom::GatherDeviceNeeds(graph, "Paths.ldmanifest");

    ASSERT_EQ(needs.Needs.size(), 1u);
    EXPECT_EQ(needs.Needs[0].Level, bootstrap::NeedLevel::Required);
}
