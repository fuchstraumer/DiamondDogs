#pragma once
#ifndef DIAMOND_DOGS_LOOM_SIZED_GRAPH_HPP
#define DIAMOND_DOGS_LOOM_SIZED_GRAPH_HPP
#include "ResourceFlags.hpp"
#include <cstdint>
#include <optional>
#include <vector>

// The Size stage's output: the creation facts that the shaders and the pass statements give each resource. Buffers
// only, for now: images wait for the image format decision (LM403).
namespace loom
{
    class DiagnosticSink;
    struct BoundGraph;
    struct FrozenGraph;

    // Unknown size: an imported buffer (its future brings the size), an image or a sampler.
    inline constexpr int64_t k_UnknownByteSize = -1;

    struct SizedResource
    {
        int64_t ByteSize{ k_UnknownByteSize };
        // every GPU use of a buffer. CPU access (map, copy for an upload) belongs to Realize.
        lodestone::BufferUsageFlags Usage{ lodestone::BufferUsageFlags::Invalid };
    };

    // One row for each FrozenGraph::Resources row, at the same index.
    struct SizedGraph
    {
        std::vector<SizedResource> Resources;
    };

    // Reports every Size error it finds (LM401, LM402), not only the first one. Returns no graph when it reported an
    // error. Sizes compare in bytes: two typed views of one buffer (an atomic uint view and a uint4 view, for example)
    // have different strides and the same byte size.
    [[nodiscard]] std::optional<SizedGraph> Size(const FrozenGraph& frozen,
                                                 const BoundGraph& bound,
                                                 DiagnosticSink& sink);

}

#endif // !DIAMOND_DOGS_LOOM_SIZED_GRAPH_HPP
