#pragma once
#ifndef DIAMOND_DOGS_LOOM_DECIDE_RUNS_HPP
#define DIAMOND_DOGS_LOOM_DECIDE_RUNS_HPP
#include "FramePlan.hpp"
#include <cstdint>
#include <span>
#include <string>

// DecideRuns.cpp: which passes run this Tick, and why the others skip (spec 5.1 step 3). Pure: the inputs are the
// compiled graph, the readiness the backend polled, and the pass controls.
namespace loom
{
    class CompiledGraph;

    // Decides each pass in run order: enabled, run policy, pipeline readiness, resource readiness, history, then
    // propagation (Q3g). Fills Decisions, Runs and Clears. `resources` has one entry per resource row, `pipelines` one
    // per pipeline (CompiledGraph::PipelineCount), `controls` one per pass, and `sync` one per copy (it tells whether
    // last frame's copy has contents). `frame_index` counts the submitted frames.
    void DecideRuns(const CompiledGraph& compiled,
                    std::span<const ResourceState> resources,
                    std::span<const PipelineReadiness> pipelines,
                    std::span<const PassControl> controls,
                    std::span<const ResourceSync> sync,
                    int64_t frame_index,
                    FramePlan& plan);

    // After a submit: marks the passes that ran (Once, first run), clears their change and trigger flags, and carries
    // a change forward to each WhenInputsChange pass that did not run but whose input a running pass wrote.
    void CommitRuns(const CompiledGraph& compiled, const FramePlan& plan, std::span<PassControl> controls);

    // WhenInputsChange rule a: a row got a new object (Supply). Marks every pass that reads `row`.
    void MarkRowChanged(const CompiledGraph& compiled, int32_t row, std::span<PassControl> controls);
    // WhenInputsChange rule b: a params key got new bytes. `params` is an index into CompiledData::Params.
    void MarkParamsChanged(const CompiledGraph& compiled, int32_t params, std::span<PassControl> controls);

    // The last Tick's decisions as text: each pass in run order, "ran" or the skip reason and its cause.
    [[nodiscard]] std::string ExplainLastTick(const CompiledGraph& compiled, const FramePlan& plan);

}

#endif // !DIAMOND_DOGS_LOOM_DECIDE_RUNS_HPP
