#pragma once
#ifndef DIAMOND_DOGS_LOOM_LAYOUT_LAYOUT_HPP
#define DIAMOND_DOGS_LOOM_LAYOUT_LAYOUT_HPP
#include "layout/LaidOutGraph.hpp"
#include <optional>

// Layout.cpp: the Layout stage, the last Compile stage. It runs the checks in Dispatch.cpp, Attachments.cpp,
// Feedback.cpp and Limits.cpp, and gathers one pipeline description per distinct pipeline.
namespace loom
{
    class DiagnosticSink;
    struct BoundGraph;
    struct FrozenGraph;
    struct LayoutLimits;
    struct ResolvedGraph;
    struct SizedGraph;

    // Reports every Layout error it finds, not only the first one. Returns no graph when it reported an error. Keep
    // the bundle open while the sink holds diagnostics: a semantic or a binding name is a view into it.
    [[nodiscard]] std::optional<LaidOutGraph> Layout(const FrozenGraph& frozen,
                                                     const ResolvedGraph& resolved,
                                                     const BoundGraph& bound,
                                                     const SizedGraph& sized,
                                                     const LayoutLimits& limits,
                                                     DiagnosticSink& sink);

}

#endif // !DIAMOND_DOGS_LOOM_LAYOUT_LAYOUT_HPP
