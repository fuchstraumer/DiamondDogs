#include "CompiledGraph.hpp"
#include "DecideRuns.hpp"
#include "Declarations.hpp"
#include "FramePlan.hpp"
#include "PassStatements.hpp"
#include "compile/CompiledData.hpp"
#include "freeze/FrozenGraph.hpp"
#include "Declarations.hpp"
#include <algorithm>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>

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

            if (reads && writer >= 0 && !use.Previous)
            {
                return PassDecision{ .Reason = SkipReason::Propagated, .Cause = writer };
            }
        }

        return PassDecision{ .Reason = SkipReason::Ran, .Cause = -1 };
    }

    bool Reads(const PassUse& use)
    {
        return use.Access == ResourceAccess::Read || use.Access == ResourceAccess::ReadWrite;
    }

    bool Writes(const PassUse& use)
    {
        return use.Access == ResourceAccess::Write || use.Access == ResourceAccess::ReadWrite;
    }

    // WhenInputsChange rule c: a pass that runs earlier in this Tick wrote a row that this pass reads.
    bool ReadsWrittenRow(const CompiledData& data, const CompiledPass& pass, const FramePlan& plan)
    {
        for (int32_t useIndex = pass.FirstUse; useIndex < pass.FirstUse + pass.UseCount; ++useIndex)
        {
            const PassUse& use = data.Uses[useIndex];

            if (Reads(use) && !use.Previous && plan.WrittenThisTick[use.Resource] != 0u)
            {
                return true;
            }
        }

        return false;
    }

    bool PolicyAllows(const CompiledData& data, int32_t pass_index, const PassControl& control, const FramePlan& plan)
    {
        // changed: we had spare padding bits, policy moved here instead of in cold frozen pass data
        switch (data.Passes[pass_index].Policy)
        {
        case RunPolicy::EveryFrame:
            return true;
        case RunPolicy::Once:
            return !control.RanOnce;
        case RunPolicy::WhenInputsChange:
            return !control.RanOnce || control.InputsChanged || ReadsWrittenRow(data, data.Passes[pass_index], plan);
        case RunPolicy::OnTrigger:
            return control.Triggered;
        default:
            break;
        }

        return false;
    }

    OnNoHistory NoHistoryActionOf(const FrozenGraph& frozen, int32_t row)
    {
        const FrozenResource& resource = frozen.Resources[row];

        if (resource.Kind == ResourceKind::Image)
        {
            return frozen.Images[resource.DeclarationIndex].Declaration.NoHistoryAction;
        }

        return frozen.Buffers[resource.DeclarationIndex].NoHistoryAction;
    }

    // A BindPrevious read of a copy that holds nothing yet: Skip gives NoHistory; Clear runs, and the copy is cleared
    // first (PushClears, once the pass is known to run).
    PassDecision HistoryOf(const CompiledData& data, const CompiledPass& pass, std::span<const ResourceSync> sync,
                           int64_t frame_index)
    {
        for (int32_t useIndex = pass.FirstUse; useIndex < pass.FirstUse + pass.UseCount; ++useIndex)
        {
            const PassUse& use = data.Uses[useIndex];

            if (!use.Previous)
            {
                continue;
            }

            const bool empty = !sync[CopyOf(data, use.Resource, frame_index, true)].HasContents;

            if (empty && NoHistoryActionOf(data.Frozen, use.Resource) == OnNoHistory::Skip)
            {
                return PassDecision{ .Reason = SkipReason::NoHistory, .Cause = use.Resource };
            }
        }

        return PassDecision{ .Reason = SkipReason::Ran, .Cause = -1 };
    }

    void PushClears(const CompiledData& data, const CompiledPass& pass, std::span<const ResourceSync> sync,
                    int64_t frame_index, FramePlan& plan)
    {
        for (int32_t useIndex = pass.FirstUse; useIndex < pass.FirstUse + pass.UseCount; ++useIndex)
        {
            const PassUse& use = data.Uses[useIndex];

            if (!use.Previous)
            {
                continue;
            }

            const int32_t copy = CopyOf(data, use.Resource, frame_index, true);
            const bool needed = !sync[copy].HasContents && std::ranges::find(plan.Clears, copy) == plan.Clears.end();

            if (needed)
            {
                plan.Clears.push_back(copy);
            }
        }
    }

    PassDecision Decide(const CompiledData& data,
                        int32_t pass_index,
                        std::span<const ResourceState> resources,
                        std::span<const PassControl> controls,
                        std::span<const ResourceSync> sync,
                        int64_t frame_index,
                        const FramePlan& plan,
                        bool all_recordable,
                        bool any_skipped_writer)
    {
        const PassControl& control = controls[pass_index];

        if (!control.Enabled)
        {
            return PassDecision{ .Reason = SkipReason::Disabled, .Cause = -1 };
        }

        if (!PolicyAllows(data, pass_index, control, plan))
        {
            return PassDecision{ .Reason = SkipReason::Policy, .Cause = -1 };
        }

        const CompiledPass& pass = data.Passes[pass_index];

        if (!all_recordable)
        {
            const PassDecision readiness = ReadinessOf(data, pass, resources);
            if (readiness.Reason != SkipReason::Ran)
            {
                return readiness;
            }
        }

        if (pass.ReadsHistory)
        {
            const PassDecision history = HistoryOf(data, pass, sync, frame_index);

            if (history.Reason != SkipReason::Ran)
            {
                return history;
            }
        }

        if (any_skipped_writer)
        {
            return PropagationOf(data, pass, plan);
        }

        return PassDecision{ .Reason = SkipReason::Ran, .Cause = -1 };
    }

    // Step 7: what each frame target needs this Tick. A target that a running pass writes renders; otherwise its
    // fallback decides. A Clear fallback clears the target before the first run (it joins plan.Clears).
    void DecideTargets(const CompiledData& data, FramePlan& plan)
    {
        if (data.FrameTargets.empty())
        {
            return;
        }

        std::ranges::fill(plan.TargetActions, TargetAction::Skip);

        for (const int32_t passIndex : plan.Runs)
        {
            const CompiledPass& pass = data.Passes[passIndex];

            for (int32_t useIndex = pass.FirstUse; useIndex < pass.FirstUse + pass.UseCount; ++useIndex)
            {
                const PassUse& use = data.Uses[useIndex];

                if (IsFrameTargetRow(data, use.Resource) && Writes(use))
                {
                    plan.TargetActions[FrameTargetOfRow(data, use.Resource)] = TargetAction::Render;
                }
            }
        }

        for (size_t target = 0u; target < data.FrameTargets.size(); ++target)
        {
            const CompiledFrameTarget& frameTarget = data.FrameTargets[target];
            const bool clears = frameTarget.Fallback.Kind == OutputFallback::Kind::Clear;

            if (plan.TargetActions[target] == TargetAction::Skip && clears)
            {
                plan.TargetActions[target] = TargetAction::Clear;
                plan.Clears.push_back(frameTarget.Row);
            }
        }
    }

    std::string_view TargetActionText(TargetAction action)
    {
        switch (action)
        {
        case TargetAction::Render:
            return "rendered";
        case TargetAction::Clear:
            return "cleared to its fallback color";
        case TargetAction::Skip:
            return "not acquired, keeps its last frame";
        case TargetAction::Invalid:
            break;
        }

        return "not decided";
    }

}

    void DecideRuns(const CompiledGraph& compiled,
                    std::span<const ResourceState> resources,
                    std::span<const PassControl> controls,
                    std::span<const ResourceSync> sync,
                    int64_t frame_index,
                    FramePlan& plan)
    {
        const CompiledData& data = compiled.Data();
        plan.Runs.clear();
        plan.Clears.clear();
        std::ranges::fill(plan.SkippedWriter, -1);

        // Only fill if the graph contains passes that will need it
        if (data.HasWhenInputsChangesPasses)
        {
            std::ranges::fill(plan.WrittenThisTick, uint8_t{ 0u });
        }

        const auto isRecordable = [](const ResourceState& state)
        {
            return state.Readiness == ResourceReadiness::Created ||
                   state.Readiness == ResourceReadiness::Complete;
        };

        const bool allRecordable = std::ranges::all_of(resources, isRecordable);
        bool anySkippedWriter = false;

        for (const PassIndex passIndex : data.Ordered.Passes)
        {
            const PassDecision decision = Decide(data,
                                                 passIndex.Value,
                                                 resources,
                                                 controls,
                                                 sync,
                                                 frame_index,
                                                 plan,
                                                 allRecordable,
                                                 anySkippedWriter);

            plan.Decisions[passIndex.Value] = decision;

            const CompiledPass& pass = data.Passes[passIndex.Value];
            const bool ran = decision.Reason == SkipReason::Ran;
            // a Once pass that already ran left valid contents behind, so its readers still run
            // and so did a WhenInputsChange or OnTrigger pass that did not need to run
            const bool propagates = !ran && decision.Reason != SkipReason::Policy;

            if (ran)
            {
                plan.Runs.push_back(passIndex.Value);
                // only need to push clears if the pass reads history
                if (pass.ReadsHistory)
                {
                    PushClears(data, pass, sync, frame_index, plan);
                }
            }

            const bool updateUses =
                (ran && (anySkippedWriter || data.HasWhenInputsChangesPasses)) || propagates;

            if (!updateUses)
            {
                continue;
            }

            for (int32_t useIndex = pass.FirstUse; useIndex < pass.FirstUse + pass.UseCount; ++useIndex)
            {
                const PassUse& use = data.Uses[useIndex];
                // a pass that ran and wrote the row gives its readers contents again
                if (ran && Writes(use) && !use.Previous)
                {
                    plan.SkippedWriter[use.Resource] = -1;
                    if (data.HasWhenInputsChangesPasses)
                    {
                        plan.WrittenThisTick[use.Resource] = 1u;
                    }
                }

                if (propagates && use.Initializes)
                {
                    plan.SkippedWriter[use.Resource] = passIndex.Value;
                    anySkippedWriter = true;
                }
            }
        }

        DecideTargets(data, plan);
    }

    void CommitRuns(const CompiledGraph& compiled, const FramePlan& plan, std::span<PassControl> controls)
    {
        const CompiledData& data = compiled.Data();

        if (data.HasWhenInputsChangesPasses)
        {
            // a WhenInputsChange pass that did not run keeps a change that a running pass made to its input
            for (int32_t passIndex = 0; passIndex < static_cast<int32_t>(data.Passes.size()); ++passIndex)
            {
                const bool watches = data.Frozen.Passes[passIndex].Policy == RunPolicy::WhenInputsChange;
                const bool ran = plan.Decisions[passIndex].Reason == SkipReason::Ran;

                if (watches && !ran && ReadsWrittenRow(data, data.Passes[passIndex], plan))
                {
                    controls[passIndex].InputsChanged = true;
                }
            }
        }

        for (const int32_t pass : plan.Runs)
        {
            controls[pass].RanOnce = true;
            controls[pass].InputsChanged = false;
            controls[pass].Triggered = false;
        }

        if (!data.HasWhenInputsChangesPasses)
        {
            return;
        }

        // a row written this Tick is next frame's history: its BindPrevious readers see a change on the next Tick
        for (int32_t passIndex = 0; passIndex < static_cast<int32_t>(data.Passes.size()); ++passIndex)
        {
            const CompiledPass& pass = data.Passes[passIndex];

            for (int32_t useIndex = pass.FirstUse; useIndex < pass.FirstUse + pass.UseCount; ++useIndex)
            {
                const PassUse& use = data.Uses[useIndex];

                if (use.Previous && plan.WrittenThisTick[use.Resource] != 0u)
                {
                    controls[passIndex].InputsChanged = true;
                    break;
                }
            }
        }
    }

    void MarkRowChanged(const CompiledGraph& compiled, int32_t row, std::span<PassControl> controls)
    {
        const CompiledData& data = compiled.Data();

        if (!data.HasWhenInputsChangesPasses)
        {
            return;
        }

        for (int32_t passIndex = 0; passIndex < static_cast<int32_t>(data.Passes.size()); ++passIndex)
        {
            const CompiledPass& pass = data.Passes[passIndex];

            if (pass.Policy != RunPolicy::WhenInputsChange)
            {
                continue;
            }

            for (int32_t useIndex = pass.FirstUse; useIndex < pass.FirstUse + pass.UseCount; ++useIndex)
            {
                const PassUse& use = data.Uses[useIndex];

                if (use.Resource == row && Reads(use))
                {
                    controls[passIndex].InputsChanged = true;
                    break;
                }
            }
        }
    }

    void MarkParamsChanged(const CompiledGraph& compiled, int32_t params, std::span<PassControl> controls)
    {
        const CompiledData& data = compiled.Data();

        if (!data.HasWhenInputsChangesPasses)
        {
            return;
        }

        for (int32_t passIndex = 0; passIndex < static_cast<int32_t>(data.Passes.size()); ++passIndex)
        {
            if (data.Passes[passIndex].Policy == RunPolicy::WhenInputsChange &&
                data.Passes[passIndex].Params == params)
            {
                controls[passIndex].InputsChanged = true;
            }
        }
    }

    std::string ExplainLastTick(const CompiledGraph& compiled, const FramePlan& plan)
    {
        const CompiledData& data = compiled.Data();
        std::string text;

        for (const PassIndex passIndex : data.Ordered.Passes)
        {
            const PassDecision& decision = plan.Decisions[passIndex.Value];
            const std::string_view pass = data.Frozen.Passes[passIndex.Value].Key.Name;

            switch (decision.Reason)
            {
            case SkipReason::Ran:
                text += std::format("{}: ran\n", pass);
                break;
            case SkipReason::Policy:
                text += std::format("{}: skipped, run policy\n", pass);
                break;
            case SkipReason::Disabled:
                text += std::format("{}: skipped, disabled\n", pass);
                break;
            case SkipReason::NotCreated:
                text += std::format("{}: skipped, '{}' does not exist yet\n", pass,
                                    KeyOfRow(data, decision.Cause).Name);
                break;
            case SkipReason::Failed:
                text += std::format("{}: skipped, '{}' failed\n", pass, KeyOfRow(data, decision.Cause).Name);
                break;
            case SkipReason::Propagated:
                text += std::format("{}: skipped, its writer '{}' skipped\n", pass,
                                    data.Frozen.Passes[decision.Cause].Key.Name);
                break;
            case SkipReason::NoHistory:
                text += std::format("{}: skipped, '{}' has no previous frame yet\n", pass,
                                    data.Frozen.Resources[decision.Cause].Key.Name);
                break;
            case SkipReason::Invalid:
                text += std::format("{}: not decided\n", pass);
                break;
            }
        }

        for (size_t target = 0u; target < data.FrameTargets.size(); ++target)
        {
            text += std::format("frame target '{}': {}\n", data.Frozen.FrameTargets[target].Key.Name,
                                TargetActionText(plan.TargetActions[target]));
        }

        return text;
    }

}
