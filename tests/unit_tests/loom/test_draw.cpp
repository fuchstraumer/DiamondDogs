#include "Compile.hpp"
#include "CompiledGraph.hpp"
#include "DecideRuns.hpp"
#include "Declarations.hpp"
#include "FramePlan.hpp"
#include "GraphBuilder.hpp"
#include "LoomDeviceLimits.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "LoomFrame.hpp"
#include "LoomKeys.hpp"
#include "PassBuilders.hpp"
#include "PlanBarriers.hpp"
#include "ShaderBundle.hpp"
#include "ShaderLibrary.host.hpp"
#include "ShaderLibraryTypes.hpp"
#include "TargetExtents.hpp"
#include "compile/CompiledData.hpp"
#include "freeze_test_helpers.hpp"
#include "utility/Delegate.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>
#include <gtest/gtest.h>

// Step 7a, the core with no GPU: Draw and Record passes compiled to attachments and uses, frame-target rows, the target
// actions and fallbacks, the frame-end batch, the indirect draw statements, LoomFrame, and the three target extents.
// The graph is LoomDrawTest's: Cull writes the indirect arguments and the count; Prepass draws depth through
// DrawIndirectCount; GBuffer writes two colors and tests depth Equal; Light writes the frame target.

using namespace freeze_tests;

namespace
{

    namespace host = loom_test_shaders;

    constexpr loom::ResourceKey<loom::Buffer> k_Positions{ "Draw.Positions" };
    constexpr loom::ResourceKey<loom::Buffer> k_Arguments{ "Draw.Arguments" };
    constexpr loom::ResourceKey<loom::Buffer> k_DrawCount{ "Draw.DrawCount" };
    constexpr loom::ResourceKey<loom::Image> k_Depth{ "Draw.Depth" };
    constexpr loom::ResourceKey<loom::Image> k_GAlbedo{ "Draw.Albedo" };
    constexpr loom::ResourceKey<loom::Image> k_GNormal{ "Draw.Normal" };
    constexpr loom::ResourceKey<loom::Sampler> k_Point{ "Draw.Point" };
    constexpr loom::FrameTargetKey k_Screen{ "Draw.Screen" };
    constexpr loom::PassKey k_Cull{ "Draw.Cull" };
    constexpr loom::PassKey k_Prepass{ "Draw.Prepass" };
    constexpr loom::PassKey k_GBuffer{ "Draw.GBuffer" };
    constexpr loom::PassKey k_Light{ "Draw.Light" };

    // Declaration order is the pass index
    constexpr int32_t k_CullPass = 0;
    constexpr int32_t k_PrepassPass = 1;
    constexpr int32_t k_GBufferPass = 2;
    constexpr int32_t k_LightPass = 3;

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

    loom::CompileInputs Inputs()
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
            .Layers = 1,
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

    struct DrawGraphOptions
    {
        bool HasOutput{ true };
        loom::OutputFallback Fallback{ loom::OutputFallback::Clear(0.1f, 0.2f, 0.3f, 1.0f) };
        int64_t ArgumentBytes{ 16 };
        int64_t ArgumentOffset{ 0 };
        int32_t MaxDraws{ 1 };
    };

