#pragma once
#ifndef DIAMOND_DOGS_LOOM_SIZE_HPP
#define DIAMOND_DOGS_LOOM_SIZE_HPP
#include "size/SizedGraph.hpp"
#include <optional>

// Size.cpp: the Size stage. Buffer sizes are here; image extents are in ImageSize.cpp.
namespace loom
{
    class DiagnosticSink;
    struct BoundGraph;
    struct FrozenGraph;

    // Reports every Size error it finds (LM401, LM402, LM403), not only the first one. Returns no graph when it
    // reported an error. Buffer sizes compare in bytes: two typed views of one buffer (an atomic uint view and a uint4
    // view, for example) have different strides and the same byte size. Image extents compare per axis.
    [[nodiscard]] std::optional<SizedGraph> Size(const FrozenGraph& frozen,
                                                 const BoundGraph& bound,
                                                 DiagnosticSink& sink);

}

#endif // !DIAMOND_DOGS_LOOM_SIZE_HPP
