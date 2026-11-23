#include "CompiledGraph.hpp"
#include "PlanBarriers.hpp"
#include "FramePlan.hpp"
#include "PassStatements.hpp"
#include "compile/CompiledData.hpp"
#include "RhiFlags.hpp"
#include <cstddef>
#include <cstdint>
#include <span>

// The barriers of one Tick, batched by level (spec 5.1 step 6). LM502 proves that two passes in one level have no
// hazard, so every barrier can wait for the level boundary: one batch before each level that needs one. The state
// carried from the last Tick makes the first batch the frame-start batch: it orders this frame's first accesses after
// the earlier frames' work on the same queue.
//
// The rules, per row, against the state left by the levels before:
//  - a read after a write needs a barrier from the write to the read, unless an earlier barrier already made the
//    write visible to that read scope;
//  - a write after a write needs a barrier from the old write to the new one;
//  - a write after reads needs a barrier from the reads to the write (an execution dependency);
//  - an image use whose layout differs from the image's needs a layout change, which waits for the last write and
//    every read since, and makes the write visible to the use. It replaces the three rules above for that use.
namespace loom
{

namespace
{

    bool Contains(const SyncScope& outer, const SyncScope& inner)
    {
        const bool stages = (inner.Stages & ~outer.Stages) == rhi::PipelineStage::None;
        const bool accesses = (inner.Accesses & ~outer.Accesses) == rhi::Access::None;
        return stages && accesses;
    }

    bool IsEmpty(const SyncScope& scope)
    {
        return scope.Stages == rhi::PipelineStage::None;
    }

    void Merge(SyncScope& into, const SyncScope& scope)
    {
        into.Stages |= scope.Stages;
        into.Accesses |= scope.Accesses;
    }

    // Every write access a use can name. The read part of a use is everything else.
    constexpr rhi::Access k_WriteAccesses = rhi::Access::ShaderWrite |
                                            rhi::Access::ShaderStorageWrite |
                                            rhi::Access::ColorAttachmentWrite |
                                            rhi::Access::DepthStencilAttachmentWrite |
                                            rhi::Access::TransferWrite |
                                            rhi::Access::HostWrite |
                                            rhi::Access::MemoryWrite |
                                            rhi::Access::CommandPreprocessWrite |
                                            rhi::Access::TransformFeedbackWrite |
                                            rhi::Access::TransformFeedbackCounterWrite |
                                            rhi::Access::AccelerationStructureWrite |
                                            rhi::Access::MicromapWrite |
                                            rhi::Access::MemoryDecompressionWrite;

    SyncScope ReadPart(const PassUse& use)
    {
        return SyncScope{ .Stages = use.Scope.Stages, .Accesses = use.Scope.Accesses & ~k_WriteAccesses };
    }

    SyncScope WritePart(const PassUse& use)
    {
        return SyncScope{ .Stages = use.Scope.Stages, .Accesses = use.Scope.Accesses & k_WriteAccesses };
    }

    bool Reads(const PassUse& use)
    {
        return use.Access == ResourceAccess::Read || use.Access == ResourceAccess::ReadWrite;
    }

    bool Writes(const PassUse& use)
    {
        return use.Access == ResourceAccess::Write || use.Access == ResourceAccess::ReadWrite;
    }

    // One barrier per row in the open batch: a second need on the same row widens the first.
    void AddBarrier(FramePlan& plan,
                    int32_t row,
                    const SyncScope& before,
                    const SyncScope& after,
                    const rhi::ImageLayout layout)
    {
        int32_t& index = plan.BatchBarrier[row];

        if (index < 0)
        {
            index = static_cast<int32_t>(plan.Barriers.size());
            PlannedBarrier barrier
            {
                .Resource = row,
                .Before = before,
                .After = after,
                .OldLayout = layout,
                .NewLayout = layout
            };
            plan.Barriers.emplace_back(std::move(barrier));
            return;
        }

        Merge(plan.Barriers[index].Before, before);
        Merge(plan.Barriers[index].After, after);
    }

