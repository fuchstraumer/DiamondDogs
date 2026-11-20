#pragma once
#ifndef DIAMOND_DOGS_LOOM_SIZED_GRAPH_HPP
#define DIAMOND_DOGS_LOOM_SIZED_GRAPH_HPP
#include "ResourceFlags.hpp"
#include <cstdint>
#include <optional>
#include <vector>

// The Size stage's output: the creation facts that the shaders and the pass statements give each resource. The format,
// the mip count and the layer count come from the declaration, so Size does not copy them.
namespace loom
{
    class DiagnosticSink;
    struct BoundGraph;
    struct FrozenGraph;

    // Unknown size: an imported buffer (its future brings the size), an image or a sampler.
    inline constexpr int64_t k_UnknownByteSize = -1;

    // Unknown extent: a buffer, a sampler, an image sized from a frame target (Realize knows the target's size), or an
    // import whose extent comes with its content.
    inline constexpr int32_t k_UnknownExtent = -1;

    struct ImageExtent
    {
        int32_t Width{ k_UnknownExtent };
        int32_t Height{ k_UnknownExtent };
        int32_t Depth{ k_UnknownExtent };
    };

    struct SizedResource
    {
        // buffers
        int64_t ByteSize{ k_UnknownByteSize };
        // every GPU use of a buffer. CPU access (map, copy for an upload) belongs to Realize.
        lodestone::BufferUsageFlags BufferUsage{ lodestone::BufferUsageFlags::Invalid };

        // images
        ImageExtent Extent;
        // from the binding shapes: 1D, 3D, or 2D for every other shape and for an image that no shader binds
        lodestone::TextureDimension Dimension{ lodestone::TextureDimension::Invalid };
        // every GPU use of an image. A copy for an upload belongs to Realize.
        lodestone::TextureUsageFlags TextureUsage{ lodestone::TextureUsageFlags::Invalid };
    };

    // One row for each FrozenGraph::Resources row, at the same index.
    struct SizedGraph
    {
        std::vector<SizedResource> Resources;
    };

    // Reports every Size error it finds (LM401, LM402, LM403), not only the first one. Returns no graph when it
    // reported an error. Buffer sizes compare in bytes: two typed views of one buffer (an atomic uint view and a uint4
    // view, for example) have different strides and the same byte size. Image extents compare per axis.
    [[nodiscard]] std::optional<SizedGraph> Size(const FrozenGraph& frozen,
                                                 const BoundGraph& bound,
                                                 DiagnosticSink& sink);

}

#endif // !DIAMOND_DOGS_LOOM_SIZED_GRAPH_HPP
