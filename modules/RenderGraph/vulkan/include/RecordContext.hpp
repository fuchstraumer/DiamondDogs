#pragma once
#ifndef DIAMOND_DOGS_LOOM_VULKAN_RECORD_CONTEXT_HPP
#define DIAMOND_DOGS_LOOM_VULKAN_RECORD_CONTEXT_HPP
#include "FrameTarget.hpp"
#include "LoomKeys.hpp"
#include "ResourceTypes.hpp"
#include "TargetExtents.hpp"
#include <cstdint>
#include <optional>

namespace rhi
{
    struct CommandBuffer;
}

// What a Record pass's callback receives (spec Q1, step 7 Q7). It gives the command buffer and the objects of the
// keys that the pass declared in Uses statements; any other key gives a null resource, because Loom planned no
// barrier for it. There is no barrier call: Loom records every barrier from the declared uses.
//
// When the pass declared a ColorTarget or DepthTarget use, Loom has begun rendering into those targets (load Keep,
// store Store) and set the viewport and scissor to RenderExtent(), and ends rendering after the callback.
//
// After the callback, Loom treats all command buffer state as unknown (step 7, round 2 Q5): it binds shaders,
// descriptor sets and dynamic state again before the next pass. The callback may change any of them.
namespace loom
{
    struct GraphImpl;

    class RecordContext
    {
    public:
        RecordContext(GraphImpl& graph, rhi::CommandBuffer& command_buffer, int32_t pass, Extent2D render_extent) noexcept;

        [[nodiscard]] rhi::CommandBuffer& CommandBuffer() noexcept;
        [[nodiscard]] GraphicsResource BufferOf(ResourceKey<Buffer> resource) const noexcept;
        [[nodiscard]] GraphicsResource ImageOf(ResourceKey<Image> resource) const noexcept;
        // The acquired image of a frame target that the pass declared, or nothing
        [[nodiscard]] std::optional<AcquiredTarget> TargetOf(FrameTargetKey target) const noexcept;
        // The extent of the rendering scope; empty when the pass declared no target
        [[nodiscard]] Extent2D RenderExtent() const noexcept;

    private:
        [[nodiscard]] bool declares(int32_t row) const noexcept;

        GraphImpl& graph;
        rhi::CommandBuffer& commandBuffer;
        int32_t pass;
        Extent2D renderExtent;
    };

}

#endif // !DIAMOND_DOGS_LOOM_VULKAN_RECORD_CONTEXT_HPP
