#pragma once
#ifndef DIAMOND_DOGS_LOOM_VULKAN_GRAPH_HPP
#define DIAMOND_DOGS_LOOM_VULKAN_GRAPH_HPP
#include "CompiledGraph.hpp"
#include "AliasLayout.hpp"
#include "FramePlan.hpp"
#include "FrameTarget.hpp"
#include "TargetExtents.hpp"
#include "LoomDeviceLimits.hpp"
#include "LoomKeys.hpp"
#include "ResourceFuture.hpp"
#include "RhiHandle.hpp"
#include "RhiResourceFlags.hpp"
#include "GpuTimers.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <source_location>
#include <span>
#include <string>
#include <string_view>

class ResourceContext;

namespace rhi
{
    class Device;
}

// loom_vulkan: the Vulkan backend of Loom. Realize turns a CompiledGraph into a runtime Graph on one
// device; Tick decides, records and submits one frame without blocking. The core decides what runs and which
// barriers go where (FramePlan.hpp); this backend creates the objects, polls the futures, and records.
namespace loom
{
    class DiagnosticSink;
    struct GraphImpl;

    // Per-pass GPU timestamps, through rhi::GpuTimers (compiled out of Release). An explicit choice, with no default.
    enum class PassTimestamps : uint8_t
    {
        Invalid = 0,
        Off = 1,
        On = 2
    };

    // `Sink` receives the runtime diagnostics (LM701 to LM705) and must outlive the Graph. FrameSlots is the count of
    // frames in flight: one command pool and one descriptor set per pass for each.
    // FrameTargets names the object of each imported FrameTargetKey; every one must outlive the Graph.
    // TargetSettleSeconds is how long a frame target's extent must keep one value before the OfTarget images follow it
    // (step 7, round 2 Q1; about 0.15 is a good start). It must be set, to 0 or more, even with no frame targets.
    // Memory aliasing (AliasLayout.hpp). Off gives every row its own memory: turn it off to find out whether a bug
    // comes from aliasing. OnPoisoned is a debug mode: On, and each placed buffer is filled with k_AliasPoisonWord at
    // its takeover, so a pass that reads bytes it did not write this frame fails every time, not now and then.
    enum class MemoryAliasing : uint8_t
    {
        Invalid = 0,
        Off = 1,
        On = 2,
        OnPoisoned = 3
    };

    struct RealizeInputs
    {
        ResourceContext& Resources;
        const rhi::Device& Device;
        int32_t FrameSlots{ 0 };
        PassTimestamps Timestamps{ PassTimestamps::Invalid };
        MemoryAliasing Aliasing{ MemoryAliasing::Invalid };
        DiagnosticSink& Sink;
        std::span<const FrameTargetBinding> FrameTargets;
        double TargetSettleSeconds{ -1.0 };
    };

    // Why Realize refused a graph. The "NotYet" values name work of later steps; the rest are API failures.
    enum class RealizeError : uint8_t
    {
        Invalid = 0,
        FrameSlotCount = 1,             // FrameSlots < 1
        TimestampsUnset = 2,
        CodeNotSpirv = 3,               // the bundle's profile does not hold SPIR-V
        PassKindNotYet = 4,             // no longer used: Draw and Record passes run since step 7
        ResourceKindNotYet = 5,         // a cube image, a format that RhiResources cannot create, or more frame targets
                                        // than LoomFrame holds (k_LoomMaxFrameTargets)
        PlacementNotBound = 6,          // a Handle or Pointer form: the bindless and BDA binding model (T2)
        BindingNotYet = 7,              // a texel buffer, an array, a combined texture-sampler, an input target, or a
                                        // bound vertex buffer (step 7 Q8: vertices are pulled)
        DescriptorLayoutFailed = 8,
        PipelineLayoutFailed = 9,
        ShaderCreateFailed = 10,        // no longer used: shader objects are made on the compile thread (LM705)
        DescriptorPoolFailed = 11,
        CommandPoolFailed = 12,
        FrameTargetUnbound = 13,        // an imported FrameTargetKey with no RealizeInputs::FrameTargets entry
        SettleUnset = 14,               // RealizeInputs::TargetSettleSeconds < 0
        DrawStateUnavailable = 15,      // a Draw pass, and the device lacks the dynamic-state entry points
        AliasingUnset = 16              // RealizeInputs::Aliasing is Invalid
    };

    [[nodiscard]] std::string_view ToString(RealizeError error) noexcept;

    class Graph;
    using RealizeResult = std::expected<std::unique_ptr<Graph>, RealizeError>;

    class Graph
    {
    public:
        // Creates every graph-declared resource through ResourceContext and keeps the futures, builds the layouts and
        // the shader objects, and registers the graph's timeline with the context. Does not block.
        [[nodiscard]] static RealizeResult Realize(CompiledGraph&& compiled, const RealizeInputs& inputs);

        // Blocks: waits for the GPU to finish the graph's last submit, then destroys what the graph created.
        ~Graph();
        Graph(const Graph&) = delete;
        Graph& operator=(const Graph&) = delete;
        Graph(Graph&&) = delete;
        Graph& operator=(Graph&&) = delete;

