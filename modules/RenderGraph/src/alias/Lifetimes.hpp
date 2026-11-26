#pragma once
#ifndef DIAMOND_DOGS_LOOM_ALIAS_LIFETIMES_HPP
#define DIAMOND_DOGS_LOOM_ALIAS_LIFETIMES_HPP
#include "AliasLayout.hpp"
#include <vector>

// Lifetimes.cpp: the rows that may share memory, with the levels each one is live in. Device-free; Compile runs it
// after the uses and the spans exist.
namespace loom
{
    struct CompiledData;

    // A candidate is a declared buffer or image with ResourceLifetime::Frame, no history, no CPU access, at least one
    // use, and no writer whose run policy is not EveryFrame (such a write persists across frames). An image sized
    // from a frame target is not one: a resize makes it again, and the heaps do not move.
    [[nodiscard]] std::vector<AliasCandidate> FindAliasCandidates(const CompiledData& data);
}

#endif // !DIAMOND_DOGS_LOOM_ALIAS_LIFETIMES_HPP
