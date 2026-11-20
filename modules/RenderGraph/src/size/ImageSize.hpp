#pragma once
#ifndef DIAMOND_DOGS_LOOM_IMAGE_SIZE_HPP
#define DIAMOND_DOGS_LOOM_IMAGE_SIZE_HPP

namespace loom
{
    class DiagnosticSink;
    struct BoundGraph;
    struct FrozenGraph;
    struct SizedGraph;

    // Fills the extent, the dimension and the usage of each image row in `sized`. Reports LM401 (an extent that does
    // not fit), LM402 (a derived extent with no source) and LM403 (a format or a dimension that does not fit a use).
    void SizeImages(const FrozenGraph& frozen, const BoundGraph& bound, DiagnosticSink& sink, SizedGraph& sized);

}

#endif // !DIAMOND_DOGS_LOOM_IMAGE_SIZE_HPP
