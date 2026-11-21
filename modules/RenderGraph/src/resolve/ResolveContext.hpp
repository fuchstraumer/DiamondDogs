#pragma once
#ifndef DIAMOND_DOGS_LOOM_RESOLVE_CONTEXT_HPP
#define DIAMOND_DOGS_LOOM_RESOLVE_CONTEXT_HPP
#include <cstdint>
#include <vector>

// The Resolve stage's shared state. Each step's functions are in the header named for its source file (Shaders.hpp,
// Pins.hpp, Variants.hpp, Helpers.hpp). The public part (ResolveInputs, ResolvedGraph, Resolve) is in
// ResolvedGraph.hpp.
namespace loom
{
    class DiagnosticSink;
    struct FrozenGraph;
    struct ResolveInputs;

    namespace resolve
    {
        struct ResolveContext
        {
            const FrozenGraph& Frozen;
            const ResolveInputs& Inputs;
            DiagnosticSink& Sink;

            // [graph pin] 1 once a resolved module declares the pin's axis. LM206 reports each pin still at 0.
            std::vector<uint8_t> GraphPinDeclared;
        };
    }
}

#endif // !DIAMOND_DOGS_LOOM_RESOLVE_CONTEXT_HPP
