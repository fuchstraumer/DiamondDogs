#pragma once
#ifndef DIAMOND_DOGS_LOOM_LAYOUT_ATTACHMENTS_HPP
#define DIAMOND_DOGS_LOOM_LAYOUT_ATTACHMENTS_HPP
#include "layout/LaidOutGraph.hpp"
#include <vector>

// Attachments.cpp: a draw's attachments against its fragment shader (LM602), and its vertex inputs (LM603).
namespace loom
{
    struct FrozenPass;
    struct ResolvedPass;

    namespace layout
    {
        struct LayoutContext;

        // LM602, and the color attachments of the pass's pipeline description
        [[nodiscard]] std::vector<LaidOutColor> CheckAttachments(LayoutContext& context, const FrozenPass& pass,
                                                                 const ResolvedPass& resolved);

        // LM603 for each vertex input. Loom has no vertex format yet, so a draw reads its vertices from a buffer
        // (vertex pulling).
        void CheckVertexInputs(LayoutContext& context, const FrozenPass& pass, const ResolvedPass& resolved);
    }
}

#endif // !DIAMOND_DOGS_LOOM_LAYOUT_ATTACHMENTS_HPP
