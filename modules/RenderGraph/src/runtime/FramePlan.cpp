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
        const size_t resourceCount = data.Frozen.Resources.size();
        const size_t copyCount = data.CopyRows.size();

        plan.Decisions.assign(passCount, PassDecision{});
        plan.Runs.clear();
        plan.Runs.reserve(passCount);

        // at most one barrier per use, and one batch per level plus the frame-start batch
        plan.Barriers.clear();
        plan.Barriers.reserve(data.Uses.size() + copyCount);
        plan.Clears.clear();
        plan.Clears.reserve(copyCount);
        plan.Batches.clear();
        plan.Batches.reserve(static_cast<size_t>(data.Ordered.LevelCount) + 2u);

        plan.SkippedWriter.assign(resourceCount, -1);
        plan.BatchBarrier.assign(copyCount, -1);
        plan.WrittenThisTick.assign(resourceCount, 0u);
        plan.RowLayouts.assign(copyCount, rhi::ImageLayout::Undefined);
    }

}
