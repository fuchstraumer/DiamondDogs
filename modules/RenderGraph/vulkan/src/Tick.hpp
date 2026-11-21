#pragma once
#ifndef DIAMOND_DOGS_LOOM_VULKAN_TICK_HPP
#define DIAMOND_DOGS_LOOM_VULKAN_TICK_HPP
#include <cstdint>

// Tick.cpp: the backend's steps of one Tick (spec 5.1). Graph::Tick (LoomGraph.cpp) runs them in this order, around
// the core's DecideRuns and PlanBarriers.
namespace loom
{
    struct GraphImpl;

    // Step 2: polls each row's future into graph.States. Reports LM701 and LM702 once per epoch.
    void PollRows(GraphImpl& graph);

    // Rewrites the frame slot's descriptor sets where a row changed epoch since the last write. The GPU must be done
    // with the slot.
    void WriteDescriptors(GraphImpl& graph, int32_t frame_slot);

    // Steps 6 and 7: records the plan's barrier batches and passes into the slot's command buffer, and submits it
    // with the waits (CC-R2) and the signal of `signal_value`. False when the recording or the submit failed.
    [[nodiscard]] bool SubmitFrame(GraphImpl& graph, int32_t frame_slot, uint64_t signal_value);

}

#endif // !DIAMOND_DOGS_LOOM_VULKAN_TICK_HPP
