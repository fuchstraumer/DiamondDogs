#pragma once
#ifndef DIAMOND_DOGS_LOOM_VULKAN_TICK_HPP
#define DIAMOND_DOGS_LOOM_VULKAN_TICK_HPP
#include <cstdint>

// Tick.cpp: the backend's steps of one Tick (spec 5.1). Graph::Tick (LoomGraph.cpp) runs them in this order, around
// the core's DecideRuns and PlanBarriers.
namespace loom
{
    struct GraphImpl;

    // Step 7: issues the destroys of retired objects that exist, and drops the destroys that finished.
    void RetireObjects(GraphImpl& graph);

    // Step 7, before the decisions: feeds each frame target's extent to the settle rule (TargetExtents.hpp), makes
    // the OfTarget images when an allocation changes, and swaps a target's new images in once all of them exist.
    void UpdateTargets(GraphImpl& graph, double delta_seconds);

    // Step 2: polls each row's future into graph.States. Reports LM701 and LM702 once per epoch.
    void PollRows(GraphImpl& graph);

    // Step 4: acquires each frame target that this Tick renders or clears, and starts its row's new epoch. False when
    // one could not be acquired (TargetNotReady); a target acquired already stays acquired for the next Tick.
    [[nodiscard]] bool AcquireTargets(GraphImpl& graph);

    // Step 8: hands each acquired target back
    void PresentTargets(GraphImpl& graph);

    // The LoomFrame values of this Tick
    void UpdateFrameConstants(GraphImpl& graph);

    // Rewrites the frame slot's descriptor sets where a row changed epoch since the last write. The GPU must be done
    // with the slot.
    void WriteDescriptors(GraphImpl& graph, int32_t frame_slot);

    // Steps 6 and 7: records the plan's barrier batches and passes into the slot's command buffer, and submits it
    // with the waits (CC-R2, and each acquired target) and the signals (`signal_value` on the timeline, and each
    // acquired swapchain image's render-done semaphore). False when the recording or the submit failed.
    [[nodiscard]] bool SubmitFrame(GraphImpl& graph, int32_t frame_slot, uint64_t signal_value);

}

#endif // !DIAMOND_DOGS_LOOM_VULKAN_TICK_HPP
