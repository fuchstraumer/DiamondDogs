#pragma once
#ifndef DIAMOND_DOGS_LOOM_DECIDE_RUNS_HPP
#define DIAMOND_DOGS_LOOM_DECIDE_RUNS_HPP
#include "FramePlan.hpp"
#include <span>

// DecideRuns.cpp: which passes run this Tick, and why the others skip (spec 5.1 step 3). Pure: the inputs are the
// compiled graph, the readiness the backend polled, and the pass controls.
namespace loom
{
    class CompiledGraph;

    // Decides each pass in run order: enabled, run policy, readiness, then propagation (Q3g). Fills Decisions and
    // Runs. `resources` has one entry per resource row, `controls` one per pass.
    void DecideRuns(const CompiledGraph& compiled,
                    std::span<const ResourceState> resources,
                    std::span<const PassControl> controls,
                    FramePlan& plan);

    // After a submit: marks the Once passes that ran.
    void CommitRuns(const CompiledGraph& compiled, const FramePlan& plan, std::span<PassControl> controls);

}

#endif // !DIAMOND_DOGS_LOOM_DECIDE_RUNS_HPP
