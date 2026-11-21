#pragma once
#ifndef DIAMOND_DOGS_LOOM_FRAME_PLAN_HPP
#define DIAMOND_DOGS_LOOM_FRAME_PLAN_HPP
#include "utility/EnumFlagUtils.hpp"
#include <cstdint>
#include <span>
#include <vector>

// The core runtime's data: what the backend tells the core each Tick (the readiness of each resource), and what the
// core tells the backend (which passes run, why the others skip, and the barriers between the levels). Every type
// here is plain, so the decisions are tested with no GPU, and a WebGPU backend reads the same plan.
namespace loom
{
    class CompiledGraph;

    // The backend's view of one resource row, polled once per Tick. It maps its futures to these values: Pending for
    // a future that is not Created yet, or that is Created with a transfer that is not submitted yet (Q4).
    enum class ResourceReadiness : uint8_t
    {
        Invalid = 0,
        Pending = 1,        // nothing to record against yet
        Created = 2,        // the object exists; a transfer may still be on its way, and the submit waits for it
        Complete = 3,       // the contents are on the GPU
        Failed = 4          // permanent (LM702)
    };

    // Epoch counts the objects a row has had: Supply of a new future, or the graph's own create, adds one. A new
    // object starts with no pending GPU work, so the barrier state of its row starts again.
    struct ResourceState
    {
        ResourceReadiness Readiness{ ResourceReadiness::Invalid };
        uint32_t Epoch{ 0u };
    };

    // The stages and the accesses of one use, in Loom's own terms. Each backend maps them to its API.
    enum class SyncStage : uint8_t
    {
        None = 0,
        Indirect = 1 << 0,      // the read of indirect arguments
        Vertex = 1 << 1,
        Fragment = 1 << 2,
        Compute = 1 << 3,
        Transfer = 1 << 4
    };

    MAKE_ENUM_CLASS_FLAGS(SyncStage);

    enum class SyncAccess : uint8_t
    {
        None = 0,
        IndirectRead = 1 << 0,
        UniformRead = 1 << 1,
        ShaderRead = 1 << 2,
        ShaderWrite = 1 << 3,
        TransferRead = 1 << 4,
        TransferWrite = 1 << 5
    };

    MAKE_ENUM_CLASS_FLAGS(SyncAccess);

    struct SyncScope
    {
        SyncStage Stages{ SyncStage::None };
        SyncAccess Accesses{ SyncAccess::None };

        bool operator==(const SyncScope&) const noexcept = default;
    };

    // What the GPU still owes on one resource row, carried from Tick to Tick. A barrier batch reads it; the accesses
    // of each level then update it. Epoch is the ResourceState epoch it belongs to.
    struct ResourceSync
    {
        SyncScope Write;            // the last write; None when nothing wrote since the object was made
        SyncScope VisibleTo;        // the read scopes that a barrier already made the last write visible to
        SyncScope ReadsSinceWrite;  // the reads after the last write: a new write must wait for them
        uint32_t Epoch{ 0u };
    };

    // A pass's control state, set by the graph's Enable and by the core after each submit.
    struct PassControl
    {
        bool Enabled{ true };
        bool RanOnce{ false };      // a Once pass that ran and was submitted
    };

    // Why a pass did not run this Tick (ExplainLastTick). Cause is the resource row for NotCreated and Failed, and the
    // pass for Propagated; -1 for the others.
    enum class SkipReason : uint8_t
    {
        Invalid = 0,
        Ran = 1,
        Policy = 2,         // Once and already ran. WhenInputsChange and OnTrigger come in step 6.
        Disabled = 3,
        NotCreated = 4,
        Failed = 5,
        Propagated = 6      // a skipped initializing writer of something this pass reads (Q3g)
    };

    struct PassDecision
    {
        SkipReason Reason{ SkipReason::Invalid };
        int32_t Cause{ -1 };
    };

    struct PlannedBarrier
    {
        int32_t Resource{ -1 };
        SyncScope Before;
        SyncScope After;
    };

    // The barriers that go before Runs[BeforeRun]: one batch at each level boundary, and one at frame start (before
    // the first run), which covers the work of earlier frames. Barriers[FirstBarrier, FirstBarrier + Count).
    struct BarrierBatch
    {
        int32_t BeforeRun{ -1 };
        int32_t FirstBarrier{ 0 };
        int32_t Count{ 0 };
    };

    // One Tick's plan. PrepareFramePlan sizes every array once, at Realize, so a Tick allocates nothing.
    struct FramePlan
    {
        std::vector<PassDecision> Decisions;    // [pass]
        std::vector<int32_t> Runs;              // the passes that run, in run order
        std::vector<PlannedBarrier> Barriers;
        std::vector<BarrierBatch> Batches;

        // scratch, [resource]: the skipped initializing writer (Q3g), and a row's barrier in the open batch
        std::vector<int32_t> SkippedWriter;
        std::vector<int32_t> BatchBarrier;
    };

    // FramePlan.cpp: sizes every array of `plan` for the graph.
    void PrepareFramePlan(const CompiledGraph& compiled, FramePlan& plan);

    // The functions that fill a plan are in DecideRuns.hpp and PlanBarriers.hpp.

    // What one Tick did (T1). Tick never blocks: each status that is not Submitted means "try again next Tick".
    enum class TickStatus : uint8_t
    {
        Invalid = 0,
        Submitted = 1,
        NoFrameSlot = 2,        // the GPU still runs the frame that last used this frame slot
        TargetNotReady = 3,     // a frame target could not be acquired (step 7)
        NothingToRun = 4,       // every pass skipped; FramePlan::Decisions says why
        Failed = 5              // the sink holds the diagnostics (LM703), or the recording or the submit failed
    };

    struct TickResult
    {
        TickStatus Status{ TickStatus::Invalid };
        int32_t Ran{ 0 };
        int32_t Skipped{ 0 };
    };

}

#endif // !DIAMOND_DOGS_LOOM_FRAME_PLAN_HPP
