#include "Compile.hpp"
#include "CompiledGraph.hpp"
#include "DecideRuns.hpp"
#include "LoomDeviceLimits.hpp"
#include "PlanBarriers.hpp"
#include "Declarations.hpp"
#include "FramePlan.hpp"
#include "GraphBuilder.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "LoomKeys.hpp"
#include "ParamsStore.hpp"
#include "PassBuilders.hpp"
#include "ShaderBundle.hpp"
#include "ShaderLibrary.host.hpp"
#include "ShaderLibraryTypes.hpp"
#include "compile/CompiledData.hpp"
#include "freeze_test_helpers.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <source_location>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include <gtest/gtest.h>

// The core runtime with no GPU (spec step 5): Compile's tables, DecideRuns, PlanBarriers and the params store, on the
// two passes of LoomComputeTest. HashFillCS writes Output (level 0); HashAgainCS reads it and writes Output2 (level 1).
// The readiness values are written by hand, as the backend would poll them.

using namespace freeze_tests;

namespace
{

    namespace host = loom_test_shaders;
    using FillParams = host::LoomHashFill::HashFillCS::Params<host::CookProfile::Spirv>;
    using AgainParams = host::LoomHashFill::HashAgainCS::Params<host::CookProfile::Spirv>;

    constexpr uint32_t k_Count = 4099u;
    constexpr int64_t k_Bytes = int64_t{ k_Count } * 4;

    constexpr loom::ResourceKey<loom::Buffer> k_Output{ "Runtime.Output" };
    constexpr loom::ResourceKey<loom::Buffer> k_Output2{ "Runtime.Output2" };
    constexpr loom::PassKey k_Fill{ "Runtime.Fill" };
    constexpr loom::PassKey k_Again{ "Runtime.Again" };
    constexpr loom::ParamsKey<FillParams> k_FillParams{ "Runtime.FillParams" };
    constexpr loom::ParamsKey<AgainParams> k_AgainParams{ "Runtime.AgainParams" };

    // Declaration order is the pass index: Fill is 0, Again is 1. Output is row 0 (imported), Output2 row 1.
    constexpr int32_t k_FillPass = 0;
    constexpr int32_t k_AgainPass = 1;

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

    void BuildHashGraph(loom::GraphBuilder& graph, loom::RunPolicy fill_policy, loom::RunPolicy again_policy)
    {
        graph.Pin("HASH_TWICE", loom::AxisValue::Boolean(false));
        graph.Import(loom::BufferImportDeclaration{ .Key = k_Output, .ByteSize = k_Bytes });
        graph.Declare(loom::BufferDeclaration
        {
            .Key = k_Output2,
            .Lifetime = loom::ResourceLifetime::Frame,
            .Size = loom::SizeSource::Bytes(k_Bytes),
            .Cpu = loom::CpuAccess::Readback,
            .HistoryDepth = 0,
            .NoHistoryAction = loom::OnNoHistory::Invalid
        });

        const auto fill = [](loom::DispatchBuilder& pass)
        {
            pass.Shader("LoomHashFill", "HashFillCS");
            pass.Bind("Output", k_Output, loom::AccessModifier::WriteOnly);
            pass.Params(k_FillParams);
            pass.DispatchThreads(static_cast<int32_t>(k_Count), 1, 1);
        };

        const auto again = [](loom::DispatchBuilder& pass)
        {
            pass.Shader("LoomHashFill", "HashAgainCS");
            pass.Bind("Hashed", k_Output);
            pass.Bind("Output2", k_Output2, loom::AccessModifier::WriteOnly);
            pass.Params(k_AgainParams);
            pass.DispatchThreads(static_cast<int32_t>(k_Count), 1, 1);
        };

        graph.AddDispatch(k_Fill, fill_policy, k_Graphics, fill);
        graph.AddDispatch(k_Again, again_policy, k_Graphics, again);
    }

    std::optional<loom::CompiledGraph> CompileHashGraphWith(loom::RunPolicy fill_policy,
                                                            loom::RunPolicy again_policy,
                                                            loom::CollectingSink& sink)
    {
        loom::GraphBuilder graph{ "Runtime.Graph", "Runtime.Library" };
        BuildHashGraph(graph, fill_policy, again_policy);
        return loom::Compile(graph, Inputs(), sink);
    }

    std::optional<loom::CompiledGraph> CompileHashGraph(loom::RunPolicy fill_policy, loom::CollectingSink& sink)
    {
        return CompileHashGraphWith(fill_policy, k_EveryFrame, sink);
    }

    // One Tick's decisions and the commit after its submit. Returns the passes that ran.
    std::vector<int32_t> DecideAndCommit(const loom::CompiledGraph& compiled,
                                         std::span<const loom::ResourceState> resources,
                                         std::span<loom::PassControl> controls,
                                         loom::FramePlan& plan)
    {
        loom::DecideRuns(compiled, resources, controls, {}, 0, plan);
        loom::CommitRuns(compiled, plan, controls);
        return plan.Runs;
    }

