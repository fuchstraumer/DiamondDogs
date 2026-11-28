#include "ArtifactTable.hpp"
#include "ExecutorTypes.hpp"
#include <cassert>
#include <cstdint>
#include <utility>

namespace
{
    [[nodiscard]] bool IsGpu(cc::ArtifactKind kind) noexcept
    {
        return kind == cc::ArtifactKind::GpuImage || kind == cc::ArtifactKind::GpuBuffer;
    }
}

namespace cc
{
    ArtifactTable::ArtifactTable(GpuArtifactOwner& _owner) noexcept : gpuOwner{ &_owner }
    {
    }

    ArtifactTable::~ArtifactTable()
    {
        assert(liveCount == 0 && "an artifact still has a holder: a row or a root did not release it");
    }

    ArtifactRef ArtifactTable::Add(Artifact _artifact)
    {
        assert((!IsGpu(_artifact.Kind) || (gpuOwner != nullptr && _artifact.Gpu.IsValid())) &&
               "a GPU artifact needs a valid GpuArtifactId and a table with a GPU owner");

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

        // the table's state is final before the owner runs, so an owner that reads the table sees it consistent
        const Artifact freed = std::move(entry.Value);
        entry.Value = Artifact{};
        // a new generation now, so every handle to the freed artifact is stale at once
        ++entry.Generation;
        freeSlots.push_back(_ref.Slot);
        --liveCount;

        if (IsGpu(freed.Kind))
        {
            gpuOwner->ReleaseGpuArtifact(freed.Gpu);
        }
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
