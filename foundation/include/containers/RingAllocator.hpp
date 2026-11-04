#pragma once
#ifndef FOUNDATION_CONTAINERS_RING_ALLOCATOR_HPP
#define FOUNDATION_CONTAINERS_RING_ALLOCATOR_HPP
#include <cassert>
#include <concepts>
#include <cstdint>
#include <optional>
#include <vector>

/** @brief A byte range of a ring: [Offset, Offset + Size). */
struct RingRange
{
    uint64_t Offset;
    uint64_t Size;
};

/**
 * @brief Hands out byte ranges of a fixed capacity in FIFO order, and frees them by tag. It touches no memory of its
 * own: the owner maps the offsets onto a buffer. One thread uses an instance, so it has no atomics and no locks.
 *
 * - A range never wraps. When it does not fit at the end, the end is skipped and the range starts at offset 0.
 * - The alignment can be any positive value, because an image copy offset must be a multiple of a texel size such as 12.
 * - Tags must rise, in the calls to Allocate and in the calls to ReleaseThrough. A release frees the oldest ranges.
 * - A ring with no live range restarts at offset 0, so a range of at most Capacity() bytes always fits in it.
 * - The records are one array that the constructor allocates. Allocate returns nullopt when the array is full.
 */
template<std::totally_ordered Tag>
class RingAllocator
{
public:

    RingAllocator(uint64_t _capacity, int32_t max_live_ranges) :
      capacityBytes{ _capacity },
      records(static_cast<size_t>(max_live_ranges))
    {
        assert(_capacity > 0u && max_live_ranges > 0);
    }

    /** @brief Returns the next range, or nullopt when the bytes are not free or the record array is full. The size
     *  and the alignment must be above 0. */
    [[nodiscard]] std::optional<RingRange> Allocate(uint64_t size, uint64_t alignment, Tag tag)
    {
        assert(size > 0u && alignment > 0u);
        assert(!newestTag.has_value() || *newestTag <= tag);

        if (recordCount == static_cast<int32_t>(records.size()))
        {
            return std::nullopt;
        }

        const std::optional<uint64_t> offset = findOffset(size, alignment);
        if (!offset.has_value())
        {
            return std::nullopt;
        }

        pushRecord(RingRange{ *offset, size }, tag);
        newestTag = tag;
        return RingRange{ *offset, size };
    }

    /** @brief Frees every oldest range whose tag is at or below `tag`. */
    void ReleaseThrough(Tag tag)
    {
        assert(!releasedTag.has_value() || *releasedTag <= tag);
        releasedTag = tag;

        while (recordCount > 0 && records[static_cast<size_t>(oldestIndex)].ReleaseTag <= tag)
        {
            oldestIndex = (oldestIndex + 1) % static_cast<int32_t>(records.size());
            recordCount -= 1;
        }

        refreshOffsets();
    }

    /** @brief Returns the bytes between the oldest live range and the end of the newest, padding and skipped end included. */
    [[nodiscard]] uint64_t UsedBytes() const noexcept
    {
        if (recordCount == 0)
        {
            return 0u;
        }

        return tailOffset > headOffset ? tailOffset - headOffset : tailOffset + capacityBytes - headOffset;
    }

    [[nodiscard]] uint64_t Capacity() const noexcept
    {
        return capacityBytes;
    }

    [[nodiscard]] int32_t LiveRanges() const noexcept
    {
        return recordCount;
    }

private:

    struct Record
    {
        RingRange Range{ 0u, 0u };
        Tag ReleaseTag{};
    };

    [[nodiscard]] static uint64_t alignUp(uint64_t value, uint64_t alignment) noexcept
    {
        const uint64_t remainder = value % alignment;
        return remainder == 0u ? value : value + (alignment - remainder);
    }

    // the oldest live range starts at headOffset, and the newest ends at tailOffset. a tail at or below the head
    // means the newest range wrapped, so the free bytes are the ones from the tail up to the head
    [[nodiscard]] std::optional<uint64_t> findOffset(uint64_t size, uint64_t alignment) const noexcept
    {
        const bool wrapped = recordCount > 0 && tailOffset <= headOffset;
        const uint64_t endOffset = alignUp(tailOffset, alignment);
        const uint64_t endLimit = wrapped ? headOffset : capacityBytes;

        if (endOffset <= endLimit && size <= endLimit - endOffset)
        {
            return endOffset;
        }

        if (wrapped || size > headOffset)
        {
            return std::nullopt;
        }

        return 0u;
    }

    void pushRecord(RingRange range, Tag tag)
    {
        const int32_t newestIndex = (oldestIndex + recordCount) % static_cast<int32_t>(records.size());
        records[static_cast<size_t>(newestIndex)] = Record{ range, tag };

        if (recordCount == 0)
        {
            headOffset = range.Offset;
        }

        recordCount += 1;
        tailOffset = range.Offset + range.Size;
    }

    void refreshOffsets()
    {
        if (recordCount == 0)
        {
            headOffset = 0u;
            tailOffset = 0u;
            return;
        }

        headOffset = records[static_cast<size_t>(oldestIndex)].Range.Offset;
    }

    uint64_t capacityBytes;
    std::vector<Record> records;
    int32_t oldestIndex{ 0 };
    int32_t recordCount{ 0 };
    uint64_t headOffset{ 0u };
    uint64_t tailOffset{ 0u };
    std::optional<Tag> newestTag;
    std::optional<Tag> releasedTag;
};

#endif //!FOUNDATION_CONTAINERS_RING_ALLOCATOR_HPP
