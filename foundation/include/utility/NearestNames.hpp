#pragma once
#ifndef FOUNDATION_UTILITY_NEAREST_NAMES_HPP
#define FOUNDATION_UTILITY_NEAREST_NAMES_HPP
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace foundation
{
    /** @brief Returns the Levenshtein distance between the two words, ignoring ASCII case. */
    [[nodiscard]] int32_t EditDistance(std::string_view first, std::string_view second);
    /** @brief Returns at most `max_count` names from `names` that are near `word`, nearest first. A name is near when
     *  its edit distance is at most max(2, word length / 4). This is used to suggest the name a user meant. */
    [[nodiscard]] std::vector<std::string> NearestNames(std::string_view word,
                                                        std::span<const std::string_view> names,
                                                        int32_t max_count);
}

#endif // !FOUNDATION_UTILITY_NEAREST_NAMES_HPP