        // Never blocks (T1). One frame at most.
        [[nodiscard]] TickResult Tick(std::chrono::duration<double> delta_time);
        // Never blocks, and records nothing: moves resource creation on (the placed rows, once their heaps exist),
        // polls every row and every pipeline, and returns true when every resource row exists (Created or Complete)
        // and every pipeline is Ready. An image sized from a frame target does not count: Tick makes it from the
        // target's extent. Before then, a Tick skips each pass whose row is missing (NotCreated) or whose pipeline is
        // still compiling (PipelinePending) and runs the rest, so an app that needs whole frames (a simulation step)
        // calls this until it is true, and only then ticks. A Failed row or pipeline keeps it false.
        [[nodiscard]] bool Ready();
        // Desktop only, and never inside Tick: blocks until the next Tick's frame slot is free, or the timeout passes.
        // Returns false on a timeout.
        [[nodiscard]] bool WaitForFrameSlot(std::chrono::nanoseconds timeout);

        template<typename T>
        void Set(ParamsKey<T> params, const T& value, std::source_location where = std::source_location::current())
        {
            setParams(params.ToRef(), std::as_bytes(std::span{ &value, 1u }), static_cast<uint32_t>(alignof(T)), where);
        }

        // An imported buffer's object. Supply again to replace it: the new future starts a new epoch. The supplier
        // keeps owning the buffer, and frees an old one only after LastSubmit() passes (CC-R4). A key that is not an
        // imported buffer of this graph is ignored.
        void Supply(ResourceKey<Buffer> resource, rhi::ResourceFuture<GraphicsResource>&& future);
        // An imported image's object, and the usage it is in when the graph gets it (its first barrier starts from
        // that usage's layout). The rules of the buffer form apply: a new epoch, and the supplier keeps owning it.
        void Supply(ResourceKey<Image> resource, rhi::ResourceFuture<GraphicsResource>&& future,
                    ImageUsageBits usage_at_handoff);
        // As above, and every submit that uses this object first waits for `supplier_wait` on the GPU: outside work
        // on another queue (a video encoder's read of the last frame's image) that must end before the graph writes it.
        // An empty wait (Value 0, or not Submitted) is the same as the form above. Vulkan only.
        void Supply(ResourceKey<Image> resource, rhi::ResourceFuture<GraphicsResource>&& future,
                    ImageUsageBits usage_at_handoff, const rhi::GpuWait& supplier_wait);
        void Enable(PassKey pass, bool enabled);
        // An OnTrigger pass runs on the next Tick whose readiness allows it. Triggers before that run count as one.
        void Trigger(PassKey pass);
        // The last Tick's decisions as text: each pass, "ran" or why it skipped.
        [[nodiscard]] std::string ExplainLastTick() const;

        // The scalability hook (step 7 Q4): the render extent of the target is its extent x scale, 0 < scale <= 1.
        // Viewports change at once and no image is made again. False, and no change, for another scale or key.
        [[nodiscard]] bool SetRenderScale(FrameTargetKey target, float scale);
        // A frame target's three extents (TargetExtents.hpp), or nothing for a key the graph did not import
        [[nodiscard]] std::optional<TargetExtents> ExtentsOf(FrameTargetKey target) const noexcept;

        // For the app's ResourceContext::Tick: the graph's timeline, and the value its next submit signals.
        [[nodiscard]] rhi::TimelineSemaphoreHandle Timeline() const noexcept;
        [[nodiscard]] uint64_t PendingValue() const noexcept;
        // The last submit: wait on it before a transfer reads what the graph wrote. Value 0 before the first submit.
        [[nodiscard]] rhi::GpuWait LastSubmit() const noexcept;

        // The object of a buffer row, or a null resource while it does not exist.
        [[nodiscard]] GraphicsResource BufferOf(ResourceKey<Buffer> resource) const noexcept;
        // The object of an image row, or a null resource while it does not exist.
        [[nodiscard]] GraphicsResource ImageOf(ResourceKey<Image> resource) const noexcept;
        [[nodiscard]] const FramePlan& LastPlan() const noexcept;
        // Which rows share memory, and where. Empty with MemoryAliasing::Off, or when no two rows can share.
        [[nodiscard]] const AliasLayout& Aliases() const noexcept;
        // The heaps and the rows in each, as text
        [[nodiscard]] std::string ExplainAliasing() const;
        [[nodiscard]] const CompiledGraph& Compiled() const noexcept;
        // The per-pass times of the latest collected frame, when Realize had PassTimestamps::On.
        [[nodiscard]] std::optional<rhi::GpuTimingBlock> LatestTimings() const;

    private:
        explicit Graph(std::unique_ptr<GraphImpl> _impl) noexcept;
        void setParams(KeyRef params, std::span<const std::byte> bytes, uint32_t alignment, std::source_location where);

        std::unique_ptr<GraphImpl> impl;
    };

    // The device facts that Compile needs, read from the physical device.
    [[nodiscard]] ShaderDeviceLimits ShaderLimitsOf(const rhi::Device& device);
    [[nodiscard]] LayoutLimits LayoutLimitsOf(const rhi::Device& device);

}

#endif // !DIAMOND_DOGS_LOOM_VULKAN_GRAPH_HPP
