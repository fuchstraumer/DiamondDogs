#include "utility/NearestNames.hpp"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace foundation
{
    int32_t EditDistance(std::string_view first, std::string_view second)
    {
        std::vector<int32_t> previous(second.size() + 1u);
        std::vector<int32_t> current(second.size() + 1u);

        for (size_t column = 0u; column < previous.size(); ++column)
        {
            previous[column] = static_cast<int32_t>(column);
        }

        for (size_t row = 0u; row < first.size(); ++row)
        {
            current[0] = static_cast<int32_t>(row) + 1;

            for (size_t column = 0u; column < second.size(); ++column)
            {
                const unsigned char firstChar = static_cast<unsigned char>(first[row]);
                const unsigned char secondChar = static_cast<unsigned char>(second[column]);
                const bool same = std::tolower(firstChar) == std::tolower(secondChar);
                const int32_t replaced = previous[column] + (same ? 0 : 1);
                const int32_t removed = previous[column + 1u] + 1;
                const int32_t inserted = current[column] + 1;

                current[column + 1u] = std::min({ replaced, removed, inserted });
            }

            std::swap(previous, current);
        }

        return previous.back();
    }

    std::vector<std::string> NearestNames(std::string_view word,
                                          std::span<const std::string_view> names,
                                          int32_t max_count)
    {
        struct Candidate
        {
            int32_t Distance{ 0 };
            std::string_view Name;
        };

        const int32_t limit = std::max(2, static_cast<int32_t>(word.size()) / 4);
        std::vector<Candidate> candidates;

        for (const std::string_view name : names)
        {
            const int32_t distance = EditDistance(word, name);

            if (distance <= limit)
            {
                candidates.push_back(Candidate{ .Distance = distance, .Name = name });
            }
        }

        std::ranges::stable_sort(candidates, {}, &Candidate::Distance);

        const int64_t keptCount = std::min<int64_t>(std::ssize(candidates), max_count);
        std::vector<std::string> nearest;

        nearest.reserve(static_cast<size_t>(keptCount));

        for (const Candidate& candidate : candidates | std::views::take(keptCount))
        {
            nearest.emplace_back(candidate.Name);
        }

        return nearest;
    }
}
