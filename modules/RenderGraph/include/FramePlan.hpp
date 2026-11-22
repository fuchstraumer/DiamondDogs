#pragma once
#ifndef DIAMOND_DOGS_LOOM_FRAME_PLAN_HPP
#define DIAMOND_DOGS_LOOM_FRAME_PLAN_HPP
#include "RhiFlags.hpp"
#include <cstdint>
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
    // InitialLayout is the layout an image is in when its epoch starts: Undefined for a graph image, and the layout of
    // the usage that Supply stated for an import. Buffers ignore it.
    struct ResourceState
    {
        ResourceReadiness Readiness{ ResourceReadiness::Invalid };
        uint32_t Epoch{ 0u };
        rhi::ImageLayout InitialLayout{ rhi::ImageLayout::Undefined };
    };

    // The stages and the accesses of one use, in the RHI's barrier vocabulary (rhi_types links no RHI). The values
    // match Vulkan's synchronization2 bits; a DX12 or WebGPU backend maps them.
    struct SyncScope
    {
        rhi::PipelineStage Stages{ rhi::PipelineStage::None };
        rhi::Access Accesses{ rhi::Access::None };

        bool operator==(const SyncScope&) const noexcept = default;
    };

    // What the GPU still owes on one resource row, carried from Tick to Tick. A barrier batch reads it; the accesses
    // of each level then update it. Epoch is the ResourceState epoch it belongs to.
    struct ResourceSync
    {
        SyncScope Write;            // the last write; None when nothing wrote since the object was made
        SyncScope VisibleTo;        // the read scopes that a barrier already made the last write visible to
        SyncScope ReadsSinceWrite;  // the reads after the last write: a new write must wait for them
        rhi::ImageLayout Layout{ rhi::ImageLayout::Undefined };    // images: the layout after the last barrier
        uint32_t Epoch{ 0u };
        bool HasContents{ false };  // a pass wrote it, or a clear did, since the object was made (OnNoHistory)
    };

    // A pass's control state, set by the graph (Enable, Trigger, Set, Supply) and by the core after each submit.
    struct PassControl
    {
        bool Enabled{ true };
        bool RanOnce{ false };          // the pass ran and was submitted at least once
        bool InputsChanged{ true };     // WhenInputsChange: a read row or params key changed since the pass last ran
        bool Triggered{ false };        // OnTrigger: Trigger was called since the pass last ran
    };

    // Why a pass did not run this Tick (ExplainLastTick). Cause is the resource row for NotCreated, Failed and
    // NoHistory, and the pass for Propagated; -1 for the others.
    enum class SkipReason : uint8_t
    {
        Invalid = 0,
        Ran = 1,
        Policy = 2,         // Once and already ran, WhenInputsChange with nothing changed, OnTrigger not triggered
        Disabled = 3,
        NotCreated = 4,
        Failed = 5,
        Propagated = 6,     // a skipped initializing writer of something this pass reads
        NoHistory = 7       // a BindPrevious read with no previous frame yet, and OnNoHistory::Skip
    };

    struct PassDecision
    {
        SkipReason Reason{ SkipReason::Invalid };
        int32_t Cause{ -1 };
    };

    // A barrier whose layouts differ changes an image's layout; the backend records it as an image barrier. Every
    // other barrier merges into the batch's one memory barrier.
    struct PlannedBarrier
    {
        int32_t Resource{ -1 };
        SyncScope Before;
        SyncScope After;
        rhi::ImageLayout OldLayout{ rhi::ImageLayout::Undefined };
        rhi::ImageLayout NewLayout{ rhi::ImageLayout::Undefined };

        [[nodiscard]] bool ChangesLayout() const noexcept
        {
            return OldLayout != NewLayout;
        }
    };

    // The barriers that go before Runs[BeforeRun]: one batch at each level boundary, and one at frame start (before
    // the first run), which covers the work of earlier frames. Barriers[FirstBarrier, FirstBarrier + Count).
    // BeforeRun k_BeforeClears: the batch before the clears of OnNoHistory::Clear, which come before every run.
    inline constexpr int32_t k_BeforeClears = -1;

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
        std::vector<int32_t> Clears;            // the copies that OnNoHistory::Clear zeroes before the first run

        // PlannedBarrier::Resource, Clears, BatchBarrier and RowLayouts count copies (CompiledData::CopyRows): a row
        // with no history is its own copy.

        // scratch, [resource]: the skipped initializing writer (Q3g), a row's barrier in the open batch, and whether
        // a pass that runs this Tick writes the row (WhenInputsChange rule c)
        std::vector<int32_t> SkippedWriter;
        std::vector<int32_t> BatchBarrier;
        std::vector<uint8_t> WrittenThisTick;
        std::vector<rhi::ImageLayout> RowLayouts;   // [copy]: an image's layout as the batches are planned
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
