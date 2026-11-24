#include "FramePlan.hpp"
#include "CompiledGraph.hpp"
#include "compile/CompiledData.hpp"
#include <cstddef>

namespace loom
{

    void PrepareFramePlan(const CompiledGraph& compiled, FramePlan& plan)
    {
        const CompiledData& data = compiled.Data();
        const size_t passCount = data.Passes.size();
        const size_t rowCount = static_cast<size_t>(data.RowCount);
        const size_t copyCount = data.CopyRows.size();

        plan.Decisions.assign(passCount, PassDecision{});
        plan.Runs.clear();
        plan.Runs.reserve(passCount);
        plan.TargetActions.assign(data.FrameTargets.size(), TargetAction::Skip);

        // at most one barrier per use, one per clear, and one per frame target at frame end; one batch per level, plus
        // the clear, frame-start and frame-end batches
        plan.Barriers.clear();
        plan.Barriers.reserve(data.Uses.size() + 2u * copyCount);
        plan.Clears.clear();
        plan.Clears.reserve(copyCount);
        plan.Batches.clear();
        plan.Batches.reserve(static_cast<size_t>(data.Ordered.LevelCount) + 3u);

        plan.SkippedWriter.assign(rowCount, -1);
        plan.BatchBarrier.assign(copyCount, -1);
        plan.WrittenThisTick.assign(rowCount, 0u);
    }

}
