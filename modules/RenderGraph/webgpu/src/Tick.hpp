#pragma once
#ifndef DIAMOND_DOGS_LOOM_WEBGPU_TICK_HPP
#define DIAMOND_DOGS_LOOM_WEBGPU_TICK_HPP
#include <cstdint>

// Tick.cpp: the backend's steps of one Tick (spec 5.1). Graph::Tick (LoomGraph.cpp) runs them in this order, around
// the core's DecideRuns. Dawn tracks hazards, so there is no barrier planning step.
namespace loom
{
    struct GraphImpl;

    // Step 2: polls each row's future into graph.States. Reports LM701 and LM702 once per epoch.
    void PollRows(GraphImpl& graph);

    // Step 2: moves each pipeline that CreateComputePipelineAsync made into its PipelineRuntime, and reports LM705
    // once per pipeline that failed.
    void PollPipelines(GraphImpl& graph);

    // A row with a new object (a new epoch) owes nothing to the old one: its copies start again with no contents.
    // Run it before DecideRuns, which reads what each copy holds.
    void RefreshSync(GraphImpl& graph);

    // Writes the params bytes of each running pass that takes constants in a uniform buffer into the shared buffer.
    // All of them go before the commands that read them are recorded.
    void UploadParams(GraphImpl& graph);

    // Builds a bind group only for a (pass, phase, group) with a row whose epoch changed since the last build, or
    // that was never built. A steady-state frame builds none.
    void UpdateBindGroups(GraphImpl& graph);

    // Steps 6 and 7: records the plan's clears and passes into one command encoder, and submits its command buffer.
    // False when the recording failed.
    [[nodiscard]] bool SubmitFrame(GraphImpl& graph);

    // After a submit: the copies that a clear or a running pass wrote now have contents (OnNoHistory).
    void MarkContents(GraphImpl& graph);

}

#endif // !DIAMOND_DOGS_LOOM_WEBGPU_TICK_HPP
