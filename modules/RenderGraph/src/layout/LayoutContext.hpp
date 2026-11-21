#pragma once
#ifndef DIAMOND_DOGS_LOOM_LAYOUT_CONTEXT_HPP
#define DIAMOND_DOGS_LOOM_LAYOUT_CONTEXT_HPP
#include "layout/LaidOutGraph.hpp"
#include "bind/BoundGraph.hpp"
#include "freeze/FrozenGraph.hpp"
#include "resolve/ResolvedGraph.hpp"
#include "size/SizedGraph.hpp"
#include "LoomDiagnostic.hpp"
#include <cstdint>
#include <source_location>
#include <string_view>
#include <vector>

// The Layout stage's internals, shared by its source files. The public part (LaidOutGraph, Layout) is in
// LaidOutGraph.hpp. Each check lives in the file named for the question it answers.
namespace loom::layout
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

    // Dispatch.cpp: the group count of a dispatch, and LM601 when it exceeds maxComputeWorkGroupCount
    [[nodiscard]] LaidOutDispatch LayOutDispatch(LayoutContext& context, const FrozenPass& pass,
                                                 const ResolvedPass& resolved);

    // Attachments.cpp: the attachments of a draw against its fragment shader (LM602), and the color attachments of
    // its pipeline description
    [[nodiscard]] std::vector<LaidOutColor> CheckAttachments(LayoutContext& context, const FrozenPass& pass,
                                                             const ResolvedPass& resolved);

    // Attachments.cpp: LM603 for each vertex input. Loom has no vertex format yet, so a draw reads its vertices from
    // a buffer (vertex pulling).
    void CheckVertexInputs(LayoutContext& context, const FrozenPass& pass, const ResolvedPass& resolved);

    // Feedback.cpp: LM604 for each image that a pass binds for reading and also writes as a target
    void CheckFeedback(LayoutContext& context, int32_t pass_index);

    // Limits.cpp: LM601 for one pass's descriptor sets, per-stage descriptors, color attachments and buffer ranges
    void CheckPassLimits(LayoutContext& context, int32_t pass_index);

    // Limits.cpp: LM601 for each image whose extent exceeds maxImageDimension
    void CheckImageLimits(LayoutContext& context);

    // Diagnostics.cpp: the values and the report that every Layout check shares
    [[nodiscard]] DiagnosticValue TextValue(std::string_view field_name, std::string_view text);
    [[nodiscard]] DiagnosticValue IntegerValue(std::string_view field_name, int64_t value);

    // LM601: Expected is the limit, Actual the value that exceeds it. Both carry the limit's name.
    void ReportLimit(LayoutContext& context, KeyRef pass, KeyRef resource, std::source_location where,
                     std::string_view limit_name, int64_t limit, int64_t value);

}

#endif // !DIAMOND_DOGS_LOOM_LAYOUT_CONTEXT_HPP
