#include "alias/Packing.hpp"
#include "compile/CompiledData.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <span>
#include <string>
#include <vector>

namespace loom
{

namespace
{

    // A candidate in the packing order, with its need and, once placed, its offset
    struct PackItem
    {
        int32_t Candidate{ -1 };
        uint64_t Offset{ 0u };
        bool Placed{ false };
    };

    uint64_t AlignUp(uint64_t value, uint64_t alignment)
    {
        if (alignment <= 1u)
        {
            return value;
        }

        return ((value + alignment - 1u) / alignment) * alignment;
    }

    bool LevelsOverlap(const AliasCandidate& lhs, const AliasCandidate& rhs)
    {
        return lhs.FirstLevel <= rhs.LastLevel && rhs.FirstLevel <= lhs.LastLevel;
    }

    bool BytesOverlap(const AliasPlacement& lhs, const AliasPlacement& rhs)
    {
        return lhs.Offset < rhs.Offset + rhs.Size && rhs.Offset < lhs.Offset + lhs.Size;
    }

    // The lowest offset at or after 0, aligned, where `size` bytes miss every busy range. `busy` is sorted by offset.
    uint64_t LowestFit(std::span<const AliasPlacement> busy, uint64_t size, uint64_t alignment)
    {
        uint64_t cursor = 0u;

        for (const AliasPlacement& range : busy)
        {
            const uint64_t candidate = AlignUp(cursor, alignment);

            if (candidate + size <= range.Offset)
            {
                return candidate;
            }

            cursor = std::max(cursor, range.Offset + range.Size);
        }

        return AlignUp(cursor, alignment);
    }

    // Places one group's members (indices into the candidates), largest first. Returns the heap size.
    uint64_t PackGroup(std::span<const AliasCandidate> candidates,
                       std::span<const MemoryNeed> needs,
                       std::vector<int32_t>& members,
                       std::vector<uint64_t>& offsets)
    {
        const auto largerFirst = [&needs, &candidates](int32_t lhs, int32_t rhs)
        {
            if (needs[lhs].Size != needs[rhs].Size)
            {
                return needs[lhs].Size > needs[rhs].Size;
            }

            return candidates[lhs].Row < candidates[rhs].Row;
        };

        std::ranges::sort(members, largerFirst);

        std::vector<AliasPlacement> busy;
        uint64_t heapSize = 0u;

        for (size_t memberIndex = 0u; memberIndex < members.size(); ++memberIndex)
        {
            const int32_t member = members[memberIndex];
            busy.clear();

            // the members placed before this one that are live in one of its levels
            for (size_t placedIndex = 0u; placedIndex < memberIndex; ++placedIndex)
            {
                const int32_t placed = members[placedIndex];

                if (LevelsOverlap(candidates[member], candidates[placed]))
                {
                    busy.push_back(AliasPlacement{ .Heap = 0, .Offset = offsets[placed], .Size = needs[placed].Size });
                }
            }

            std::ranges::sort(busy, {}, &AliasPlacement::Offset);
            offsets[member] = LowestFit(busy, needs[member].Size, needs[member].Alignment);
            heapSize = std::max(heapSize, offsets[member] + needs[member].Size);
        }

        return heapSize;
    }

    void BuildOverlaps(AliasLayout& layout, int32_t row_count)
    {
        std::vector<std::vector<int32_t>> overlaps(static_cast<size_t>(row_count));

        for (int32_t row = 0; row < row_count; ++row)
        {
            if (!layout.IsPlaced(row))
            {
                continue;
            }

            for (int32_t other = row + 1; other < row_count; ++other)
            {
                const bool sameHeap = layout.IsPlaced(other) && layout.Placements[other].Heap == layout.Placements[row].Heap;

                if (sameHeap && BytesOverlap(layout.Placements[row], layout.Placements[other]))
                {
                    overlaps[row].push_back(other);
                    overlaps[other].push_back(row);
                }
            }
        }

        layout.OverlapFirst.assign(static_cast<size_t>(row_count) + 1u, 0);

        for (int32_t row = 0; row < row_count; ++row)
        {
            layout.OverlapFirst[row] = static_cast<int32_t>(layout.OverlapRows.size());
            std::ranges::sort(overlaps[row]);
            layout.OverlapRows.insert(layout.OverlapRows.end(), overlaps[row].begin(), overlaps[row].end());
        }

        layout.OverlapFirst[row_count] = static_cast<int32_t>(layout.OverlapRows.size());
    }

