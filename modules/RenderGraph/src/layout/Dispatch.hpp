#pragma once
#ifndef DIAMOND_DOGS_LOOM_LAYOUT_DISPATCH_HPP
#define DIAMOND_DOGS_LOOM_LAYOUT_DISPATCH_HPP
#include "layout/LaidOutGraph.hpp"

// Dispatch.cpp: the group count of a dispatch, and LM601 when it exceeds maxComputeWorkGroupCount.
namespace loom
{
    struct FrozenPass;
    struct ResolvedPass;

    namespace layout
    {
        struct LayoutContext;

        [[nodiscard]] LaidOutDispatch LayOutDispatch(LayoutContext& context, const FrozenPass& pass,
                                                     const ResolvedPass& resolved);
    }
}

#endif // !DIAMOND_DOGS_LOOM_LAYOUT_DISPATCH_HPP
