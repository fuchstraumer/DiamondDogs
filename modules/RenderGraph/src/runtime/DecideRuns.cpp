#include "CompiledGraph.hpp"
#include "DecideRuns.hpp"
#include "Declarations.hpp"
#include "FramePlan.hpp"
#include "PassStatements.hpp"
#include "compile/CompiledData.hpp"
#include <algorithm>
#include <cstdint>
#include <span>

// Which passes run this Tick, and why the others skip (spec 5.1 step 3). Pure: the inputs are the compiled graph, the
// readiness the backend polled, and the pass controls.
namespace loom
{

namespace
{

    // Readiness first: a Failed row stops the pass for good, a row with nothing to record against stops it this Tick.
    PassDecision ReadinessOf(const CompiledData& data, const CompiledPass& pass, std::span<const ResourceState> resources)
    {
        for (int32_t useIndex = pass.FirstUse; useIndex < pass.FirstUse + pass.UseCount; ++useIndex)
        {
            const int32_t row = data.Uses[useIndex].Resource;
            const ResourceReadiness readiness = resources[row].Readiness;

            if (readiness == ResourceReadiness::Failed)
            {
                return PassDecision{ .Reason = SkipReason::Failed, .Cause = row };
            }

            const bool recordable = readiness == ResourceReadiness::Created || readiness == ResourceReadiness::Complete;

            if (!recordable)
            {
                return PassDecision{ .Reason = SkipReason::NotCreated, .Cause = row };
            }
        }

        return PassDecision{ .Reason = SkipReason::Ran, .Cause = -1 };
    }

    // Q3g: a pass that reads a row whose initializing writer skipped earlier in this Tick skips too.
    PassDecision PropagationOf(const CompiledData& data, const CompiledPass& pass, const FramePlan& plan)
    {
        for (int32_t useIndex = pass.FirstUse; useIndex < pass.FirstUse + pass.UseCount; ++useIndex)
        {
            const PassUse& use = data.Uses[useIndex];
            const bool reads = use.Access == ResourceAccess::Read || use.Access == ResourceAccess::ReadWrite;
            const int32_t writer = plan.SkippedWriter[use.Resource];

            if (reads && writer >= 0)
            {
                return PassDecision{ .Reason = SkipReason::Propagated, .Cause = writer };
            }
        }

        return PassDecision{ .Reason = SkipReason::Ran, .Cause = -1 };
    }

    PassDecision Decide(const CompiledData& data,
                        int32_t pass_index,
                        std::span<const ResourceState> resources,
                        std::span<const PassControl> controls,
                        const FramePlan& plan)
    {
        const PassControl& control = controls[pass_index];

        if (!control.Enabled)
        {
            return PassDecision{ .Reason = SkipReason::Disabled, .Cause = -1 };
        }

        const RunPolicy policy = data.Frozen.Passes[pass_index].Policy;
        const bool onceDone = policy == RunPolicy::Once && control.RanOnce;
        // WhenInputsChange and OnTrigger come with step 6: until then they never run
        const bool notYet = policy == RunPolicy::WhenInputsChange || policy == RunPolicy::OnTrigger;

        if (onceDone || notYet)
        {
            return PassDecision{ .Reason = SkipReason::Policy, .Cause = -1 };
        }

        const CompiledPass& pass = data.Passes[pass_index];
        const PassDecision readiness = ReadinessOf(data, pass, resources);

        if (readiness.Reason != SkipReason::Ran)
        {
            return readiness;
        }

        return PropagationOf(data, pass, plan);
    }

}

    void DecideRuns(const CompiledGraph& compiled,
                    std::span<const ResourceState> resources,
                    std::span<const PassControl> controls,
                    FramePlan& plan)
    {
        const CompiledData& data = compiled.Data();
        plan.Runs.clear();
        std::ranges::fill(plan.SkippedWriter, -1);

        for (const PassIndex passIndex : data.Ordered.Passes)
        {
            const PassDecision decision = Decide(data, passIndex.Value, resources, controls, plan);
            plan.Decisions[passIndex.Value] = decision;

            const CompiledPass& pass = data.Passes[passIndex.Value];
            const bool ran = decision.Reason == SkipReason::Ran;
            // a Once pass that already ran left valid contents behind, so its readers still run
            const bool propagates = !ran && decision.Reason != SkipReason::Policy;

            if (ran)
            {
                plan.Runs.push_back(passIndex.Value);
            }

            for (int32_t useIndex = pass.FirstUse; useIndex < pass.FirstUse + pass.UseCount; ++useIndex)
            {
                const PassUse& use = data.Uses[useIndex];
                const bool writes = use.Access == ResourceAccess::Write || use.Access == ResourceAccess::ReadWrite;

                // a pass that ran and wrote the row gives its readers contents again
                if (ran && writes)
                {
                    plan.SkippedWriter[use.Resource] = -1;
                }

                if (propagates && use.Initializes)
                {
                    plan.SkippedWriter[use.Resource] = passIndex.Value;
                }
            }
        }
    }

    void CommitRuns(const CompiledGraph& compiled, const FramePlan& plan, std::span<PassControl> controls)
    {
        const CompiledData& data = compiled.Data();

        for (const int32_t pass : plan.Runs)
        {
            if (data.Frozen.Passes[pass].Policy == RunPolicy::Once)
            {
                controls[pass].RanOnce = true;
            }
        }
    }

}
