#pragma once
#ifndef DIAMOND_DOGS_LOOM_LAYOUT_LIMITS_HPP
#define DIAMOND_DOGS_LOOM_LAYOUT_LIMITS_HPP
#include <cstdint>

// Limits.cpp: the device limits that Resolve does not check (LM601).
namespace loom::layout
{
    struct LayoutContext;

    // One pass's descriptor sets, per-stage descriptors, color attachments and buffer ranges
    void CheckPassLimits(LayoutContext& context, int32_t pass_index);

    // Each image whose extent exceeds maxImageDimension
    void CheckImageLimits(LayoutContext& context);
}

#endif // !DIAMOND_DOGS_LOOM_LAYOUT_LIMITS_HPP
