#pragma once
#ifndef DIAMOND_DOGS_LOOM_VULKAN_GRAPH_HPP
#define DIAMOND_DOGS_LOOM_VULKAN_GRAPH_HPP
#include "CompiledGraph.hpp"
#include "FramePlan.hpp"
#include "LoomDeviceLimits.hpp"
#include "LoomKeys.hpp"
#include "ResourceFuture.hpp"
#include "RhiHandle.hpp"
#include "GpuTimers.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <source_location>
#include <span>
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

    // How dispatches bind their shaders. ShaderObjects is the engine's path. ComputePipelines builds a VkPipeline from
    // the same code and layout: synchronization validation tracks a dispatch's descriptor accesses only through a bound
    // VkPipeline, so the GPU tests run each graph this way too. An explicit choice, with no default.
    enum class PipelineMode : uint8_t
    {
        Invalid = 0,
        ShaderObjects = 1,
        ComputePipelines = 2
    };

    // `Sink` receives the runtime diagnostics (LM701 to LM704) and must outlive the Graph. FrameSlots is the count of
    // frames in flight: one command pool and one descriptor set per pass for each.
    struct RealizeInputs
    {
        ResourceContext& Resources;
        const rhi::Device& Device;
        int32_t FrameSlots{ 0 };
        PassTimestamps Timestamps{ PassTimestamps::Invalid };
        PipelineMode Pipelines{ PipelineMode::Invalid };
        DiagnosticSink& Sink;
    };

    // Why Realize refused a graph. The "NotYet" values name work of later steps; the rest are API failures.
    enum class RealizeError : uint8_t
    {
        Invalid = 0,
        FrameSlotCount = 1,             // FrameSlots < 1
        TimestampsUnset = 2,
        CodeNotSpirv = 3,               // the bundle's profile does not hold SPIR-V
        PassKindNotYet = 4,             // a Draw or Record pass (step 7)
        ResourceKindNotYet = 5,         // an image or an imported image (step 6)
        HistoryNotYet = 6,              // HistoryDepth > 0 or a BindPrevious (step 6)
        RunPolicyNotYet = 7,            // WhenInputsChange or OnTrigger (step 6)
        PlacementNotBound = 8,          // a Handle or Pointer form: the bindless and BDA binding model (T2)
        BindingNotYet = 9,              // a texel buffer, an array, or another binding kind that step 5 does not write
        DescriptorLayoutFailed = 10,
        PipelineLayoutFailed = 11,
        ShaderCreateFailed = 12,
        DescriptorPoolFailed = 13,
        CommandPoolFailed = 14,
        PipelineModeUnset = 15,
        PipelineCreateFailed = 16
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
        void Enable(PassKey pass, bool enabled);

        // For the app's ResourceContext::Tick: the graph's timeline, and the value its next submit signals.
        [[nodiscard]] rhi::TimelineSemaphoreHandle Timeline() const noexcept;
        [[nodiscard]] uint64_t PendingValue() const noexcept;
        // The last submit: wait on it before a transfer reads what the graph wrote. Value 0 before the first submit.
        [[nodiscard]] rhi::GpuWait LastSubmit() const noexcept;

        // The object of a buffer row, or a null resource while it does not exist.
        [[nodiscard]] GraphicsResource BufferOf(ResourceKey<Buffer> resource) const noexcept;
        [[nodiscard]] const FramePlan& LastPlan() const noexcept;
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
