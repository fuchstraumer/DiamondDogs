#pragma once
#ifndef DIAMOND_DOGS_LOOM_PLAN_BARRIERS_HPP
#define DIAMOND_DOGS_LOOM_PLAN_BARRIERS_HPP
#include "FramePlan.hpp"
#include <span>

// PlanBarriers.cpp: the barriers of one Tick, batched by level (spec 5.1 step 6).
namespace loom
{
    class CompiledGraph;

    // Fills Barriers and Batches for plan.Runs, and moves `sync` to the state after them. A row whose epoch changed
    // starts with no pending work. Call it once per submitted frame: it assumes the plan's passes are recorded.
    void PlanBarriers(const CompiledGraph& compiled,
                      std::span<const ResourceState> resources,
                      std::span<ResourceSync> sync,
                      FramePlan& plan);

}

#endif // !DIAMOND_DOGS_LOOM_PLAN_BARRIERS_HPP