    std::string_view ClassName(AliasClass alias_class)
    {
        switch (alias_class)
        {
        case AliasClass::Buffer:
            return "buffers";
        case AliasClass::Image:
            return "images";
        case AliasClass::Invalid:
            break;
        }

        return "invalid";
    }

}

    AliasLayout PackAliases(std::span<const AliasCandidate> candidates, std::span<const MemoryNeed> needs, int32_t row_count)
    {
        AliasLayout layout;
        layout.Placements.assign(static_cast<size_t>(row_count), AliasPlacement{});

        // groups: one per (class, memory type mask), in the order their first member appears
        struct Group
        {
            AliasClass Class{ AliasClass::Invalid };
            uint32_t MemoryTypeBits{ 0u };
            std::vector<int32_t> Members;
        };

        std::vector<Group> groups;

        for (int32_t candidate = 0; candidate < static_cast<int32_t>(candidates.size()); ++candidate)
        {
            const MemoryNeed& need = needs[candidate];

            if (need.Size == 0u || need.MemoryTypeBits == 0u)
            {
                continue;
            }

            const auto sameGroup = [&candidates, &need, candidate](const Group& group)
            {
                return group.Class == candidates[candidate].Class && group.MemoryTypeBits == need.MemoryTypeBits;
            };

            auto found = std::ranges::find_if(groups, sameGroup);

            if (found == groups.end())
            {
                groups.push_back(Group{ .Class = candidates[candidate].Class, .MemoryTypeBits = need.MemoryTypeBits });
                found = std::prev(groups.end());
            }

            found->Members.push_back(candidate);
        }

        std::vector<uint64_t> offsets(candidates.size(), 0u);

        for (Group& group : groups)
        {
            // one member alone shares nothing
            if (group.Members.size() < 2u)
            {
                continue;
            }

            const uint64_t heapSize = PackGroup(candidates, needs, group.Members, offsets);
            uint64_t unaliased = 0u;

            for (const int32_t member : group.Members)
            {
                unaliased += needs[member].Size;
            }

            // a heap that saves nothing only adds a placement step
            if (heapSize >= unaliased)
            {
                continue;
            }

            const int32_t heap = static_cast<int32_t>(layout.Heaps.size());
            layout.Heaps.push_back(AliasHeap{ .Class = group.Class, .MemoryTypeBits = group.MemoryTypeBits, .Size = heapSize });
            layout.UnaliasedBytes += unaliased;
            layout.HeapBytes += heapSize;

            for (const int32_t member : group.Members)
            {
                layout.Placements[candidates[member].Row] = AliasPlacement
                {
                    .Heap = heap,
                    .Offset = offsets[member],
                    .Size = needs[member].Size
                };
            }
        }

        if (layout.Heaps.empty())
        {
            layout.Placements.clear();
            return layout;
        }

        BuildOverlaps(layout, row_count);
        return layout;
    }

    std::string ExplainAliases(const CompiledData& data, const AliasLayout& layout)
    {
        std::string out;

        if (layout.Heaps.empty())
        {
            out = "No aliasing: no two candidates can share memory.\n";
            return out;
        }

        std::format_to(std::back_inserter(out), "Aliasing: {} heap(s), {} bytes instead of {}\n", layout.Heaps.size(),
                       layout.HeapBytes, layout.UnaliasedBytes);

        for (int32_t heap = 0; heap < static_cast<int32_t>(layout.Heaps.size()); ++heap)
        {
            const AliasHeap& aliasHeap = layout.Heaps[heap];
            std::format_to(std::back_inserter(out), "  heap {} ({}, memory types 0x{:X}): {} bytes\n", heap,
                           ClassName(aliasHeap.Class), aliasHeap.MemoryTypeBits, aliasHeap.Size);

            for (int32_t row = 0; row < static_cast<int32_t>(layout.Placements.size()); ++row)
            {
                const AliasPlacement& placement = layout.Placements[row];

                if (placement.Heap != heap)
                {
                    continue;
                }

                const ResourceSpan& span = data.Spans[row];
                std::format_to(std::back_inserter(out), "    {} at {}, {} bytes, levels {}-{}\n", KeyOfRow(data, row).Name,
                               placement.Offset, placement.Size, span.FirstLevel, span.LastLevel);
            }
        }

        return out;
    }

}
