#pragma once
#ifndef DIAMOND_DOGS_LOOM_BIND_CONTEXT_HPP
#define DIAMOND_DOGS_LOOM_BIND_CONTEXT_HPP

// The Bind stage's shared state. Each step's functions are in the header named for its source file (Layout.hpp,
// Slots.hpp, Params.hpp, Helpers.hpp). The public part (BoundGraph, Bind) is in BoundGraph.hpp.
namespace loom
{
    class DiagnosticSink;
    struct FrozenGraph;

    namespace bind
    {
        struct BindContext
        {
            const FrozenGraph& Frozen;
            DiagnosticSink& Sink;
        };
    }
}

#endif // !DIAMOND_DOGS_LOOM_BIND_CONTEXT_HPP