    void BuildDrawGraph(loom::GraphBuilder& graph, const DrawGraphOptions& options)
    {
        graph.Import(k_Screen);

        if (options.HasOutput)
        {
            graph.Output(loom::OutputDeclaration{ .Target = k_Screen, .OnSkip = options.Fallback });
        }

        graph.Import(loom::BufferImportDeclaration{ .Key = k_Positions, .ByteSize = 96 });
        graph.Declare(Bytes(k_Arguments, options.ArgumentBytes));
        graph.Declare(Bytes(k_DrawCount, 4));
        graph.Declare(TargetSized(k_Depth, lodestone::TextureFormat::Depth32Float));
        graph.Declare(TargetSized(k_GAlbedo, lodestone::TextureFormat::RGBA8Unorm));
        graph.Declare(TargetSized(k_GNormal, lodestone::TextureFormat::RGBA16Float));
        graph.Declare(loom::SamplerDeclaration{ .Key = k_Point, .CreateInfo = SamplerCreateInfo{} });

        const auto cull = [](loom::DispatchBuilder& pass)
        {
            pass.Shader("LoomDraw", "CullCS");
            pass.Bind("DrawArguments", k_Arguments, loom::AccessModifier::WriteOnly);
            pass.Bind("DrawCount", k_DrawCount, loom::AccessModifier::WriteOnly);
            pass.DispatchThreads(1, 1, 1);
        };

        const auto prepass = [&options](loom::DrawBuilder& pass)
        {
            pass.Shaders("LoomDraw", "SceneVS", "");
            pass.Bind("Positions", k_Positions);
            pass.Depth(k_Depth, loom::DepthLoad::Clear(1.0f), loom::AttachmentStore::Store, loom::CompareOp::Less,
                       loom::DepthWrite::Enabled);
            pass.DrawIndirectCount(k_Arguments, options.ArgumentOffset, k_DrawCount, 0, options.MaxDraws);
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

        graph.AddDispatch(k_Cull, k_EveryFrame, k_Graphics, cull);
        graph.AddDraw(k_Prepass, k_EveryFrame, k_Graphics, prepass);
        graph.AddDraw(k_GBuffer, k_EveryFrame, k_Graphics, gbuffer);
        graph.AddDraw(k_Light, k_EveryFrame, k_Graphics, light);
    }

    std::optional<loom::CompiledGraph> CompileDrawGraph(const DrawGraphOptions& options, loom::CollectingSink& sink)
    {
        loom::GraphBuilder graph{ "Draw.Graph", "Draw.Library" };
        BuildDrawGraph(graph, options);
        return loom::Compile(graph, Inputs(), sink);
    }

    std::string FirstDiagnostic(const loom::CollectingSink& sink)
    {
        if (sink.Diagnostics().empty())
        {
            return "no diagnostic";
        }

        return std::string{ loom::ToString(sink.Diagnostics().front().Code) };
    }

    int32_t RowOf(const loom::CompiledGraph& compiled, loom::KeyRef key)
    {
        return compiled.ResourceIndexOf(key);
    }

    int32_t ScreenRow(const loom::CompiledGraph& compiled)
    {
        return compiled.ResourceCount();
    }

    // Every row complete; the frame target hands back in `final_layout`
    std::vector<loom::ResourceState> ReadyStates(const loom::CompiledGraph& compiled, rhi::ImageLayout final_layout)
    {
        std::vector<loom::ResourceState> states(compiled.RowCount(),
                                                loom::ResourceState{ .Readiness = loom::ResourceReadiness::Complete, .Epoch = 1u });
        states[ScreenRow(compiled)].FinalLayout = final_layout;
        return states;
    }

    const loom::PassUse* UseOf(const loom::CompiledData& data, int32_t pass, int32_t row)
    {
        const loom::CompiledPass& compiled = data.Passes[pass];

        for (int32_t useIndex = compiled.FirstUse; useIndex < compiled.FirstUse + compiled.UseCount; ++useIndex)
        {
            if (data.Uses[useIndex].Resource == row)
            {
                return &data.Uses[useIndex];
            }
        }

        return nullptr;
    }

    const loom::CompiledAttachment* AttachmentOf(const loom::CompiledData& data, int32_t pass, int32_t row)
    {
        const loom::CompiledPass& compiled = data.Passes[pass];

        for (int32_t index = compiled.FirstAttachment; index < compiled.FirstAttachment + compiled.AttachmentCount; ++index)
        {
            if (data.Attachments[index].Row == row)
            {
                return &data.Attachments[index];
            }
        }

        return nullptr;
    }

    const loom::BarrierBatch* BatchBefore(const loom::FramePlan& plan, int32_t before_run)
    {
        const auto matches = [before_run](const loom::BarrierBatch& batch)
        {
            return batch.BeforeRun == before_run;
        };

        const auto found = std::ranges::find_if(plan.Batches, matches);
        return found == plan.Batches.end() ? nullptr : &*found;
    }

    const loom::PlannedBarrier* BarrierOf(const loom::FramePlan& plan, const loom::BarrierBatch& batch, int32_t copy)
    {
        for (int32_t index = batch.FirstBarrier; index < batch.FirstBarrier + batch.Count; ++index)
        {
            if (plan.Barriers[index].Resource == copy)
            {
                return &plan.Barriers[index];
            }
        }

        return nullptr;
    }

    // One Tick's decisions and barriers, from fresh sync state
    struct PlannedTick
    {
        loom::FramePlan Plan;
        std::vector<loom::ResourceSync> Sync;
    };

    PlannedTick PlanTick(const loom::CompiledGraph& compiled, std::span<const loom::ResourceState> states,
                         std::span<const loom::PassControl> controls)
    {
        PlannedTick tick;
        loom::PrepareFramePlan(compiled, tick.Plan);
        tick.Sync.assign(compiled.Data().CopyRows.size(), loom::ResourceSync{});
        loom::DecideRuns(compiled, states, controls, tick.Sync, 0, tick.Plan);
        loom::PlanBarriers(compiled, states, tick.Sync, 0, tick.Plan);
        return tick;
    }

    void RecordNothing(loom::RecordContext&)
    {
    }

}

// Compile: the tables of a draw

TEST(Draw, FrameTargetsGetRowsAfterTheResources)
{
    loom::CollectingSink sink;
    const std::optional<loom::CompiledGraph> compiled = CompileDrawGraph(DrawGraphOptions{}, sink);
    ASSERT_TRUE(compiled.has_value()) << FirstDiagnostic(sink);
    const loom::CompiledData& data = compiled->Data();

    EXPECT_EQ(compiled->RowCount(), compiled->ResourceCount() + 1);
    ASSERT_EQ(data.FrameTargets.size(), 1u);
    EXPECT_EQ(data.FrameTargets[0].Row, ScreenRow(*compiled));
    EXPECT_EQ(data.FrameTargets[0].Fallback.Kind, loom::OutputFallback::Kind::Clear);
    EXPECT_TRUE(loom::IsFrameTargetRow(data, ScreenRow(*compiled)));
    EXPECT_TRUE(loom::IsImageRow(data, ScreenRow(*compiled)));
    EXPECT_EQ(loom::KeyOfRow(data, ScreenRow(*compiled)).Name, k_Screen.Name);
    EXPECT_EQ(data.CopyCounts.size(), static_cast<size_t>(compiled->RowCount()));
    EXPECT_EQ(data.CopyRows.size(), static_cast<size_t>(compiled->RowCount()));
}

TEST(Draw, AttachmentsCarryRoleRowLoadAndClearValue)
{
    loom::CollectingSink sink;
    const std::optional<loom::CompiledGraph> compiled = CompileDrawGraph(DrawGraphOptions{}, sink);
    ASSERT_TRUE(compiled.has_value()) << FirstDiagnostic(sink);
    const loom::CompiledData& data = compiled->Data();
    const int32_t depthRow = RowOf(*compiled, k_Depth.ToRef());

    EXPECT_EQ(data.Passes[k_CullPass].AttachmentCount, 0);
    EXPECT_EQ(data.Passes[k_PrepassPass].Kind, loom::PassKind::Draw);
    EXPECT_EQ(data.Passes[k_PrepassPass].AttachmentCount, 1);
    EXPECT_EQ(data.Passes[k_GBufferPass].AttachmentCount, 3);
    EXPECT_EQ(data.Passes[k_LightPass].AttachmentCount, 1);

    const loom::CompiledAttachment* prepassDepth = AttachmentOf(data, k_PrepassPass, depthRow);
    ASSERT_NE(prepassDepth, nullptr);
    EXPECT_EQ(prepassDepth->Role, loom::AttachmentRole::Depth);
    EXPECT_EQ(prepassDepth->Load, loom::AttachmentLoad::Clear);
    EXPECT_EQ(prepassDepth->ClearValue.x, 1.0f);

    const loom::CompiledAttachment* normal = AttachmentOf(data, k_GBufferPass, RowOf(*compiled, k_GNormal.ToRef()));
    ASSERT_NE(normal, nullptr);
    EXPECT_EQ(normal->Role, loom::AttachmentRole::Color);
    EXPECT_EQ(normal->Slot, 1);
    EXPECT_EQ(normal->Store, loom::AttachmentStore::Store);

    const loom::CompiledAttachment* screen = AttachmentOf(data, k_LightPass, ScreenRow(*compiled));
    ASSERT_NE(screen, nullptr);
    EXPECT_EQ(screen->Role, loom::AttachmentRole::Color);
    EXPECT_EQ(screen->Load, loom::AttachmentLoad::DontCare);
}

TEST(Draw, DepthUsesFollowTheWriteStatement)
{
    loom::CollectingSink sink;
    const std::optional<loom::CompiledGraph> compiled = CompileDrawGraph(DrawGraphOptions{}, sink);
    ASSERT_TRUE(compiled.has_value()) << FirstDiagnostic(sink);
    const loom::CompiledData& data = compiled->Data();
    const int32_t depthRow = RowOf(*compiled, k_Depth.ToRef());
    const rhi::PipelineStage tests = rhi::PipelineStage::EarlyFragmentTests | rhi::PipelineStage::LateFragmentTests;

    // a Clear with writes on: a write, in the writable layout
    const loom::PassUse* written = UseOf(data, k_PrepassPass, depthRow);
    ASSERT_NE(written, nullptr);
    EXPECT_EQ(written->Access, loom::ResourceAccess::Write);
    EXPECT_TRUE(written->Initializes);
    EXPECT_EQ(written->Layout, rhi::ImageLayout::DepthAttachment);
    EXPECT_EQ(written->Scope.Stages, tests);
    EXPECT_EQ(written->Scope.Accesses, rhi::Access::DepthStencilAttachmentRead | rhi::Access::DepthStencilAttachmentWrite);

    // Keep with writes off: a depth test only, a read in the read-only layout
    const loom::PassUse* tested = UseOf(data, k_GBufferPass, depthRow);
    ASSERT_NE(tested, nullptr);
    EXPECT_EQ(tested->Access, loom::ResourceAccess::Read);
    EXPECT_EQ(tested->Layout, rhi::ImageLayout::DepthReadOnly);
    EXPECT_EQ(tested->Scope.Accesses, rhi::Access::DepthStencilAttachmentRead);
}

TEST(Draw, ColorTargetUsesAndIndirectReads)
{
    loom::CollectingSink sink;
    const std::optional<loom::CompiledGraph> compiled = CompileDrawGraph(DrawGraphOptions{}, sink);
    ASSERT_TRUE(compiled.has_value()) << FirstDiagnostic(sink);
    const loom::CompiledData& data = compiled->Data();
    const loom::SyncScope indirect{ .Stages = rhi::PipelineStage::DrawIndirect, .Accesses = rhi::Access::IndirectCommandRead };

    const loom::PassUse* screen = UseOf(data, k_LightPass, ScreenRow(*compiled));
    ASSERT_NE(screen, nullptr);
    EXPECT_EQ(screen->Access, loom::ResourceAccess::Write);
    EXPECT_TRUE(screen->Initializes);
    EXPECT_EQ(screen->Layout, rhi::ImageLayout::ColorAttachment);
    EXPECT_EQ(screen->Scope.Stages, rhi::PipelineStage::ColorAttachmentOutput);
    EXPECT_EQ(screen->Scope.Accesses, rhi::Access::ColorAttachmentWrite);

    const loom::PassUse* arguments = UseOf(data, k_PrepassPass, RowOf(*compiled, k_Arguments.ToRef()));
    const loom::PassUse* count = UseOf(data, k_PrepassPass, RowOf(*compiled, k_DrawCount.ToRef()));
    ASSERT_NE(arguments, nullptr);
    ASSERT_NE(count, nullptr);
    EXPECT_EQ(arguments->Scope, indirect);
    EXPECT_EQ(count->Scope, indirect);
    EXPECT_EQ(count->Access, loom::ResourceAccess::Read);
}

TEST(Draw, LoomFrameBindsWithNoBindStatement)
{
    loom::CollectingSink sink;
    const std::optional<loom::CompiledGraph> compiled = CompileDrawGraph(DrawGraphOptions{}, sink);
    ASSERT_TRUE(compiled.has_value()) << FirstDiagnostic(sink);
    EXPECT_EQ(sink.ErrorCount(), 0);
    const loom::CompiledData& data = compiled->Data();
    const loom::BoundPass& light = data.BoundPasses[k_LightPass];
    int32_t builtIns = 0;

    for (int32_t slotIndex = light.FirstSlot; slotIndex < light.FirstSlot + light.SlotCount; ++slotIndex)
    {
        const loom::BindingSlot& slot = data.Slots[slotIndex];

        if (slot.BuiltIn == loom::BuiltInBinding::LoomFrame)
        {
            ++builtIns;
            EXPECT_FALSE(slot.Resource.IsValid());
            EXPECT_EQ(slot.Kind, lodestone::BindingKind::UniformBuffer);
            EXPECT_EQ(slot.Access, loom::ResourceAccess::Read);
        }
    }

    EXPECT_EQ(builtIns, 1);
}

TEST(Draw, LoomFrameMirrorMatchesTheSlangLayout)
{
    using Cooked = host::LoomFrame<host::CookProfile::Spirv, lodestone::LayoutRule::Std140>;
    using CookedTarget = host::LoomTargetInfo<host::CookProfile::Spirv, lodestone::LayoutRule::Std140>;

    static_assert(sizeof(Cooked) == sizeof(loom::LoomFrameConstants));
    static_assert(offsetof(Cooked, Targets) == offsetof(loom::LoomFrameConstants, Targets));
    static_assert(sizeof(CookedTarget) == sizeof(loom::LoomTargetInfo));
    static_assert(offsetof(CookedTarget, RenderExtent) == offsetof(loom::LoomTargetInfo, RenderExtent));
    static_assert(offsetof(CookedTarget, RenderToAllocation) == offsetof(loom::LoomTargetInfo, RenderToAllocation));
    SUCCEED();
}

TEST(Draw, TheDrawCountOrdersItsWriterFirst)
{
    loom::CollectingSink sink;
    const std::optional<loom::CompiledGraph> compiled = CompileDrawGraph(DrawGraphOptions{}, sink);
    ASSERT_TRUE(compiled.has_value()) << FirstDiagnostic(sink);
    const int32_t countRow = RowOf(*compiled, k_DrawCount.ToRef());

    // the count buffer has its own data-flow edge, not only the one through the arguments
    const auto countEdge = [countRow](const loom::OrderEdge& edge)
    {
        return edge.Reason == loom::EdgeReason::DataFlow && edge.Resource.Value == countRow &&
               edge.From.Value == k_CullPass && edge.To.Value == k_PrepassPass;
    };

    EXPECT_TRUE(std::ranges::any_of(compiled->Data().Ordered.Edges, countEdge));
}

// Freeze and Size: the indirect statements

TEST(Draw, IndirectCountOffsetsAndMaxDrawsAreChecked)
{
    DrawGraphOptions options{};
    options.ArgumentOffset = 2;
    options.MaxDraws = 0;
    loom::CollectingSink sink;

    EXPECT_FALSE(CompileDrawGraph(options, sink).has_value());
    ASSERT_EQ(sink.Diagnostics().size(), 2u);
    EXPECT_EQ(sink.Diagnostics()[0].Code, loom::DiagnosticCode::LM109);
    EXPECT_EQ(sink.Diagnostics()[1].Code, loom::DiagnosticCode::LM109);
}

TEST(Draw, IndirectCountNeedsRoomForEveryDraw)
{
    // two draws of 16 bytes do not fit in 16 bytes
    DrawGraphOptions options{};
    options.MaxDraws = 2;
    loom::CollectingSink sink;

    EXPECT_FALSE(CompileDrawGraph(options, sink).has_value());
    EXPECT_EQ(FirstDiagnostic(sink), "LM401");

    options.ArgumentBytes = 32;
    loom::CollectingSink fits;
    EXPECT_TRUE(CompileDrawGraph(options, fits).has_value()) << FirstDiagnostic(fits);
}

// DecideRuns: what each frame target needs

TEST(Draw, ATargetThatAPassWritesRenders)
{
    loom::CollectingSink sink;
    const std::optional<loom::CompiledGraph> compiled = CompileDrawGraph(DrawGraphOptions{}, sink);
    ASSERT_TRUE(compiled.has_value()) << FirstDiagnostic(sink);
    const std::vector<loom::ResourceState> states = ReadyStates(*compiled, rhi::ImageLayout::PresentSource);
    const std::vector<loom::PassControl> controls(compiled->PassCount());

    const PlannedTick tick = PlanTick(*compiled, states, controls);

    EXPECT_EQ(tick.Plan.Runs.size(), 4u);
    ASSERT_EQ(tick.Plan.TargetActions.size(), 1u);
    EXPECT_EQ(tick.Plan.TargetActions[0], loom::TargetAction::Render);
    EXPECT_TRUE(tick.Plan.Clears.empty());
}

TEST(Draw, ASkippedWriterGivesTheClearFallback)
{
    loom::CollectingSink sink;
    const std::optional<loom::CompiledGraph> compiled = CompileDrawGraph(DrawGraphOptions{}, sink);
    ASSERT_TRUE(compiled.has_value()) << FirstDiagnostic(sink);
    const std::vector<loom::ResourceState> states = ReadyStates(*compiled, rhi::ImageLayout::PresentSource);
    std::vector<loom::PassControl> controls(compiled->PassCount());
    controls[k_LightPass].Enabled = false;

    const PlannedTick tick = PlanTick(*compiled, states, controls);

    EXPECT_EQ(tick.Plan.TargetActions[0], loom::TargetAction::Clear);
    ASSERT_EQ(tick.Plan.Clears.size(), 1u);
    EXPECT_EQ(tick.Plan.Clears[0], ScreenRow(*compiled));
    EXPECT_NE(loom::ExplainLastTick(*compiled, tick.Plan).find("cleared to its fallback color"), std::string::npos);
}

TEST(Draw, KeepLastAndNoOutputDoNotAcquire)
{
    DrawGraphOptions keepLast{};
    keepLast.Fallback = loom::OutputFallback::KeepLast();
    DrawGraphOptions noOutput{};
    noOutput.HasOutput = false;

    for (const DrawGraphOptions& options : { keepLast, noOutput })
    {
        loom::CollectingSink sink;
        const std::optional<loom::CompiledGraph> compiled = CompileDrawGraph(options, sink);
        ASSERT_TRUE(compiled.has_value()) << FirstDiagnostic(sink);
        const std::vector<loom::ResourceState> states = ReadyStates(*compiled, rhi::ImageLayout::PresentSource);
        std::vector<loom::PassControl> controls(compiled->PassCount());
        controls[k_LightPass].Enabled = false;

        const PlannedTick tick = PlanTick(*compiled, states, controls);

        EXPECT_EQ(tick.Plan.TargetActions[0], loom::TargetAction::Skip);
        EXPECT_TRUE(tick.Plan.Clears.empty());
        EXPECT_EQ(BatchBefore(tick.Plan, loom::k_AfterRuns), nullptr);
    }
}

// PlanBarriers: the frame-end batch, target clears and attachment layouts

TEST(Draw, TheFrameEndBatchMovesTheTargetToItsFinalLayout)
{
    loom::CollectingSink sink;
    const std::optional<loom::CompiledGraph> compiled = CompileDrawGraph(DrawGraphOptions{}, sink);
    ASSERT_TRUE(compiled.has_value()) << FirstDiagnostic(sink);
    const std::vector<loom::ResourceState> states = ReadyStates(*compiled, rhi::ImageLayout::PresentSource);
    const std::vector<loom::PassControl> controls(compiled->PassCount());

    const PlannedTick tick = PlanTick(*compiled, states, controls);

    ASSERT_FALSE(tick.Plan.Batches.empty());
    EXPECT_EQ(tick.Plan.Batches.back().BeforeRun, loom::k_AfterRuns);
    const loom::PlannedBarrier* present = BarrierOf(tick.Plan, tick.Plan.Batches.back(), ScreenRow(*compiled));
    ASSERT_NE(present, nullptr);
    EXPECT_EQ(present->OldLayout, rhi::ImageLayout::ColorAttachment);
    EXPECT_EQ(present->NewLayout, rhi::ImageLayout::PresentSource);
    EXPECT_EQ(present->Before.Stages, rhi::PipelineStage::ColorAttachmentOutput);
    EXPECT_EQ(present->Before.Accesses, rhi::Access::ColorAttachmentWrite);
    EXPECT_EQ(tick.Sync[ScreenRow(*compiled)].Layout, rhi::ImageLayout::PresentSource);

    // the first use of the acquired image starts from Undefined
    const loom::BarrierBatch* lightBatch = BatchBefore(tick.Plan, 3);
    ASSERT_NE(lightBatch, nullptr);
    const loom::PlannedBarrier* acquire = BarrierOf(tick.Plan, *lightBatch, ScreenRow(*compiled));
    ASSERT_NE(acquire, nullptr);
    EXPECT_EQ(acquire->OldLayout, rhi::ImageLayout::Undefined);
    EXPECT_EQ(acquire->NewLayout, rhi::ImageLayout::ColorAttachment);
}

TEST(Draw, AnOffscreenTargetWithNoFinalLayoutHasNoFrameEndBatch)
{
    loom::CollectingSink sink;
    const std::optional<loom::CompiledGraph> compiled = CompileDrawGraph(DrawGraphOptions{}, sink);
    ASSERT_TRUE(compiled.has_value()) << FirstDiagnostic(sink);
    const std::vector<loom::ResourceState> states = ReadyStates(*compiled, rhi::ImageLayout::Undefined);
    const std::vector<loom::PassControl> controls(compiled->PassCount());

    const PlannedTick tick = PlanTick(*compiled, states, controls);

    EXPECT_EQ(BatchBefore(tick.Plan, loom::k_AfterRuns), nullptr);
}

TEST(Draw, AClearFallbackClearsInGeneralThenPresents)
{
    loom::CollectingSink sink;
    const std::optional<loom::CompiledGraph> compiled = CompileDrawGraph(DrawGraphOptions{}, sink);
    ASSERT_TRUE(compiled.has_value()) << FirstDiagnostic(sink);
    const std::vector<loom::ResourceState> states = ReadyStates(*compiled, rhi::ImageLayout::PresentSource);
    std::vector<loom::PassControl> controls(compiled->PassCount());
    controls[k_LightPass].Enabled = false;

    const PlannedTick tick = PlanTick(*compiled, states, controls);

    const loom::BarrierBatch* clears = BatchBefore(tick.Plan, loom::k_BeforeClears);
    ASSERT_NE(clears, nullptr);
    const loom::PlannedBarrier* toClear = BarrierOf(tick.Plan, *clears, ScreenRow(*compiled));
    ASSERT_NE(toClear, nullptr);
    EXPECT_EQ(toClear->NewLayout, rhi::ImageLayout::General);
    EXPECT_EQ(toClear->After.Accesses, rhi::Access::TransferWrite);

    const loom::BarrierBatch* frameEnd = BatchBefore(tick.Plan, loom::k_AfterRuns);
    ASSERT_NE(frameEnd, nullptr);
    const loom::PlannedBarrier* present = BarrierOf(tick.Plan, *frameEnd, ScreenRow(*compiled));
    ASSERT_NE(present, nullptr);
    EXPECT_EQ(present->OldLayout, rhi::ImageLayout::General);
    EXPECT_EQ(present->NewLayout, rhi::ImageLayout::PresentSource);
    EXPECT_EQ(present->Before.Accesses, rhi::Access::TransferWrite);
}

TEST(Draw, DepthMovesToReadOnlyBetweenPrepassAndGBuffer)
{
    loom::CollectingSink sink;
    const std::optional<loom::CompiledGraph> compiled = CompileDrawGraph(DrawGraphOptions{}, sink);
    ASSERT_TRUE(compiled.has_value()) << FirstDiagnostic(sink);
    const std::vector<loom::ResourceState> states = ReadyStates(*compiled, rhi::ImageLayout::PresentSource);
    const std::vector<loom::PassControl> controls(compiled->PassCount());
    const int32_t depthRow = RowOf(*compiled, k_Depth.ToRef());

    const PlannedTick tick = PlanTick(*compiled, states, controls);

    // runs: Cull 0, Prepass 1, GBuffer 2, Light 3, one level each
    const loom::BarrierBatch* gbufferBatch = BatchBefore(tick.Plan, 2);
    ASSERT_NE(gbufferBatch, nullptr);
    const loom::PlannedBarrier* depth = BarrierOf(tick.Plan, *gbufferBatch, depthRow);
    ASSERT_NE(depth, nullptr);
    EXPECT_EQ(depth->OldLayout, rhi::ImageLayout::DepthAttachment);
    EXPECT_EQ(depth->NewLayout, rhi::ImageLayout::DepthReadOnly);
    EXPECT_NE(depth->Before.Accesses & rhi::Access::DepthStencilAttachmentWrite, rhi::Access::None);

    // the culling writes reach the indirect reads
    const loom::BarrierBatch* prepassBatch = BatchBefore(tick.Plan, 1);
    ASSERT_NE(prepassBatch, nullptr);
    const loom::PlannedBarrier* arguments = BarrierOf(tick.Plan, *prepassBatch, RowOf(*compiled, k_Arguments.ToRef()));
    ASSERT_NE(arguments, nullptr);
    EXPECT_EQ(arguments->Before.Stages, rhi::PipelineStage::ComputeShader);
    EXPECT_EQ(arguments->After.Stages, rhi::PipelineStage::DrawIndirect);
    EXPECT_EQ(arguments->After.Accesses, rhi::Access::IndirectCommandRead);
}

// Record passes: uses and attachments from the Uses statements

TEST(Draw, RecordTargetUsesBecomeKeepAndStoreAttachments)
{
    loom::GraphBuilder graph{ "Draw.Record", "Draw.Library" };
    graph.Import(k_Screen);
    graph.Output(loom::OutputDeclaration{ .Target = k_Screen, .OnSkip = loom::OutputFallback::KeepLast() });
    graph.Declare(TargetSized(k_GAlbedo, lodestone::TextureFormat::RGBA8Unorm));

    const auto paint = [](loom::DrawBuilder& pass)
    {
        pass.Shaders("LoomDraw", "SceneVS", "GBufferPS");
        pass.Bind("Positions", k_Positions);
        pass.Color(0, k_GAlbedo, loom::ColorLoad::DontCare(), loom::AttachmentStore::Store, loom::BlendMode::Opaque);
        pass.Color(1, k_Screen, loom::ColorLoad::DontCare(), loom::AttachmentStore::Store, loom::BlendMode::Opaque);
        pass.Draw(3, 1, 0, 0);
    };

    const auto overlay = [](loom::RecordBuilder& pass)
    {
        pass.Uses(k_GAlbedo, loom::ResourceAccess::Read, loom::ImageUse::Sampled);
        pass.Uses(k_Screen, loom::ResourceAccess::ReadWrite, loom::ImageUse::ColorTarget);
        pass.Record(Delegate<void(loom::RecordContext&)>::Create<&RecordNothing>());
    };

    graph.Import(loom::BufferImportDeclaration{ .Key = k_Positions, .ByteSize = 96 });
    graph.AddDraw(k_GBuffer, k_EveryFrame, k_Graphics, paint);
    graph.AddRecord(k_Light, k_EveryFrame, k_Graphics, overlay);

    loom::CollectingSink sink;
    const std::optional<loom::CompiledGraph> compiled = loom::Compile(graph, Inputs(), sink);
    ASSERT_TRUE(compiled.has_value()) << FirstDiagnostic(sink);
    const loom::CompiledData& data = compiled->Data();
    constexpr int32_t k_RecordPass = 1;

    EXPECT_EQ(data.Passes[k_RecordPass].Kind, loom::PassKind::Record);
    ASSERT_EQ(data.Passes[k_RecordPass].AttachmentCount, 1);
    const loom::CompiledAttachment& attachment = data.Attachments[data.Passes[k_RecordPass].FirstAttachment];
    EXPECT_EQ(attachment.Role, loom::AttachmentRole::Color);
    EXPECT_EQ(attachment.Row, ScreenRow(*compiled));
    EXPECT_EQ(attachment.Slot, 0);
    EXPECT_EQ(attachment.Load, loom::AttachmentLoad::Keep);
    EXPECT_EQ(attachment.Store, loom::AttachmentStore::Store);

    const loom::PassUse* sampled = UseOf(data, k_RecordPass, RowOf(*compiled, k_GAlbedo.ToRef()));
    ASSERT_NE(sampled, nullptr);
    EXPECT_EQ(sampled->Layout, rhi::ImageLayout::ShaderReadOnly);
    EXPECT_EQ(sampled->Scope.Accesses, rhi::Access::ShaderSampledRead);

    const loom::PassUse* screen = UseOf(data, k_RecordPass, ScreenRow(*compiled));
    ASSERT_NE(screen, nullptr);
    EXPECT_EQ(screen->Layout, rhi::ImageLayout::ColorAttachment);
    EXPECT_EQ(screen->Scope.Accesses, rhi::Access::ColorAttachmentRead | rhi::Access::ColorAttachmentWrite);
}

// The three target extents (step 7, round 2 Q1)

namespace
{

