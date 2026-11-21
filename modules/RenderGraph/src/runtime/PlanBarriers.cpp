#include "CompiledGraph.hpp"
#include "PlanBarriers.hpp"
#include "FramePlan.hpp"
#include "PassStatements.hpp"
#include "compile/CompiledData.hpp"
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
//  - a write after reads needs a barrier from the reads to the write (an execution dependency).
namespace loom
{

namespace
{

    bool Contains(const SyncScope& outer, const SyncScope& inner)
    {
        const bool stages = (inner.Stages & ~outer.Stages) == SyncStage::None;
        const bool accesses = (inner.Accesses & ~outer.Accesses) == SyncAccess::None;
        return stages && accesses;
    }

    bool IsEmpty(const SyncScope& scope)
    {
        return scope.Stages == SyncStage::None;
    }

    void Merge(SyncScope& into, const SyncScope& scope)
    {
        into.Stages |= scope.Stages;
        into.Accesses |= scope.Accesses;
    }

    constexpr SyncAccess k_ReadAccesses = SyncAccess::IndirectRead |
                                          SyncAccess::UniformRead |
                                          SyncAccess::ShaderRead |
                                          SyncAccess::TransferRead;
    constexpr SyncAccess k_WriteAccesses = SyncAccess::ShaderWrite | SyncAccess::TransferWrite;

    SyncScope ReadPart(const PassUse& use)
    {
        return SyncScope{ .Stages = use.Scope.Stages, .Accesses = use.Scope.Accesses & k_ReadAccesses };
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
    void AddBarrier(FramePlan& plan, int32_t row, const SyncScope& before, const SyncScope& after)
    {
        int32_t& index = plan.BatchBarrier[row];

        if (index < 0)
        {
            index = static_cast<int32_t>(plan.Barriers.size());
            plan.Barriers.push_back(PlannedBarrier{ .Resource = row, .Before = before, .After = after });
            return;
        }

        Merge(plan.Barriers[index].Before, before);
        Merge(plan.Barriers[index].After, after);
    }

    // The barriers that one use needs against the state before its level.
    void NeedsOf(FramePlan& plan, const PassUse& use, const ResourceSync& sync)
    {
        const bool written = !IsEmpty(sync.Write);

        if (Reads(use) && written)
        {
            const SyncScope read = ReadPart(use);

            if (!Contains(sync.VisibleTo, read))
            {
                AddBarrier(plan, use.Resource, sync.Write, read);
            }
        }

        if (!Writes(use))
        {
            return;
        }

        const SyncScope write = WritePart(use);

        if (written)
        {
            AddBarrier(plan, use.Resource, sync.Write, write);
        }

        if (!IsEmpty(sync.ReadsSinceWrite))
        {
            // an execution dependency: the reads made nothing that the write must see
            const SyncScope reads{ .Stages = sync.ReadsSinceWrite.Stages, .Accesses = SyncAccess::None };
            AddBarrier(plan, use.Resource, reads, write);
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
            return;
        }

        Merge(sync.ReadsSinceWrite, ReadPart(use));
    }

    void CloseBatch(FramePlan& plan, int32_t before_run, int32_t first_barrier, std::span<ResourceSync> sync)
    {
        const int32_t count = static_cast<int32_t>(plan.Barriers.size()) - first_barrier;

        for (int32_t barrierIndex = first_barrier; barrierIndex < first_barrier + count; ++barrierIndex)
        {
            const PlannedBarrier& barrier = plan.Barriers[barrierIndex];
            ResourceSync& row = sync[barrier.Resource];
            plan.BatchBarrier[barrier.Resource] = -1;

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
                      FramePlan& plan)
    {
        const CompiledData& data = compiled.Data();
        plan.Barriers.clear();
        plan.Batches.clear();

        // a new object owes nothing to the work on the old one
        for (size_t row = 0u; row < sync.size(); ++row)
        {
            if (sync[row].Epoch != resources[row].Epoch)
            {
                sync[row] = ResourceSync{ .Epoch = resources[row].Epoch };
            }
        }

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
                    NeedsOf(plan, use, sync[use.Resource]);
                }
            }

            CloseBatch(plan, levelStart, firstBarrier, sync);

            for (int32_t runIndex = levelStart; runIndex < levelEnd; ++runIndex)
            {
                const CompiledPass& pass = data.Passes[plan.Runs[runIndex]];

                for (int32_t useIndex = pass.FirstUse; useIndex < pass.FirstUse + pass.UseCount; ++useIndex)
                {
                    const PassUse& use = data.Uses[useIndex];
                    Apply(sync[use.Resource], use);
                }
            }

            levelStart = levelEnd;
        }
    }

}
