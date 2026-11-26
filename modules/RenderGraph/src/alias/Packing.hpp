#pragma once
#ifndef DIAMOND_DOGS_LOOM_ALIAS_PACKING_HPP
#define DIAMOND_DOGS_LOOM_ALIAS_PACKING_HPP
#include "AliasLayout.hpp"
#include <cstdint>
#include <span>
#include <string>

// Packing.cpp: the heaps and the offsets for the alias candidates, from what the device says each one needs.
// Device-free, so loom_tests checks it with needs written by hand.
namespace loom
{
    struct CompiledData;

    // `needs` has one entry per candidate, in the same order. Candidates group by class and memory type mask; each
    // group packs largest first, at the lowest aligned offset that overlaps no placed candidate whose level range
    // overlaps its own. A group whose heap would save nothing gets no heap: its rows keep their own memory.
    // `row_count` sizes Placements and OverlapFirst (CompiledData::RowCount).
    [[nodiscard]] AliasLayout PackAliases(std::span<const AliasCandidate> candidates,
                                          std::span<const MemoryNeed> needs,
                                          int32_t row_count);

    // One line per heap and one per placed row: the key, the offset, the size and the levels.
    [[nodiscard]] std::string ExplainAliases(const CompiledData& data, const AliasLayout& layout);
}

#endif // !DIAMOND_DOGS_LOOM_ALIAS_PACKING_HPP
