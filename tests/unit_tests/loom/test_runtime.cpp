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

    void BuildHashGraph(loom::GraphBuilder& graph, loom::RunPolicy fill_policy)
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
        graph.AddDispatch(k_Again, k_EveryFrame, k_Graphics, again);
    }

    std::optional<loom::CompiledGraph> CompileHashGraph(loom::RunPolicy fill_policy, loom::CollectingSink& sink)
    {
        loom::GraphBuilder graph{ "Runtime.Graph", "Runtime.Library" };
        BuildHashGraph(graph, fill_policy);
        return loom::Compile(graph, Inputs(), sink);
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

    constexpr loom::SyncScope k_ComputeWrite{ .Stages = loom::SyncStage::Compute, .Accesses = loom::SyncAccess::ShaderWrite };
    constexpr loom::SyncScope k_ComputeRead{ .Stages = loom::SyncStage::Compute, .Accesses = loom::SyncAccess::ShaderRead };

    // One Tick's core work: decide, then plan the barriers.
    void PlanTick(const loom::CompiledGraph& compiled,
                  std::span<const loom::ResourceState> resources,
                  std::span<const loom::PassControl> controls,
                  std::span<loom::ResourceSync> sync,
                  loom::FramePlan& plan)
    {
        loom::DecideRuns(compiled, resources, controls, plan);
        loom::PlanBarriers(compiled, resources, sync, plan);
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
        EXPECT_FALSE(pipeline.Stages[0].Code.empty());
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
    loom::DecideRuns(*compiled, resources, controls, plan);

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
    loom::DecideRuns(*compiled, resources, controls, plan);

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
    loom::DecideRuns(*compiled, resources, controls, plan);

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
    loom::DecideRuns(*compiled, resources, controls, plan);

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

    loom::DecideRuns(*compiled, resources, controls, plan);
    EXPECT_EQ(plan.Runs, (std::vector<int32_t>{ k_FillPass, k_AgainPass }));
    loom::CommitRuns(*compiled, plan, controls);

    loom::DecideRuns(*compiled, resources, controls, plan);
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
    EXPECT_EQ(frameStart.Before.Stages, loom::SyncStage::Compute);
    EXPECT_EQ(frameStart.Before.Accesses, loom::SyncAccess::ShaderWrite);
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
    EXPECT_EQ(war.Before, (loom::SyncScope{ .Stages = loom::SyncStage::Compute, .Accesses = loom::SyncAccess::None }));
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
    store.Set(k_FillParams.ToRef(), std::as_bytes(std::span{ &wrong, 1u }), alignof(uint64_t),
              std::source_location::current(), setSink);

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
    loom::DecideRuns(*compiled, resources, controls, plan);

    loom::ParamsStore store;
    store.Prepare(*compiled);
    const FillParams fill{ .seed = 7u, .count = k_Count };
    loom::CollectingSink setSink;
    store.Set(k_FillParams.ToRef(), std::as_bytes(std::span{ &fill, 1u }), alignof(FillParams),
              std::source_location::current(), setSink);

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
