#include "EdidRange.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace
{

    constexpr size_t k_BaseBlockSize = 128u;
    constexpr std::array<uint8_t, 8> k_Header{ 0x00u, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0x00u };
    // the four 18-byte descriptors of the base block
    constexpr std::array<size_t, 4> k_DescriptorOffsets{ 54u, 72u, 90u, 108u };
    constexpr uint8_t k_RangeLimitsTag = 0xfdu;
    // byte 4 of the range descriptor (EDID 1.4): bits 1..0 are 10 for "max vertical + 255", 11 for "both + 255"
    constexpr uint8_t k_VerticalOffsetMask = 0x03u;
    constexpr uint8_t k_MaxVerticalOffset = 0x02u;
    constexpr uint8_t k_BothVerticalOffsets = 0x03u;
    constexpr float k_RateOffset = 255.0f;

    [[nodiscard]] bool ChecksumValid(std::span<const uint8_t> block) noexcept
    {
        uint32_t sum = 0u;

        for (const uint8_t value : block)
        {
            sum += value;
        }

        return (sum & 0xffu) == 0u;
    }

}

std::optional<EdidRefreshRange> ParseEdidRefreshRange(std::span<const uint8_t> edid) noexcept
{
    if (edid.size() < k_BaseBlockSize)
    {
        return std::nullopt;
    }

    const std::span<const uint8_t> block = edid.first(k_BaseBlockSize);
    const bool headerValid = std::equal(k_Header.begin(), k_Header.end(), block.begin());

    if (!headerValid || !ChecksumValid(block))
    {
        return std::nullopt;
    }

    for (const size_t offset : k_DescriptorOffsets)
    {
        const std::span<const uint8_t> descriptor = block.subspan(offset, 18u);
        const bool isRangeLimits = descriptor[0] == 0u && descriptor[1] == 0u && descriptor[2] == 0u &&
                                   descriptor[3] == k_RangeLimitsTag;

        if (!isRangeLimits)
        {
            continue;
        }

        const uint8_t offsets = descriptor[4] & k_VerticalOffsetMask;
        const float minOffset = offsets == k_BothVerticalOffsets ? k_RateOffset : 0.0f;
        const float maxOffset = offsets == k_MaxVerticalOffset || offsets == k_BothVerticalOffsets ? k_RateOffset : 0.0f;
        const EdidRefreshRange range{ .MinHz = descriptor[5] + minOffset, .MaxHz = descriptor[6] + maxOffset };

        if (range.MaxHz <= range.MinHz)
        {
            return std::nullopt;
        }

        return range;
    }

    return std::nullopt;
}
