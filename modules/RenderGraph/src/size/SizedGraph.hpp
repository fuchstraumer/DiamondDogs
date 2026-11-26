#pragma once
#ifndef DIAMOND_DOGS_LOOM_SIZED_GRAPH_HPP
#define DIAMOND_DOGS_LOOM_SIZED_GRAPH_HPP
#include "ResourceFlags.hpp"
#include <cstdint>
#include <vector>

// The Size stage's output: the creation facts that the shaders and the pass statements give each resource. The format
// and the mip count come from the declaration, so Size does not copy them.
namespace loom
{

    // Unknown size: an image or a sampler.
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
        // the array layer count: Fixed, Derived from the stated counts, or an import's expectation. k_UnknownExtent
        // after an LM402.
        int32_t Layers{ k_UnknownExtent };
        // from the binding shapes: 1D, 3D, or 2D for every other shape and for an image that no shader binds
        lodestone::TextureDimension Dimension{ lodestone::TextureDimension::Invalid };
        // every GPU use of an image. A copy for an upload belongs to Realize.
        lodestone::TextureUsageFlags TextureUsage{ lodestone::TextureUsageFlags::Invalid };
        // a binding reads the image as an array, or as a cube: the view that Realize makes must match
        bool Arrayed{ false };
        bool Cube{ false };
    };

    // One row for each FrozenGraph::Resources row, at the same index. The stage that fills it, Size(), is in Size.hpp.
    struct SizedGraph
    {
        std::vector<SizedResource> Resources;
    };

}

#endif // !DIAMOND_DOGS_LOOM_SIZED_GRAPH_HPP