    constexpr double k_Settle = 0.15;
    constexpr double k_Frame = 1.0 / 60.0;

    loom::TargetExtents Allocated(loom::Extent2D extent)
    {
        loom::TargetExtents state;
        EXPECT_EQ(loom::UpdateTargetExtents(state, extent, k_Frame, k_Settle, false), loom::ExtentChange::Reallocate);
        return state;
    }

}

TEST(TargetExtents, TheFirstReportAllocatesAtOnce)
{
    loom::TargetExtents state;
    EXPECT_EQ(loom::UpdateTargetExtents(state, loom::Extent2D{ 1280, 720 }, k_Frame, k_Settle, false),
              loom::ExtentChange::Reallocate);
    EXPECT_EQ(state.Allocation, (loom::Extent2D{ 1280, 720 }));
    EXPECT_EQ(state.Render, (loom::Extent2D{ 1280, 720 }));
}

TEST(TargetExtents, ADragDoesNotReallocateUntilItSettles)
{
    loom::TargetExtents state = Allocated(loom::Extent2D{ 1280, 720 });

    // the window grows a little every frame: the scene renders at the old allocation
    for (int32_t frame = 1; frame <= 30; ++frame)
    {
        const loom::Extent2D reported{ 1280 + frame * 4, 720 + frame * 2 };
        EXPECT_NE(loom::UpdateTargetExtents(state, reported, k_Frame, k_Settle, false), loom::ExtentChange::Reallocate);
        EXPECT_EQ(state.Allocation, (loom::Extent2D{ 1280, 720 }));
        EXPECT_EQ(state.Render, (loom::Extent2D{ 1280, 720 }));
    }

    // still: 9 frames are 0.15 s, so the 9th reallocates once
    int32_t reallocations = 0;

    for (int32_t frame = 0; frame < 12; ++frame)
    {
        const loom::ExtentChange change = loom::UpdateTargetExtents(state, loom::Extent2D{ 1400, 780 }, k_Frame, k_Settle, false);
        reallocations += change == loom::ExtentChange::Reallocate ? 1 : 0;
    }

    EXPECT_EQ(reallocations, 1);
    EXPECT_EQ(state.Allocation, (loom::Extent2D{ 1400, 780 }));
    EXPECT_EQ(state.Render, (loom::Extent2D{ 1400, 780 }));
}

TEST(TargetExtents, AShrinkRendersIntoPartOfTheOldImages)
{
    loom::TargetExtents state = Allocated(loom::Extent2D{ 1280, 720 });

    EXPECT_EQ(loom::UpdateTargetExtents(state, loom::Extent2D{ 800, 600 }, k_Frame, k_Settle, false),
              loom::ExtentChange::Render);
    EXPECT_EQ(state.Allocation, (loom::Extent2D{ 1280, 720 }));
    EXPECT_EQ(state.Render, (loom::Extent2D{ 800, 600 }));
}

TEST(TargetExtents, TheResizeFinishedHintSettlesAtOnce)
{
    loom::TargetExtents state = Allocated(loom::Extent2D{ 1280, 720 });

    EXPECT_EQ(loom::UpdateTargetExtents(state, loom::Extent2D{ 1920, 1080 }, k_Frame, k_Settle, true),
              loom::ExtentChange::Reallocate);
    EXPECT_EQ(state.Allocation, (loom::Extent2D{ 1920, 1080 }));
}

TEST(TargetExtents, AnEmptyTargetNeverBecomesTheAllocation)
{
    loom::TargetExtents state = Allocated(loom::Extent2D{ 1280, 720 });

    for (int32_t frame = 0; frame < 60; ++frame)
    {
        EXPECT_NE(loom::UpdateTargetExtents(state, loom::Extent2D{ 0, 0 }, k_Frame, k_Settle, true),
                  loom::ExtentChange::Reallocate);
    }

    EXPECT_EQ(state.Allocation, (loom::Extent2D{ 1280, 720 }));
    EXPECT_TRUE(state.Render.IsEmpty());

    // restored: the old allocation fits, so nothing reallocates
    EXPECT_EQ(loom::UpdateTargetExtents(state, loom::Extent2D{ 1280, 720 }, k_Frame, k_Settle, false),
              loom::ExtentChange::Render);
}

TEST(TargetExtents, RenderScaleChangesOnlyTheRenderExtent)
{
    loom::TargetExtents state = Allocated(loom::Extent2D{ 1280, 720 });

    EXPECT_TRUE(loom::SetRenderScale(state, 0.5f));
    EXPECT_EQ(state.Render, (loom::Extent2D{ 640, 360 }));
    EXPECT_EQ(state.Allocation, (loom::Extent2D{ 1280, 720 }));

    EXPECT_FALSE(loom::SetRenderScale(state, 0.0f));
    EXPECT_FALSE(loom::SetRenderScale(state, 1.5f));
    EXPECT_EQ(state.RenderScale, 0.5f);
}

TEST(TargetExtents, ScaledExtentRoundsAndKeepsOnePixel)
{
    EXPECT_EQ(loom::ScaledExtent(loom::Extent2D{ 1281, 721 }, 0.5f), (loom::Extent2D{ 641, 361 }));
    EXPECT_EQ(loom::ScaledExtent(loom::Extent2D{ 3, 3 }, 0.1f), (loom::Extent2D{ 1, 1 }));
}
