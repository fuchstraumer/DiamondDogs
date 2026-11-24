#pragma once
#ifndef DIAMOND_DOGS_LOOM_WEBGPU_GRAPH_HPP
#define DIAMOND_DOGS_LOOM_WEBGPU_GRAPH_HPP
#include "CompiledGraph.hpp"
#include "FramePlan.hpp"
#include "LoomDeviceLimits.hpp"
#include "LoomKeys.hpp"
#include "ResourceFuture.hpp"
#include "RhiResourceFlags.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <source_location>
#include <span>
#include <string>
#include <string_view>

class ResourceContext;

namespace rhi
{
    class RhiContext;
}

// loom_webgpu: the WebGPU backend of Loom, with the same vocabulary as loom_vulkan. Realize turns a CompiledGraph into
// a runtime Graph on one device; Tick decides, records and submits one frame without blocking. Dawn tracks hazards, so
// this backend plans no barriers. The core decides what runs; this backend creates the objects and records.
namespace loom
{
    class DiagnosticSink;
    struct GraphImpl;

    // Accepted for parity with loom_vulkan. WebGPU has no GPU timers here, so On and Off behave the same.
    enum class PassTimestamps : uint8_t
    {
        Invalid = 0,
        Off = 1,
        On = 2
    };

    // `Sink` receives the runtime diagnostics (LM701 to LM704) and must outlive the Graph. FrameSlots is accepted for
    // parity with loom_vulkan: WebGPU needs no per-frame objects, so it must only be 1 or more.
    struct RealizeInputs
    {
        ResourceContext& Resources;
        const rhi::RhiContext& Context;
        int32_t FrameSlots{ 0 };
        PassTimestamps Timestamps{ PassTimestamps::Invalid };
        DiagnosticSink& Sink;
    };

    // Why Realize refused a graph. The "NotYet" values name work of later steps; the rest are API failures.
    enum class RealizeError : uint8_t
    {
        Invalid = 0,
        FrameSlotCount = 1,             // FrameSlots < 1
        TimestampsUnset = 2,
        CodeNotWgsl = 3,                // the bundle's profile does not hold WGSL
        PassKindNotYet = 4,             // a Draw or Record pass
        ResourceKindNotYet = 5,         // a frame target or an image sized from one, a cube image, or a format that
                                        // RhiResources cannot create
        PlacementNotBound = 6,          // a Handle or Pointer form
        BindingNotYet = 7,              // a texel buffer, an array, a combined texture-sampler, an input target, or
                                        // push constant delivery
        BindGroupLayoutFailed = 8,
        PipelineLayoutFailed = 9,
        ShaderCreateFailed = 10,
        PipelineCreateFailed = 11,
        ParamsBufferFailed = 12,
        BindGroupMaxExceeded = 13 // WGPU can only have up to 4 bind group layouts per pipeline
    };

    [[nodiscard]] std::string_view ToString(RealizeError error) noexcept;

    class Graph;
    using RealizeResult = std::expected<std::unique_ptr<Graph>, RealizeError>;

    class Graph
    {
    public:
        // Creates every graph-declared resource through ResourceContext and keeps the futures, builds the layouts and
        // the pipelines, and the one buffer for the params of every pass. Does not block.
        [[nodiscard]] static RealizeResult Realize(CompiledGraph&& compiled, const RealizeInputs& inputs);

        // Does not block: WebGPU frees what the graph created when the device is done with it.
        ~Graph();
        Graph(const Graph&) = delete;
        Graph& operator=(const Graph&) = delete;
        Graph(Graph&&) = delete;
        Graph& operator=(Graph&&) = delete;

        // Never blocks (T1). One frame at most.
        [[nodiscard]] TickResult Tick(std::chrono::duration<double> delta_time);

        template<typename T>
        void Set(ParamsKey<T> params, const T& value, std::source_location where = std::source_location::current())
        {
            setParams(params.ToRef(), std::as_bytes(std::span{ &value, 1u }), static_cast<uint32_t>(alignof(T)), where);
        }

        // An imported buffer's object. Supply again to replace it: the new future starts a new epoch. The supplier
        // keeps owning the buffer. A key that is not an imported buffer of this graph is ignored.
        void Supply(ResourceKey<Buffer> resource, rhi::ResourceFuture<GraphicsResource>&& future);
        // An imported image's object, and the usage it is in when the graph gets it. The rules of the buffer form apply.
        void Supply(ResourceKey<Image> resource, rhi::ResourceFuture<GraphicsResource>&& future,
                    ImageUsageBits usage_at_handoff);
        void Enable(PassKey pass, bool enabled);
        // An OnTrigger pass runs on the next Tick whose readiness allows it. Triggers before that run count as one.
        void Trigger(PassKey pass);
        // The last Tick's decisions as text: each pass, "ran" or why it skipped.
        [[nodiscard]] std::string ExplainLastTick() const;

        // The object of a buffer row, or a null resource while it does not exist.
        [[nodiscard]] GraphicsResource BufferOf(ResourceKey<Buffer> resource) const noexcept;
        // The object of an image row, or a null resource while it does not exist.
        [[nodiscard]] GraphicsResource ImageOf(ResourceKey<Image> resource) const noexcept;
        [[nodiscard]] const FramePlan& LastPlan() const noexcept;
        [[nodiscard]] const CompiledGraph& Compiled() const noexcept;

    private:
        explicit Graph(std::unique_ptr<GraphImpl> _impl) noexcept;
        void setParams(KeyRef params, std::span<const std::byte> bytes, uint32_t alignment, std::source_location where);

        std::unique_ptr<GraphImpl> impl;
    };

    // The device facts that Compile needs, read from the WebGPU device of the context.
    [[nodiscard]] ShaderDeviceLimits ShaderLimitsOf(const rhi::RhiContext& context);
    [[nodiscard]] LayoutLimits LayoutLimitsOf(const rhi::RhiContext& context);

}

#endif // !DIAMOND_DOGS_LOOM_WEBGPU_GRAPH_HPP
