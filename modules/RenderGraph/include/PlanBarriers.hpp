#pragma once
#ifndef DIAMOND_DOGS_LOOM_PLAN_BARRIERS_HPP
#define DIAMOND_DOGS_LOOM_PLAN_BARRIERS_HPP
#include "AliasLayout.hpp"
#include "FramePlan.hpp"
#include <cstdint>
#include <span>

// PlanBarriers.cpp: the barriers of one Tick, batched by level (spec 5.1 step 6).
namespace loom
{
    class CompiledGraph;

    // Fills Barriers and Batches for plan.Clears and plan.Runs, and moves `sync` (one entry per copy) to the state
    // after them. A copy whose row's epoch changed starts with no pending work. `frame_index` picks each history
    // ring's copies. Call it once per submitted frame: it assumes the plan's clears and passes are recorded.
    // `aliases` is the backend's packing (empty when nothing shares memory): each placed row's first use in the Tick
    // waits for every row that shares its bytes, and an image starts from Undefined.
    void PlanBarriers(const CompiledGraph& compiled,
                      std::span<const ResourceState> resources,
                      std::span<ResourceSync> sync,
                      const AliasLayout& aliases,
                      int64_t frame_index,
                      FramePlan& plan);

}

#endif // !DIAMOND_DOGS_LOOM_PLAN_BARRIERS_HPP
