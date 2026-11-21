#pragma once
#ifndef DIAMOND_DOGS_LOOM_LAYOUT_FEEDBACK_HPP
#define DIAMOND_DOGS_LOOM_LAYOUT_FEEDBACK_HPP
#include <cstdint>

// Feedback.cpp: LM604 for each image that a pass binds for reading and also writes as a target.
namespace loom::layout
{
    struct LayoutContext;

    void CheckFeedback(LayoutContext& context, int32_t pass_index);
}

#endif // !DIAMOND_DOGS_LOOM_LAYOUT_FEEDBACK_HPP