    void AddLayoutChange(FramePlan& plan, int32_t copy, const PassUse& use, const ResourceSync& sync)
    {
        int32_t& index = plan.BatchBarrier[copy];
        const SyncScope before{ .Stages = sync.Write.Stages | sync.ReadsSinceWrite.Stages, .Accesses = sync.Write.Accesses };

        if (index < 0)
        {
            index = static_cast<int32_t>(plan.Barriers.size());

            plan.Barriers.push_back(PlannedBarrier
            {
                .Resource = copy,
                .Before = before,
                .After = use.Scope,
                .OldLayout = sync.Layout,
                .NewLayout = use.Layout,
            });

            return;
        }

        // Compile gave every use of the image in this level one layout, so a second use only widens the scopes
        Merge(plan.Barriers[index].Before, before);
        Merge(plan.Barriers[index].After, use.Scope);
    }

    // The barriers that one use needs against the state before its level.
    void NeedsOf(FramePlan& plan, int32_t copy, const PassUse& use, const ResourceSync& sync)
    {
        const bool image = use.Layout != rhi::ImageLayout::Undefined;

        if (image && use.Layout != sync.Layout)
        {
            AddLayoutChange(plan, copy, use, sync);
            return;
        }

        const bool written = !IsEmpty(sync.Write);

        if (Reads(use) && written)
        {
            const SyncScope read = ReadPart(use);

            if (!Contains(sync.VisibleTo, read))
            {
                AddBarrier(plan, copy, sync.Write, read, sync.Layout);
            }
        }

        if (!Writes(use))
        {
            return;
        }

        const SyncScope write = WritePart(use);

        if (written)
        {
            AddBarrier(plan, copy, sync.Write, write, sync.Layout);
        }

        if (!IsEmpty(sync.ReadsSinceWrite))
        {
            // an execution dependency: the reads made nothing that the write must see
            const SyncScope reads{ .Stages = sync.ReadsSinceWrite.Stages, .Accesses = rhi::Access::None };
            AddBarrier(plan, copy, reads, write, sync.Layout);
        }
    }

    // The state after a level: a barrier made the last write visible to its After scope, and each use then counts.
    void Apply(ResourceSync& sync, const PassUse& use)
    {
        if (Writes(use))
        {
            sync.Write = WritePart(use);
            sync.VisibleTo = SyncScope{};
            sync.ReadsSinceWrite = SyncScope{};
            sync.HasContents = true;
            return;
        }

        Merge(sync.ReadsSinceWrite, ReadPart(use));
    }

    // OnNoHistory::Clear: a clear is a transfer write of the whole copy. Its batch waits for every earlier access and
    // puts an image in General, which vkCmdClearColorImage allows.
    void PlanClears(const CompiledData& data, std::span<ResourceSync> sync, FramePlan& plan)
    {
        if (plan.Clears.empty())
        {
            return;
        }

        const int32_t firstBarrier = static_cast<int32_t>(plan.Barriers.size());
        const SyncScope clear{ .Stages = rhi::PipelineStage::AllTransfer, .Accesses = rhi::Access::TransferWrite };

        for (const int32_t copy : plan.Clears)
        {
            ResourceSync& state = sync[copy];
            const bool image = data.Frozen.Resources[data.CopyRows[copy]].Kind == ResourceKind::Image;
            const rhi::ImageLayout newLayout = image ? rhi::ImageLayout::General : rhi::ImageLayout::Undefined;
            const SyncScope before{ .Stages = state.Write.Stages | state.ReadsSinceWrite.Stages, .Accesses = state.Write.Accesses };

            plan.Barriers.push_back(PlannedBarrier
            {
                .Resource = copy,
                .Before = before,
                .After = clear,
                .OldLayout = image ? state.Layout : rhi::ImageLayout::Undefined,
                .NewLayout = newLayout,
            });

            state.Write = clear;
            state.VisibleTo = SyncScope{};
            state.ReadsSinceWrite = SyncScope{};
            state.Layout = newLayout;
            state.HasContents = true;
        }

        const int32_t count = static_cast<int32_t>(plan.Barriers.size()) - firstBarrier;
        plan.Batches.push_back(BarrierBatch{ .BeforeRun = k_BeforeClears, .FirstBarrier = firstBarrier, .Count = count });
    }

