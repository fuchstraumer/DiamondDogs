#pragma once
#ifndef DIAMOND_DOGS_LOOM_PASS_STATEMENTS_HPP
#define DIAMOND_DOGS_LOOM_PASS_STATEMENTS_HPP
#include "RhiFlags.hpp"
#include "math/Math.hpp"
#include <cstdint>

// The values a draw or a Record pass states: attachment load and store, blend, depth, raster state, and the uses of a
// Record pass. Cull, front face and depth compare use RhiSystem's enums, as samplers use RhiResources' types; only
// Lodestone types stay out of Loom's public headers.
namespace loom
{

    // What a Record pass's callback receives: handles for its declared uses, and no barrier call (Q1). Realize defines it.
    class RecordContext;

    // How a color target starts. Each kind carries only the values it uses, so a clear color cannot be forgotten.
    struct ColorLoad
    {
        enum class Kind : uint8_t
        {
            Invalid = 0,
            Keep = 1,           // the previous contents
            Clear = 2,
            DontCare = 3        // every pixel is written, so the previous contents do not matter
        };

        Kind Kind{ Kind::Invalid };
        math::Float4 ClearColor{ 0.0f, 0.0f, 0.0f, 0.0f };    // Clear only, in the target's own encoding

        static ColorLoad Keep() noexcept;
        static ColorLoad Clear(float red, float green, float blue, float alpha) noexcept;
        static ColorLoad DontCare() noexcept;
    };

    struct DepthLoad
    {
        enum class Kind : uint8_t
        {
            Invalid = 0,
            Keep = 1,
            Clear = 2,
            DontCare = 3
        };

        Kind Kind{ Kind::Invalid };
        float ClearDepth{ -1.0f };      // Clear only. Vulkan accepts 0 to 1, so -1 is never a real clear value.

        static DepthLoad Keep() noexcept;
        static DepthLoad Clear(float depth) noexcept;
        static DepthLoad DontCare() noexcept;
    };

    enum class AttachmentStore : uint8_t
    {
        Invalid = 0,
        Store = 1,
        DontCare = 2        // a transient target: nothing after this pass reads it
    };

    // Per color target. Presets, not the full blend equation: these four cover the engine's passes.
    enum class BlendMode : uint8_t
    {
        Invalid = 0,
        Opaque = 1,
        AlphaBlend = 2,         // src * a + dst * (1 - a)
        Additive = 3,           // src + dst
        Premultiplied = 4       // src + dst * (1 - a)
    };

    // Whether a depth-tested draw writes depth. A draw that only tests reads the depth target, and Order treats it as a
    // reader, not a writer.
    enum class DepthWrite : uint8_t
    {
        Invalid = 0,
        Enabled = 1,
        Disabled = 2
    };

    enum class PrimitiveTopology : uint8_t
    {
        Invalid = 0,
        TriangleList = 1,
        TriangleStrip = 2,
        LineList = 3,
        PointList = 4
    };

    enum class PolygonMode : uint8_t
    {
        Invalid = 0,
        Fill = 1,
        Line = 2
    };

    // A draw with no RasterState statement uses these values.
    struct RasterState
    {
        PrimitiveTopology Topology{ PrimitiveTopology::TriangleList };
        PolygonMode Polygon{ PolygonMode::Fill };
        rhi::CullMode Cull{ rhi::CullMode::None };
        rhi::FrontFace Front{ rhi::FrontFace::CounterClockwise };
    };

    enum class IndexType : uint8_t
    {
        Invalid = 0,
        UInt16 = 1,
        UInt32 = 2
    };

    // A Record pass states its uses because Loom cannot see into the callback. Order computes its hazards from these
    // alone (Q1).
    enum class ResourceAccess : uint8_t
    {
        Invalid = 0,
        Read = 1,
        Write = 2,
        ReadWrite = 3
    };

    enum class ImageUse : uint8_t
    {
        Invalid = 0,
        Sampled = 1,
        Storage = 2,
        ColorTarget = 3,
        DepthTarget = 4,
        TransferSource = 5,
        TransferDestination = 6
    };

    enum class BufferUse : uint8_t
    {
        Invalid = 0,
        Uniform = 1,
        Storage = 2,
        Vertex = 3,
        Index = 4,
        Indirect = 5,
        TransferSource = 6,
        TransferDestination = 7
    };

}

#endif // !DIAMOND_DOGS_LOOM_PASS_STATEMENTS_HPP
