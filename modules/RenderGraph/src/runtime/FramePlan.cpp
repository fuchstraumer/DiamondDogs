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

        plan.Decisions.assign(passCount, PassDecision{});
        plan.Runs.clear();
        plan.Runs.reserve(passCount);

        // at most one barrier per use, and one batch per level plus the frame-start batch
        plan.Barriers.clear();
        plan.Barriers.reserve(data.Uses.size());
        plan.Batches.clear();
        plan.Batches.reserve(static_cast<size_t>(data.Ordered.LevelCount) + 1u);

        plan.SkippedWriter.assign(resourceCount, -1);
        plan.BatchBarrier.assign(resourceCount, -1);
    }

}
