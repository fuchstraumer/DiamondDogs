#pragma once
#ifndef DIAMOND_DOGS_LOOM_LAYOUT_CONTEXT_HPP
#define DIAMOND_DOGS_LOOM_LAYOUT_CONTEXT_HPP

// The Layout stage's shared state. Each check's functions are in the header named for its source file (Dispatch.hpp,
// Attachments.hpp, Feedback.hpp, Limits.hpp, Diagnostics.hpp). The stage itself is in Layout.hpp, and its output in
// LaidOutGraph.hpp.
namespace loom
{
    class DiagnosticSink;
    struct BoundGraph;
    struct FrozenGraph;
    struct LayoutLimits;
    struct ResolvedGraph;
    struct SizedGraph;

    namespace layout
    {
        struct LayoutContext
        {
            const FrozenGraph& Frozen;
            const ResolvedGraph& Resolved;
            const BoundGraph& Bound;
            const SizedGraph& Sized;
            const LayoutLimits& Limits;
            DiagnosticSink& Sink;
        };
    }
}

#endif // !DIAMOND_DOGS_LOOM_LAYOUT_CONTEXT_HPP
