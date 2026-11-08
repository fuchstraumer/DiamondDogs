#pragma once
#ifndef DIAMOND_DOGS_CC_ARTIFACT_TABLE_HPP
#define DIAMOND_DOGS_CC_ARTIFACT_TABLE_HPP
#include "ExecutorTypes.hpp"
#include <cstdint>
#include <vector>

namespace cc
{
    // Owns every artifact. A handle is a slot and a generation, so a stale handle is caught instead of read. Each
    // artifact has a holder count: rows and roots hold it, and the last Release frees it. One thread, plain counts.
    class ArtifactTable
    {
    public:
        // The new artifact has one holder: the caller.
        [[nodiscard]] ArtifactRef Add(Artifact _artifact);

        void Hold(ArtifactRef _ref);
        // Frees the artifact when its last holder releases it. The slot is reused with a new generation.
        void Release(ArtifactRef _ref);

        [[nodiscard]] bool IsLive(ArtifactRef _ref) const noexcept;
        // The handle must be live.
        [[nodiscard]] const Artifact& Get(ArtifactRef _ref) const;
        [[nodiscard]] int32_t LiveCount() const noexcept;

    private:
        struct Entry
        {
            Artifact Value;
            uint32_t Generation{ 0 };
            int32_t Holders{ 0 };
        };

        std::vector<Entry> entries;
        std::vector<uint32_t> freeSlots;
        int32_t liveCount{ 0 };
    };
}

#endif // !DIAMOND_DOGS_CC_ARTIFACT_TABLE_HPP