    void CloseBatch(FramePlan& plan, int32_t before_run, int32_t first_barrier, std::span<ResourceSync> sync)
    {
        const int32_t count = static_cast<int32_t>(plan.Barriers.size()) - first_barrier;

        for (int32_t barrierIndex = first_barrier; barrierIndex < first_barrier + count; ++barrierIndex)
        {
            const PlannedBarrier& barrier = plan.Barriers[barrierIndex];
            ResourceSync& row = sync[barrier.Resource];
            plan.BatchBarrier[barrier.Resource] = -1;

            // A layout change is a write in the stages after it, ordered after every earlier access. Keeping the old
            // write's stages and accesses lets a later barrier from Write chain through the change.
            if (barrier.ChangesLayout())
            {
                row.Write = SyncScope{ .Stages = row.Write.Stages | barrier.After.Stages, .Accesses = row.Write.Accesses };
                row.VisibleTo = barrier.After;
                row.ReadsSinceWrite = SyncScope{};
                row.Layout = barrier.NewLayout;
                continue;
            }

            // a barrier from the last write makes it visible to the scope after it
            if (!IsEmpty(row.Write) && Contains(barrier.Before, row.Write))
            {
                Merge(row.VisibleTo, barrier.After);
            }
        }

        if (count > 0)
        {
            plan.Batches.push_back(BarrierBatch{ .BeforeRun = before_run, .FirstBarrier = first_barrier, .Count = count });
        }
    }

}

    void PlanBarriers(const CompiledGraph& compiled,
                      std::span<const ResourceState> resources,
                      std::span<ResourceSync> sync,
                      int64_t frame_index,
                      FramePlan& plan)
    {
        const CompiledData& data = compiled.Data();
        plan.Barriers.clear();
        plan.Batches.clear();

        // a new object owes nothing to the work on the old one
        for (size_t copy = 0u; copy < sync.size(); ++copy)
        {
            const ResourceState& state = resources[data.CopyRows[copy]];

            if (sync[copy].Epoch != state.Epoch)
            {
                sync[copy] = ResourceSync{ .Layout = state.InitialLayout, .Epoch = state.Epoch };
            }
        }

        PlanClears(data, sync, plan);

        const int32_t runCount = static_cast<int32_t>(plan.Runs.size());
        int32_t levelStart = 0;

        while (levelStart < runCount)
        {
            const int32_t level = data.Ordered.Levels[plan.Runs[levelStart]];
            int32_t levelEnd = levelStart;

            while (levelEnd < runCount && data.Ordered.Levels[plan.Runs[levelEnd]] == level)
            {
                ++levelEnd;
            }

            const int32_t firstBarrier = static_cast<int32_t>(plan.Barriers.size());

            for (int32_t runIndex = levelStart; runIndex < levelEnd; ++runIndex)
            {
                const CompiledPass& pass = data.Passes[plan.Runs[runIndex]];

                for (int32_t useIndex = pass.FirstUse; useIndex < pass.FirstUse + pass.UseCount; ++useIndex)
                {
                    const PassUse& use = data.Uses[useIndex];
                    const int32_t copy = CopyOf(data, use.Resource, frame_index, use.Previous);
                    NeedsOf(plan, copy, use, sync[copy]);
                }
            }

            CloseBatch(plan, levelStart, firstBarrier, sync);

            for (int32_t runIndex = levelStart; runIndex < levelEnd; ++runIndex)
            {
                const CompiledPass& pass = data.Passes[plan.Runs[runIndex]];

                for (int32_t useIndex = pass.FirstUse; useIndex < pass.FirstUse + pass.UseCount; ++useIndex)
                {
                    const PassUse& use = data.Uses[useIndex];
                    Apply(sync[CopyOf(data, use.Resource, frame_index, use.Previous)], use);
                }
            }

            levelStart = levelEnd;
        }
    }

}
