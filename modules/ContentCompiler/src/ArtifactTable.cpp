#include "ArtifactTable.hpp"
#include "ExecutorTypes.hpp"
#include <cassert>
#include <cstdint>
#include <utility>

namespace cc
{
    ArtifactRef ArtifactTable::Add(Artifact _artifact)
    {
        uint32_t slot = 0;

        if (freeSlots.empty())
        {
            slot = static_cast<uint32_t>(entries.size());
            entries.emplace_back();
        }
        else
        {
            slot = freeSlots.back();
            freeSlots.pop_back();
        }

        Entry& entry = entries[slot];

        // generation 0 is never handed out, so a default ArtifactRef is never live
        ++entry.Generation;

        if (entry.Generation == 0)
        {
            entry.Generation = 1;
        }

        entry.Value = std::move(_artifact);
        entry.Holders = 1;
        ++liveCount;

        return ArtifactRef{ slot, entry.Generation };
    }

    void ArtifactTable::Hold(ArtifactRef _ref)
    {
        assert(IsLive(_ref) && "Hold needs a live artifact");

        ++entries[_ref.Slot].Holders;
    }

    void ArtifactTable::Release(ArtifactRef _ref)
    {
        assert(IsLive(_ref) && "Release needs a live artifact");

        Entry& entry = entries[_ref.Slot];
        --entry.Holders;

        if (entry.Holders > 0)
        {
            return;
        }

        entry.Value = Artifact{};
        // a new generation now, so every handle to the freed artifact is stale at once
        ++entry.Generation;
        freeSlots.push_back(_ref.Slot);
        --liveCount;
    }

    bool ArtifactTable::IsLive(ArtifactRef _ref) const noexcept
    {
        const bool inRange = _ref.Slot < entries.size();

        if (!inRange || !_ref.IsValid())
        {
            return false;
        }

        const Entry& entry = entries[_ref.Slot];

        return entry.Generation == _ref.Generation && entry.Holders > 0;
    }

    const Artifact& ArtifactTable::Get(ArtifactRef _ref) const
    {
        assert(IsLive(_ref) && "Get needs a live artifact");

        return entries[_ref.Slot].Value;
    }

    int32_t ArtifactTable::LiveCount() const noexcept
    {
        return liveCount;
    }
}
