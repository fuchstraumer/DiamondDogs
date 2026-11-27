#pragma once
#ifndef DIAMOND_DOGS_LOOM_ALIAS_LAYOUT_HPP
#define DIAMOND_DOGS_LOOM_ALIAS_LAYOUT_HPP
#include <cstdint>
#include <span>
#include <vector>

// Memory aliasing: resource rows that are never live in the same level share memory. Compile picks the candidates
// (alias/Lifetimes.cpp, no device). A backend that can place resources asks the device what each candidate needs, and
// PackAliases (alias/Packing.cpp, no device) gives the heaps and the offsets. PlanBarriers reads the result, so that
// the first use of a row in each Tick waits for every row that shares its bytes.
namespace loom
{

    // Buffers and images never share a heap: a linear buffer next to an optimal image would need
    // bufferImageGranularity padding, and two heaps need none.
    enum class AliasClass : uint8_t
    {
        Invalid = 0,
        Buffer = 1,
        Image = 2
    };

    // A row that may share memory, and the levels it is live in (first use to last use, both included). Two
    // candidates can share bytes only when their level ranges do not overlap: the passes of one level run with no
    // barrier between them, so they can run at the same time.
    struct AliasCandidate
    {
        int32_t Row{ -1 };
        int32_t FirstLevel{ -1 };
        int32_t LastLevel{ -1 };
        AliasClass Class{ AliasClass::Invalid };
    };

    // What the device says one candidate needs. A Size of 0 means the backend could not ask, and the row is not aliased.
    struct MemoryNeed
    {
        uint64_t Size{ 0u };
        uint64_t Alignment{ 0u };
        uint32_t MemoryTypeBits{ 0u };
    };

    struct AliasHeap
    {
        AliasClass Class{ AliasClass::Invalid };
        uint32_t MemoryTypeBits{ 0u };
        uint64_t Size{ 0u };
    };

    // Where one row lives. Heap -1: the row has its own memory.
    struct AliasPlacement
    {
        int32_t Heap{ -1 };
        uint64_t Offset{ 0u };
        uint64_t Size{ 0u };
    };

    // A quiet NaN as a float, and an index far past any buffer as an integer
    inline constexpr uint32_t k_AliasPoisonWord = 0x7FC0DEADu;

    struct AliasLayout
    {
        std::vector<AliasHeap> Heaps;
        std::vector<AliasPlacement> Placements;     // [row], or empty when nothing is aliased

        // The rows that share at least one byte with a row: OverlapRows[OverlapFirst[row], OverlapFirst[row + 1])
        std::vector<int32_t> OverlapFirst;          // [row + 1], or empty when nothing is aliased
        std::vector<int32_t> OverlapRows;

        uint64_t UnaliasedBytes{ 0u };              // the placed rows' sizes added up: what they would take alone
        uint64_t HeapBytes{ 0u };                   // what the heaps take

        // A debug mode (MemoryAliasing::OnPoisoned): the backend fills each placed buffer with k_AliasPoisonWord at its
        // takeover, so a pass that reads bytes it did not write this frame reads the poison every time.
        bool Poison{ false };

        [[nodiscard]] bool IsPlaced(int32_t row) const noexcept
        {
            return row >= 0 && row < static_cast<int32_t>(Placements.size()) && Placements[row].Heap >= 0;
        }

        [[nodiscard]] std::span<const int32_t> OverlapsOf(int32_t row) const noexcept
        {
            if (!IsPlaced(row))
            {
                return {};
            }

            const int32_t first = OverlapFirst[row];
            const int32_t count = OverlapFirst[row + 1] - first;
            return std::span<const int32_t>{ OverlapRows }.subspan(static_cast<size_t>(first), static_cast<size_t>(count));
        }
    };

}

#endif // !DIAMOND_DOGS_LOOM_ALIAS_LAYOUT_HPP