    std::vector<loom::ResourceState> AllComplete(const loom::CompiledGraph& compiled)
    {
        return std::vector<loom::ResourceState>(compiled.ResourceCount(),
                                                loom::ResourceState{ .Readiness = loom::ResourceReadiness::Complete, .Epoch = 1u });
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

    // Step 6: images, samplers and history rings, on LoomImageTest's shaders. Paint writes Painted (storage); Sample
    // samples it and the imported Lut; Accumulate reads last frame's Counter and writes this frame's.
    using PaintParams = host::LoomImages::PaintCS::Params<host::CookProfile::Spirv>;
    using SampleParams = host::LoomImages::SampleCS::Params<host::CookProfile::Spirv>;
    using AccumulateParams = host::LoomImages::AccumulateCS::Params<host::CookProfile::Spirv>;

    constexpr loom::ResourceKey<loom::Image> k_Painted{ "Images.Painted" };
    constexpr loom::ResourceKey<loom::Image> k_Lut{ "Images.Lut" };
    constexpr loom::ResourceKey<loom::Sampler> k_Point{ "Images.Point" };
    constexpr loom::ResourceKey<loom::Buffer> k_Sampled{ "Images.Sampled" };
    constexpr loom::ResourceKey<loom::Buffer> k_Counter{ "Images.Counter" };
    constexpr loom::PassKey k_Paint{ "Images.Paint" };
    constexpr loom::PassKey k_Sample{ "Images.Sample" };
    constexpr loom::PassKey k_Accumulate{ "Images.Accumulate" };
    constexpr loom::ParamsKey<PaintParams> k_PaintParams{ "Images.PaintParams" };
    constexpr loom::ParamsKey<SampleParams> k_SampleParams{ "Images.SampleParams" };
    constexpr loom::ParamsKey<AccumulateParams> k_AccumulateParams{ "Images.AccumulateParams" };

    // Declaration order is the pass index
    constexpr int32_t k_PaintPass = 0;
    constexpr int32_t k_SamplePass = 1;
    constexpr int32_t k_AccumulatePass = 2;

    std::optional<loom::CompiledGraph> CompileImageGraph(loom::OnNoHistory counter_no_history, loom::CollectingSink& sink)
    {
        constexpr int32_t k_Size = 32;
        loom::GraphBuilder graph{ "Images.Graph", "Images.Library" };

        graph.Declare(loom::ImageDeclaration
        {
            .Key = k_Painted,
            .Lifetime = loom::ResourceLifetime::Persistent,
            .Format = lodestone::TextureFormat::RGBA32Float,
            .Extent = loom::ExtentSource::Fixed(k_Size, k_Size, 1),
            .Mips = loom::MipSource::One(),
            .Layers = loom::LayerSource::Fixed(1),
            .Cpu = loom::CpuAccess::None,
            .HistoryDepth = 0,
            .NoHistoryAction = loom::OnNoHistory::Invalid,
        });

        graph.Import(loom::ImportDeclaration{ .Key = k_Lut,
                                              .Expectation = loom::ImageExpectation::AnyExtent(lodestone::TextureFormat::RGBA32Float, 1) });
        graph.Declare(loom::SamplerDeclaration{ .Key = k_Point, .CreateInfo = SamplerCreateInfo{} });
        graph.Declare(loom::BufferDeclaration
        {
            .Key = k_Sampled,
            .Lifetime = loom::ResourceLifetime::Frame,
            .Size = loom::SizeSource::Bytes(int64_t{ k_Size } * k_Size * 16),
            .Cpu = loom::CpuAccess::Readback,
            .HistoryDepth = 0,
            .NoHistoryAction = loom::OnNoHistory::Invalid,
        });
        graph.Declare(loom::BufferDeclaration
        {
            .Key = k_Counter,
            .Lifetime = loom::ResourceLifetime::Persistent,
            .Size = loom::SizeSource::Bytes(int64_t{ 1024 } * 4),
            .Cpu = loom::CpuAccess::Readback,
            .HistoryDepth = 1,
            .NoHistoryAction = counter_no_history,
        });

        const auto paint = [](loom::DispatchBuilder& pass)
        {
            pass.Shader("LoomImages", "PaintCS");
            pass.Bind("Painted", k_Painted, loom::AccessModifier::WriteOnly);
            pass.Params(k_PaintParams);
            pass.DispatchThreads(k_Size, k_Size, 1);
        };

        const auto sample = [](loom::DispatchBuilder& pass)
        {
            pass.Shader("LoomImages", "SampleCS");
            pass.Bind("PaintedRead", k_Painted);
            pass.Bind("Lut", k_Lut);
            pass.Bind("PointSampler", k_Point);
            pass.Bind("Sampled", k_Sampled, loom::AccessModifier::WriteOnly);
            pass.Params(k_SampleParams);
            pass.DispatchThreadsOver(k_Lut);
        };

        const auto accumulate = [](loom::DispatchBuilder& pass)
        {
            pass.Shader("LoomImages", "AccumulateCS");
            pass.BindPrevious("PrevCount", k_Counter);
            pass.Bind("NextCount", k_Counter, loom::AccessModifier::WriteOnly);
            pass.Params(k_AccumulateParams);
            pass.DispatchThreads(1024, 1, 1);
        };

        graph.AddDispatch(k_Paint, k_EveryFrame, k_Graphics, paint);
        graph.AddDispatch(k_Sample, k_EveryFrame, k_Graphics, sample);
        graph.AddDispatch(k_Accumulate, k_EveryFrame, k_Graphics, accumulate);
        return loom::Compile(graph, Inputs(), sink);
    }

    // The use of `row` by `pass` that reads last frame's copy (previous) or this frame's.
    const loom::PassUse* UseOfCopy(const loom::CompiledData& data, int32_t pass, int32_t row, bool previous)
    {
        const loom::CompiledPass& compiled = data.Passes[pass];

        for (int32_t useIndex = compiled.FirstUse; useIndex < compiled.FirstUse + compiled.UseCount; ++useIndex)
        {
            const loom::PassUse& use = data.Uses[useIndex];

            if (use.Resource == row && use.Previous == previous)
            {
                return &use;
            }
        }

        return nullptr;
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

    constexpr loom::SyncScope k_ComputeWrite{ .Stages = rhi::PipelineStage::ComputeShader, .Accesses = rhi::Access::ShaderStorageWrite };
    constexpr loom::SyncScope k_ComputeRead{ .Stages = rhi::PipelineStage::ComputeShader, .Accesses = rhi::Access::ShaderStorageRead };

    // One Tick's core work: decide, then plan the barriers.
    void PlanTick(const loom::CompiledGraph& compiled,
                  std::span<const loom::ResourceState> resources,
                  std::span<const loom::PassControl> controls,
                  std::span<loom::ResourceSync> sync,
                  loom::FramePlan& plan)
    {
        loom::DecideRuns(compiled, resources, controls, {}, 0, plan);
        loom::PlanBarriers(compiled, resources, sync, 0, plan);
    }

}

TEST(Runtime, CompileCopiesThePipelinesAndTheUses)
{
    loom::CollectingSink sink;
    const std::optional<loom::CompiledGraph> compiled = CompileHashGraph(k_EveryFrame, sink);

    ASSERT_TRUE(compiled.has_value()) << sink.Diagnostics().size() << " diagnostics";
    const loom::CompiledData& data = compiled->Data();

    EXPECT_EQ(data.CodeFormat, lodestone::ShaderCodeFormat::Spirv);
    EXPECT_EQ(data.Ordered.Levels, (std::vector<int32_t>{ 0, 1 }));
    ASSERT_EQ(data.Pipelines.size(), 2u);

    for (const loom::CompiledPipeline& pipeline : data.Pipelines)
    {
        ASSERT_EQ(pipeline.Stages.size(), 1u);
        EXPECT_FALSE(pipeline.Stages[0].ShaderCode.empty());
        EXPECT_EQ(pipeline.Workgroup.X, 64u);
    }

    EXPECT_EQ(data.Pipelines[0].Stages[0].EntryPoint, "HashFillCS");
    EXPECT_EQ(data.Pipelines[0].PushConstantBytes, static_cast<uint32_t>(sizeof(FillParams)));
    EXPECT_EQ(data.Pipelines[1].PushConstantBytes, static_cast<uint32_t>(sizeof(AgainParams)));

    ASSERT_EQ(data.Params.size(), 2u);
    EXPECT_EQ(data.Passes[k_FillPass].Params, 0);
    EXPECT_EQ(data.Passes[k_AgainPass].Params, 1);

    const int32_t output = compiled->ResourceIndexOf(k_Output.ToRef());
    const int32_t output2 = compiled->ResourceIndexOf(k_Output2.ToRef());
    const loom::PassUse* fillWrite = UseOf(data, k_FillPass, output);
    const loom::PassUse* againRead = UseOf(data, k_AgainPass, output);
    const loom::PassUse* againWrite = UseOf(data, k_AgainPass, output2);

    ASSERT_NE(fillWrite, nullptr);
    ASSERT_NE(againRead, nullptr);
    ASSERT_NE(againWrite, nullptr);
    EXPECT_TRUE(fillWrite->Initializes);
    EXPECT_EQ(fillWrite->Scope, k_ComputeWrite);
    EXPECT_FALSE(againRead->Initializes);
    EXPECT_EQ(againRead->Scope, k_ComputeRead);
    EXPECT_EQ(data.Spans[output].FirstLevel, 0);
    EXPECT_EQ(data.Spans[output].LastLevel, 1);
    EXPECT_EQ(data.Spans[output2].FirstLevel, 1);
}

TEST(Runtime, EveryPassRunsWhenEveryRowIsReady)
{
    loom::CollectingSink sink;
    const std::optional<loom::CompiledGraph> compiled = CompileHashGraph(k_EveryFrame, sink);
    ASSERT_TRUE(compiled.has_value());

    loom::FramePlan plan;
    loom::PrepareFramePlan(*compiled, plan);
    const std::vector<loom::ResourceState> resources = AllComplete(*compiled);
    const std::vector<loom::PassControl> controls(compiled->PassCount());
    loom::DecideRuns(*compiled, resources, controls, {}, 0, plan);

    EXPECT_EQ(plan.Runs, (std::vector<int32_t>{ k_FillPass, k_AgainPass }));
    EXPECT_EQ(plan.Decisions[k_FillPass].Reason, loom::SkipReason::Ran);
    EXPECT_EQ(plan.Decisions[k_AgainPass].Reason, loom::SkipReason::Ran);
}

// A pending row stops every pass that uses it, and names the row. A Created row is enough to record against.
TEST(Runtime, APendingRowSkipsItsPasses)
{
    loom::CollectingSink sink;
    const std::optional<loom::CompiledGraph> compiled = CompileHashGraph(k_EveryFrame, sink);
    ASSERT_TRUE(compiled.has_value());

    const int32_t output = compiled->ResourceIndexOf(k_Output.ToRef());
    const int32_t output2 = compiled->ResourceIndexOf(k_Output2.ToRef());
    loom::FramePlan plan;
    loom::PrepareFramePlan(*compiled, plan);
    const std::vector<loom::PassControl> controls(compiled->PassCount());

    std::vector<loom::ResourceState> resources = AllComplete(*compiled);
    resources[output].Readiness = loom::ResourceReadiness::Created;
    resources[output2].Readiness = loom::ResourceReadiness::Pending;
    loom::DecideRuns(*compiled, resources, controls, {}, 0, plan);

    EXPECT_EQ(plan.Runs, (std::vector<int32_t>{ k_FillPass }));
    EXPECT_EQ(plan.Decisions[k_AgainPass].Reason, loom::SkipReason::NotCreated);
    EXPECT_EQ(plan.Decisions[k_AgainPass].Cause, output2);
}

TEST(Runtime, AFailedRowStopsItsPasses)
{
    loom::CollectingSink sink;
    const std::optional<loom::CompiledGraph> compiled = CompileHashGraph(k_EveryFrame, sink);
    ASSERT_TRUE(compiled.has_value());

    const int32_t output = compiled->ResourceIndexOf(k_Output.ToRef());
    loom::FramePlan plan;
    loom::PrepareFramePlan(*compiled, plan);
    const std::vector<loom::PassControl> controls(compiled->PassCount());

    std::vector<loom::ResourceState> resources = AllComplete(*compiled);
    resources[output].Readiness = loom::ResourceReadiness::Failed;
    loom::DecideRuns(*compiled, resources, controls, {}, 0, plan);

    EXPECT_TRUE(plan.Runs.empty());
    EXPECT_EQ(plan.Decisions[k_FillPass].Reason, loom::SkipReason::Failed);
    EXPECT_EQ(plan.Decisions[k_FillPass].Cause, output);
}

// Q3g: Fill writes Output and never reads it, so it initializes it. A disabled Fill skips Again, its reader.
TEST(Runtime, ASkippedInitializingWriterSkipsItsReaders)
{
    loom::CollectingSink sink;
    const std::optional<loom::CompiledGraph> compiled = CompileHashGraph(k_EveryFrame, sink);
    ASSERT_TRUE(compiled.has_value());

    loom::FramePlan plan;
    loom::PrepareFramePlan(*compiled, plan);
    const std::vector<loom::ResourceState> resources = AllComplete(*compiled);
    std::vector<loom::PassControl> controls(compiled->PassCount());
    controls[k_FillPass].Enabled = false;
    loom::DecideRuns(*compiled, resources, controls, {}, 0, plan);

    EXPECT_TRUE(plan.Runs.empty());
    EXPECT_EQ(plan.Decisions[k_FillPass].Reason, loom::SkipReason::Disabled);
    EXPECT_EQ(plan.Decisions[k_AgainPass].Reason, loom::SkipReason::Propagated);
    EXPECT_EQ(plan.Decisions[k_AgainPass].Cause, k_FillPass);
}

// A Once pass that ran left its contents behind: after CommitRuns it skips by policy, and its reader still runs.
TEST(Runtime, AOncePassRunsOnceAndKeepsItsReaders)
{
    loom::CollectingSink sink;
    const std::optional<loom::CompiledGraph> compiled = CompileHashGraph(loom::RunPolicy::Once, sink);
    ASSERT_TRUE(compiled.has_value());

    loom::FramePlan plan;
    loom::PrepareFramePlan(*compiled, plan);
    const std::vector<loom::ResourceState> resources = AllComplete(*compiled);
    std::vector<loom::PassControl> controls(compiled->PassCount());

    loom::DecideRuns(*compiled, resources, controls, {}, 0, plan);
    EXPECT_EQ(plan.Runs, (std::vector<int32_t>{ k_FillPass, k_AgainPass }));
    loom::CommitRuns(*compiled, plan, controls);

    loom::DecideRuns(*compiled, resources, controls, {}, 0, plan);
    EXPECT_EQ(plan.Runs, (std::vector<int32_t>{ k_AgainPass }));
    EXPECT_EQ(plan.Decisions[k_FillPass].Reason, loom::SkipReason::Policy);
}

// Frame 1: one batch, at the level boundary: Output from Fill's write to Again's read. Frame 2 adds the frame-start
// batch: Fill's new write of Output waits for frame 1's write and read. Again's write of Output2 waits for frame 1's
// write in the level 1 batch, beside the read after write of Output.
TEST(Runtime, BarriersGoAtLevelBoundariesAndAtFrameStart)
{
    loom::CollectingSink sink;
    const std::optional<loom::CompiledGraph> compiled = CompileHashGraph(k_EveryFrame, sink);
    ASSERT_TRUE(compiled.has_value());

    const int32_t output = compiled->ResourceIndexOf(k_Output.ToRef());
    const int32_t output2 = compiled->ResourceIndexOf(k_Output2.ToRef());
    loom::FramePlan plan;
    loom::PrepareFramePlan(*compiled, plan);
    const std::vector<loom::ResourceState> resources = AllComplete(*compiled);
    const std::vector<loom::PassControl> controls(compiled->PassCount());
    std::vector<loom::ResourceSync> sync(compiled->ResourceCount());

    PlanTick(*compiled, resources, controls, sync, plan);

    ASSERT_EQ(plan.Batches.size(), 1u);
    EXPECT_EQ(plan.Batches[0].BeforeRun, 1);
    ASSERT_EQ(plan.Batches[0].Count, 1);
    const loom::PlannedBarrier& raw = plan.Barriers[plan.Batches[0].FirstBarrier];
    EXPECT_EQ(raw.Resource, output);
    EXPECT_EQ(raw.Before, k_ComputeWrite);
    EXPECT_EQ(raw.After, k_ComputeRead);

    PlanTick(*compiled, resources, controls, sync, plan);

    ASSERT_EQ(plan.Batches.size(), 2u);
    EXPECT_EQ(plan.Batches[0].BeforeRun, 0);
    ASSERT_EQ(plan.Batches[0].Count, 1);
    const loom::PlannedBarrier& frameStart = plan.Barriers[plan.Batches[0].FirstBarrier];
    EXPECT_EQ(frameStart.Resource, output);
    EXPECT_EQ(frameStart.Before.Stages, rhi::PipelineStage::ComputeShader);
    EXPECT_EQ(frameStart.Before.Accesses, rhi::Access::ShaderStorageWrite);
    EXPECT_EQ(frameStart.After, k_ComputeWrite);

    EXPECT_EQ(plan.Batches[1].BeforeRun, 1);
    ASSERT_EQ(plan.Batches[1].Count, 2);
    std::vector<int32_t> rows;

    for (int32_t index = plan.Batches[1].FirstBarrier; index < plan.Batches[1].FirstBarrier + plan.Batches[1].Count; ++index)
    {
        rows.push_back(plan.Barriers[index].Resource);
    }

    std::ranges::sort(rows);
    EXPECT_EQ(rows, (std::vector<int32_t>{ std::min(output, output2), std::max(output, output2) }));
}

// A re-supplied row is a new object: nothing on it waits for the work on the old one, so frame 2 has no frame-start
// barrier for Output.
TEST(Runtime, ANewEpochStartsWithNoPendingWork)
{
    loom::CollectingSink sink;
    const std::optional<loom::CompiledGraph> compiled = CompileHashGraph(k_EveryFrame, sink);
    ASSERT_TRUE(compiled.has_value());

    const int32_t output = compiled->ResourceIndexOf(k_Output.ToRef());
    loom::FramePlan plan;
    loom::PrepareFramePlan(*compiled, plan);
    std::vector<loom::ResourceState> resources = AllComplete(*compiled);
    const std::vector<loom::PassControl> controls(compiled->PassCount());
    std::vector<loom::ResourceSync> sync(compiled->ResourceCount());

    PlanTick(*compiled, resources, controls, sync, plan);
    resources[output].Epoch = 2u;
    PlanTick(*compiled, resources, controls, sync, plan);

    ASSERT_EQ(plan.Batches.size(), 1u);
    EXPECT_EQ(plan.Batches[0].BeforeRun, 1);
}

// A write after reads alone: Output's only pending work is a read (its write is already visible), so Fill's new write
// needs an execution dependency from the read's stage, with no access to make visible.
TEST(Runtime, AWriteAfterReadsWaitsForTheReads)
{
    loom::CollectingSink sink;
    const std::optional<loom::CompiledGraph> compiled = CompileHashGraph(k_EveryFrame, sink);
    ASSERT_TRUE(compiled.has_value());

    const int32_t output = compiled->ResourceIndexOf(k_Output.ToRef());
    loom::FramePlan plan;
    loom::PrepareFramePlan(*compiled, plan);
    const std::vector<loom::ResourceState> resources = AllComplete(*compiled);
    const std::vector<loom::PassControl> controls(compiled->PassCount());
    std::vector<loom::ResourceSync> sync(compiled->ResourceCount(), loom::ResourceSync{ .Epoch = 1u });
    sync[output].ReadsSinceWrite = k_ComputeRead;

    PlanTick(*compiled, resources, controls, sync, plan);

    ASSERT_FALSE(plan.Batches.empty());
    EXPECT_EQ(plan.Batches[0].BeforeRun, 0);
    ASSERT_EQ(plan.Batches[0].Count, 1);
    const loom::PlannedBarrier& war = plan.Barriers[plan.Batches[0].FirstBarrier];
    EXPECT_EQ(war.Resource, output);
    EXPECT_EQ(war.Before, (loom::SyncScope{ .Stages = rhi::PipelineStage::ComputeShader, .Accesses = rhi::Access::None }));
    EXPECT_EQ(war.After, k_ComputeWrite);
}

TEST(Runtime, ParamsOfAnotherSizeAreLM704)
{
    loom::CollectingSink sink;
    const std::optional<loom::CompiledGraph> compiled = CompileHashGraph(k_EveryFrame, sink);
    ASSERT_TRUE(compiled.has_value());

    loom::ParamsStore store;
    store.Prepare(*compiled);
    const uint64_t wrong = 0u;
    loom::CollectingSink setSink;
    const int32_t changed = store.Set(k_FillParams.ToRef(), std::as_bytes(std::span{ &wrong, 1u }), alignof(uint64_t),
                                      std::source_location::current(), setSink);

    EXPECT_EQ(changed, loom::k_ParamsUnchanged);

    ASSERT_EQ(setSink.Diagnostics().size(), 1u);
    EXPECT_EQ(setSink.Diagnostics().front().Code, loom::DiagnosticCode::LM704);
    EXPECT_FALSE(store.IsSet(0));
}

// LM703 names each running pass whose params key was never set. A key that is set holds the bytes given.
TEST(Runtime, UnsetParamsAreLM703)
{
    loom::CollectingSink sink;
    const std::optional<loom::CompiledGraph> compiled = CompileHashGraph(k_EveryFrame, sink);
    ASSERT_TRUE(compiled.has_value());

    loom::FramePlan plan;
    loom::PrepareFramePlan(*compiled, plan);
    const std::vector<loom::ResourceState> resources = AllComplete(*compiled);
    const std::vector<loom::PassControl> controls(compiled->PassCount());
    loom::DecideRuns(*compiled, resources, controls, {}, 0, plan);

    loom::ParamsStore store;
    store.Prepare(*compiled);
    const FillParams fill{ .seed = 7u, .count = k_Count };
    loom::CollectingSink setSink;
    [[maybe_unused]] const int32_t changed = store.Set(k_FillParams.ToRef(), std::as_bytes(std::span{ &fill, 1u }),
                                                       alignof(FillParams), std::source_location::current(), setSink);

    loom::CollectingSink tickSink;
    EXPECT_FALSE(store.CheckRuns(*compiled, plan, tickSink));
    ASSERT_EQ(tickSink.Diagnostics().size(), 1u);
    const loom::Diagnostic& diagnostic = tickSink.Diagnostics().front();
    EXPECT_EQ(diagnostic.Code, loom::DiagnosticCode::LM703);
    EXPECT_EQ(diagnostic.Pass.Hash, k_Again.Hash);

    ASSERT_TRUE(store.IsSet(0));
    const std::span<const std::byte> bytes = store.Bytes(0);
    ASSERT_EQ(bytes.size(), sizeof(FillParams));
    EXPECT_TRUE(std::equal(bytes.begin(), bytes.end(), std::as_bytes(std::span{ &fill, 1u }).begin()));
}

// Step 6 run policies. WhenInputsChange runs on its first Tick, then only after a change: new params bytes (rule b)
// or a new object in a row it reads (rule a). A Tick with no change skips it by policy, and its skip does not
// propagate.
TEST(Runtime, WhenInputsChangeRunsFirstThenOnlyOnAChange)
{
    loom::CollectingSink sink;
    const std::optional<loom::CompiledGraph> compiled = CompileHashGraphWith(loom::RunPolicy::Once,
                                                                             loom::RunPolicy::WhenInputsChange, sink);
    ASSERT_TRUE(compiled.has_value());

    loom::FramePlan plan;
    loom::PrepareFramePlan(*compiled, plan);
    const std::vector<loom::ResourceState> resources = AllComplete(*compiled);
    std::vector<loom::PassControl> controls(compiled->PassCount());
    const int32_t output = compiled->ResourceIndexOf(k_Output.ToRef());

    EXPECT_EQ(DecideAndCommit(*compiled, resources, controls, plan), (std::vector<int32_t>{ k_FillPass, k_AgainPass }));
    EXPECT_TRUE(DecideAndCommit(*compiled, resources, controls, plan).empty());
    EXPECT_EQ(plan.Decisions[k_AgainPass].Reason, loom::SkipReason::Policy);

    loom::MarkParamsChanged(*compiled, compiled->Data().Passes[k_AgainPass].Params, controls);
    EXPECT_EQ(DecideAndCommit(*compiled, resources, controls, plan), (std::vector<int32_t>{ k_AgainPass }));
    EXPECT_TRUE(DecideAndCommit(*compiled, resources, controls, plan).empty());

    loom::MarkRowChanged(*compiled, output, controls);
    EXPECT_EQ(DecideAndCommit(*compiled, resources, controls, plan), (std::vector<int32_t>{ k_AgainPass }));
    EXPECT_TRUE(DecideAndCommit(*compiled, resources, controls, plan).empty());
}

// Rule c: a WhenInputsChange pass runs in the Tick where a pass that writes its input runs. The OnTrigger writer
// skips by policy until it is triggered, and that skip does not stop its reader's first run.
TEST(Runtime, WhenInputsChangeFollowsAWriterThatRan)
{
    loom::CollectingSink sink;
    const std::optional<loom::CompiledGraph> compiled = CompileHashGraphWith(loom::RunPolicy::OnTrigger,
                                                                             loom::RunPolicy::WhenInputsChange, sink);
    ASSERT_TRUE(compiled.has_value());

    loom::FramePlan plan;
    loom::PrepareFramePlan(*compiled, plan);
    const std::vector<loom::ResourceState> resources = AllComplete(*compiled);
    std::vector<loom::PassControl> controls(compiled->PassCount());

    EXPECT_EQ(DecideAndCommit(*compiled, resources, controls, plan), (std::vector<int32_t>{ k_AgainPass }));
    EXPECT_EQ(plan.Decisions[k_FillPass].Reason, loom::SkipReason::Policy);
    EXPECT_TRUE(DecideAndCommit(*compiled, resources, controls, plan).empty());

    controls[k_FillPass].Triggered = true;
    EXPECT_EQ(DecideAndCommit(*compiled, resources, controls, plan), (std::vector<int32_t>{ k_FillPass, k_AgainPass }));
    EXPECT_TRUE(DecideAndCommit(*compiled, resources, controls, plan).empty());
}

// A change that a WhenInputsChange pass could not act on (its other row was pending) is kept, and it runs on the
// first Tick that allows it.
TEST(Runtime, WhenInputsChangeKeepsAChangeItCouldNotRunFor)
{
    loom::CollectingSink sink;
    const std::optional<loom::CompiledGraph> compiled = CompileHashGraphWith(loom::RunPolicy::OnTrigger,
                                                                             loom::RunPolicy::WhenInputsChange, sink);
    ASSERT_TRUE(compiled.has_value());

    loom::FramePlan plan;
    loom::PrepareFramePlan(*compiled, plan);
    const std::vector<loom::ResourceState> ready = AllComplete(*compiled);
    std::vector<loom::PassControl> controls(compiled->PassCount());
    const int32_t output2 = compiled->ResourceIndexOf(k_Output2.ToRef());

    EXPECT_EQ(DecideAndCommit(*compiled, ready, controls, plan), (std::vector<int32_t>{ k_AgainPass }));

    std::vector<loom::ResourceState> pending = AllComplete(*compiled);
    pending[output2].Readiness = loom::ResourceReadiness::Pending;
    controls[k_FillPass].Triggered = true;
    EXPECT_EQ(DecideAndCommit(*compiled, pending, controls, plan), (std::vector<int32_t>{ k_FillPass }));
    EXPECT_EQ(plan.Decisions[k_AgainPass].Reason, loom::SkipReason::NotCreated);

    EXPECT_EQ(DecideAndCommit(*compiled, ready, controls, plan), (std::vector<int32_t>{ k_AgainPass }));
    EXPECT_TRUE(DecideAndCommit(*compiled, ready, controls, plan).empty());
}

// OnTrigger: no run until triggered; a trigger that cannot run yet waits; several triggers before a run are one run.
TEST(Runtime, OnTriggerRunsOncePerTriggeredSubmit)
{
    loom::CollectingSink sink;
    const std::optional<loom::CompiledGraph> compiled = CompileHashGraphWith(k_EveryFrame, loom::RunPolicy::OnTrigger,
                                                                             sink);
    ASSERT_TRUE(compiled.has_value());

    loom::FramePlan plan;
    loom::PrepareFramePlan(*compiled, plan);
    const std::vector<loom::ResourceState> ready = AllComplete(*compiled);
    std::vector<loom::PassControl> controls(compiled->PassCount());
    const int32_t output2 = compiled->ResourceIndexOf(k_Output2.ToRef());

    EXPECT_EQ(DecideAndCommit(*compiled, ready, controls, plan), (std::vector<int32_t>{ k_FillPass }));
    EXPECT_EQ(plan.Decisions[k_AgainPass].Reason, loom::SkipReason::Policy);

    std::vector<loom::ResourceState> pending = AllComplete(*compiled);
    pending[output2].Readiness = loom::ResourceReadiness::Pending;
    controls[k_AgainPass].Triggered = true;
    controls[k_AgainPass].Triggered = true;
    EXPECT_EQ(DecideAndCommit(*compiled, pending, controls, plan), (std::vector<int32_t>{ k_FillPass }));
    EXPECT_TRUE(controls[k_AgainPass].Triggered);

    EXPECT_EQ(DecideAndCommit(*compiled, ready, controls, plan), (std::vector<int32_t>{ k_FillPass, k_AgainPass }));
    EXPECT_EQ(DecideAndCommit(*compiled, ready, controls, plan), (std::vector<int32_t>{ k_FillPass }));
}

// Set reports a change only for new bytes; the first Set always counts.
TEST(Runtime, ParamsSetReportsOnlyNewBytes)
{
    loom::CollectingSink sink;
    const std::optional<loom::CompiledGraph> compiled = CompileHashGraph(k_EveryFrame, sink);
    ASSERT_TRUE(compiled.has_value());

    loom::ParamsStore store;
    store.Prepare(*compiled);
    loom::CollectingSink setSink;
    const FillParams first{ .seed = 7u, .count = k_Count };
    const FillParams second{ .seed = 8u, .count = k_Count };

    const auto set = [&](const FillParams& value)
    {
        return store.Set(k_FillParams.ToRef(), std::as_bytes(std::span{ &value, 1u }), alignof(FillParams),
                         std::source_location::current(), setSink);
    };

    EXPECT_EQ(set(first), 0);
    EXPECT_EQ(set(first), loom::k_ParamsUnchanged);
    EXPECT_EQ(set(second), 0);
    EXPECT_TRUE(setSink.Diagnostics().empty());
}

// ExplainLastTick names each pass with its decision, and the cause where there is one.
TEST(Runtime, ExplainLastTickNamesEachDecision)
{
    loom::CollectingSink sink;
    const std::optional<loom::CompiledGraph> compiled = CompileHashGraph(k_EveryFrame, sink);
    ASSERT_TRUE(compiled.has_value());

    loom::FramePlan plan;
    loom::PrepareFramePlan(*compiled, plan);
    std::vector<loom::ResourceState> resources = AllComplete(*compiled);
    const std::vector<loom::PassControl> controls(compiled->PassCount());
    resources[compiled->ResourceIndexOf(k_Output2.ToRef())].Readiness = loom::ResourceReadiness::Pending;
    loom::DecideRuns(*compiled, resources, controls, {}, 0, plan);

    const std::string text = loom::ExplainLastTick(*compiled, plan);
    EXPECT_EQ(text, "Runtime.Fill: ran\nRuntime.Again: skipped, 'Runtime.Output2' does not exist yet\n");
}

// Step 6 images. A storage write is General, a sampled read ShaderReadOnly, and each descriptor names its use's layout.
// A sampler has no layout.
TEST(Runtime, ImageUsesCarryTheirLayouts)
{
    loom::CollectingSink sink;
    const std::optional<loom::CompiledGraph> compiled = CompileImageGraph(loom::OnNoHistory::Clear, sink);
    ASSERT_TRUE(compiled.has_value()) << sink.Diagnostics().size() << " diagnostics";
    const loom::CompiledData& data = compiled->Data();

    const int32_t painted = compiled->ResourceIndexOf(k_Painted.ToRef());
    const int32_t point = compiled->ResourceIndexOf(k_Point.ToRef());
    const loom::PassUse* write = UseOf(data, k_PaintPass, painted);
    const loom::PassUse* read = UseOf(data, k_SamplePass, painted);
    const loom::PassUse* sampler = UseOf(data, k_SamplePass, point);

    ASSERT_NE(write, nullptr);
    ASSERT_NE(read, nullptr);
    ASSERT_NE(sampler, nullptr);
    EXPECT_EQ(write->Layout, rhi::ImageLayout::General);
    EXPECT_EQ(write->Scope.Accesses, rhi::Access::ShaderStorageWrite);
    EXPECT_EQ(read->Layout, rhi::ImageLayout::ShaderReadOnly);
    EXPECT_EQ(read->Scope.Accesses, rhi::Access::ShaderSampledRead);
    EXPECT_EQ(sampler->Layout, rhi::ImageLayout::Undefined);

    int32_t storageSlots = 0;
    int32_t sampledSlots = 0;

    for (size_t slotIndex = 0u; slotIndex < data.Slots.size(); ++slotIndex)
    {
        if (data.Slots[slotIndex].Resource.Value != painted)
        {
            continue;
        }

        const bool storage = data.Slots[slotIndex].Kind == lodestone::BindingKind::StorageTexture;
        EXPECT_EQ(data.SlotLayouts[slotIndex], storage ? rhi::ImageLayout::General : rhi::ImageLayout::ShaderReadOnly);
        storageSlots += storage ? 1 : 0;
        sampledSlots += storage ? 0 : 1;
    }

    EXPECT_EQ(storageSlots, 1);
    EXPECT_EQ(sampledSlots, 1);
}

// Frame 0: Painted changes from Undefined to General at frame start, then to ShaderReadOnly before Sample, waiting for
// Paint's write. The imported Lut starts in the layout of its handoff usage, so Sample's read needs no barrier. Frame 1:
// back to General, waiting for frame 0's read.
TEST(Runtime, ImageLayoutsChangeAtTheBatches)
{
    loom::CollectingSink sink;
    const std::optional<loom::CompiledGraph> compiled = CompileImageGraph(loom::OnNoHistory::Clear, sink);
    ASSERT_TRUE(compiled.has_value());

    const int32_t painted = compiled->ResourceIndexOf(k_Painted.ToRef());
    const int32_t lut = compiled->ResourceIndexOf(k_Lut.ToRef());
    loom::FramePlan plan;
    loom::PrepareFramePlan(*compiled, plan);
    std::vector<loom::ResourceState> resources = AllComplete(*compiled);
    resources[lut].InitialLayout = rhi::ImageLayout::ShaderReadOnly;
    std::vector<loom::PassControl> controls(compiled->PassCount());
    std::vector<loom::ResourceSync> sync(compiled->Data().CopyRows.size());

    loom::DecideRuns(*compiled, resources, controls, sync, 0, plan);
    loom::PlanBarriers(*compiled, resources, sync, 0, plan);

    ASSERT_GE(plan.Batches.size(), 3u);     // the clears, frame start, before Sample
    const loom::BarrierBatch& frameStart = plan.Batches[1];
    const loom::BarrierBatch& beforeSample = plan.Batches[2];
    ASSERT_EQ(plan.Runs[static_cast<size_t>(beforeSample.BeforeRun)], k_SamplePass);

    const loom::PlannedBarrier* toGeneral = BarrierOf(plan, frameStart, painted);
    ASSERT_NE(toGeneral, nullptr);
    EXPECT_EQ(toGeneral->OldLayout, rhi::ImageLayout::Undefined);
    EXPECT_EQ(toGeneral->NewLayout, rhi::ImageLayout::General);

    const loom::PlannedBarrier* toRead = BarrierOf(plan, beforeSample, painted);
    ASSERT_NE(toRead, nullptr);
    EXPECT_EQ(toRead->OldLayout, rhi::ImageLayout::General);
    EXPECT_EQ(toRead->NewLayout, rhi::ImageLayout::ShaderReadOnly);
    EXPECT_EQ(toRead->Before, (loom::SyncScope{ .Stages = rhi::PipelineStage::ComputeShader, .Accesses = rhi::Access::ShaderStorageWrite }));
    EXPECT_EQ(toRead->After, (loom::SyncScope{ .Stages = rhi::PipelineStage::ComputeShader, .Accesses = rhi::Access::ShaderSampledRead }));

    for (const loom::PlannedBarrier& barrier : plan.Barriers)
    {
        EXPECT_NE(barrier.Resource, lut);
    }

    loom::CommitRuns(*compiled, plan, controls);
    loom::DecideRuns(*compiled, resources, controls, sync, 1, plan);
    loom::PlanBarriers(*compiled, resources, sync, 1, plan);

    ASSERT_FALSE(plan.Batches.empty());
    const loom::PlannedBarrier* again = BarrierOf(plan, plan.Batches.front(), painted);
    ASSERT_NE(again, nullptr);
    EXPECT_EQ(again->OldLayout, rhi::ImageLayout::ShaderReadOnly);
    EXPECT_EQ(again->NewLayout, rhi::ImageLayout::General);
    EXPECT_NE(again->Before.Stages & rhi::PipelineStage::ComputeShader, rhi::PipelineStage::None);
}

// A history ring of depth 1 has two copies: the row, and one after the rows. Frame f writes copy f % 2 and reads the
// other through BindPrevious. The BindPrevious use and the write are two uses, and the previous one only reads.
TEST(Runtime, HistoryRingsAlternateTheirCopies)
{
    loom::CollectingSink sink;
    const std::optional<loom::CompiledGraph> compiled = CompileImageGraph(loom::OnNoHistory::Clear, sink);
    ASSERT_TRUE(compiled.has_value());
    const loom::CompiledData& data = compiled->Data();

    const int32_t counter = compiled->ResourceIndexOf(k_Counter.ToRef());
    const int32_t rowCount = compiled->ResourceCount();
    ASSERT_EQ(data.CopyCounts[counter], 2);
    ASSERT_EQ(data.CopyRows.size(), static_cast<size_t>(rowCount) + 1u);
    EXPECT_EQ(data.ExtraCopyBase[counter], rowCount);
    EXPECT_EQ(data.CopyRows[static_cast<size_t>(rowCount)], counter);

    EXPECT_EQ(loom::CopyOf(data, counter, 0, false), counter);
    EXPECT_EQ(loom::CopyOf(data, counter, 0, true), rowCount);
    EXPECT_EQ(loom::CopyOf(data, counter, 1, false), rowCount);
    EXPECT_EQ(loom::CopyOf(data, counter, 1, true), counter);
    EXPECT_EQ(loom::CopyOf(data, counter, 2, false), counter);

    const loom::PassUse* previous = UseOfCopy(data, k_AccumulatePass, counter, true);
    const loom::PassUse* current = UseOfCopy(data, k_AccumulatePass, counter, false);
    ASSERT_NE(previous, nullptr);
    ASSERT_NE(current, nullptr);
    EXPECT_EQ(previous->Access, loom::ResourceAccess::Read);
    EXPECT_FALSE(previous->Initializes);
    EXPECT_EQ(current->Access, loom::ResourceAccess::Write);
}

// OnNoHistory::Clear: frame 0's previous copy holds nothing, so the plan clears it in a batch before every run, and the
// read then waits for the clear. Frame 1's previous copy is frame 0's write: no clear.
TEST(Runtime, AnEmptyHistoryCopyIsClearedFirst)
{
    loom::CollectingSink sink;
    const std::optional<loom::CompiledGraph> compiled = CompileImageGraph(loom::OnNoHistory::Clear, sink);
    ASSERT_TRUE(compiled.has_value());
    const loom::CompiledData& data = compiled->Data();

    const int32_t counter = compiled->ResourceIndexOf(k_Counter.ToRef());
    const int32_t previousCopy = loom::CopyOf(data, counter, 0, true);
    loom::FramePlan plan;
    loom::PrepareFramePlan(*compiled, plan);
    const std::vector<loom::ResourceState> resources = AllComplete(*compiled);
    std::vector<loom::PassControl> controls(compiled->PassCount());
    std::vector<loom::ResourceSync> sync(data.CopyRows.size());

    loom::DecideRuns(*compiled, resources, controls, sync, 0, plan);
    EXPECT_EQ(plan.Decisions[k_AccumulatePass].Reason, loom::SkipReason::Ran);
    EXPECT_EQ(plan.Clears, (std::vector<int32_t>{ previousCopy }));

    loom::PlanBarriers(*compiled, resources, sync, 0, plan);
    ASSERT_FALSE(plan.Batches.empty());
    EXPECT_EQ(plan.Batches.front().BeforeRun, loom::k_BeforeClears);
    const loom::PlannedBarrier* toClear = BarrierOf(plan, plan.Batches.front(), previousCopy);
    ASSERT_NE(toClear, nullptr);
    EXPECT_EQ(toClear->After, (loom::SyncScope{ .Stages = rhi::PipelineStage::AllTransfer, .Accesses = rhi::Access::TransferWrite }));
    EXPECT_TRUE(sync[previousCopy].HasContents);
    EXPECT_TRUE(sync[counter].HasContents);

    // the read of the cleared copy waits for the clear, at frame start
    const loom::PlannedBarrier* toRead = BarrierOf(plan, plan.Batches[1], previousCopy);
    ASSERT_NE(toRead, nullptr);
    EXPECT_EQ(toRead->Before.Accesses, rhi::Access::TransferWrite);

    loom::CommitRuns(*compiled, plan, controls);
    loom::DecideRuns(*compiled, resources, controls, sync, 1, plan);
    EXPECT_TRUE(plan.Clears.empty());
}

// OnNoHistory::Skip: a BindPrevious read of an empty copy skips the pass with NoHistory, and names the row. Once the
// copy has contents, the pass runs.
TEST(Runtime, AnEmptyHistoryCopySkipsWithNoHistory)
{
    loom::CollectingSink sink;
    const std::optional<loom::CompiledGraph> compiled = CompileImageGraph(loom::OnNoHistory::Skip, sink);
    ASSERT_TRUE(compiled.has_value());
    const loom::CompiledData& data = compiled->Data();

    const int32_t counter = compiled->ResourceIndexOf(k_Counter.ToRef());
    loom::FramePlan plan;
    loom::PrepareFramePlan(*compiled, plan);
    const std::vector<loom::ResourceState> resources = AllComplete(*compiled);
    const std::vector<loom::PassControl> controls(compiled->PassCount());
    std::vector<loom::ResourceSync> sync(data.CopyRows.size());

    loom::DecideRuns(*compiled, resources, controls, sync, 0, plan);
    EXPECT_EQ(plan.Decisions[k_AccumulatePass].Reason, loom::SkipReason::NoHistory);
    EXPECT_EQ(plan.Decisions[k_AccumulatePass].Cause, counter);
    EXPECT_TRUE(plan.Clears.empty());

    sync[loom::CopyOf(data, counter, 0, true)].HasContents = true;
    loom::DecideRuns(*compiled, resources, controls, sync, 0, plan);
    EXPECT_EQ(plan.Decisions[k_AccumulatePass].Reason, loom::SkipReason::Ran);
}
