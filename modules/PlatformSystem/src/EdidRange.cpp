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

    // adaptive sync: the feature byte and its EDID 1.4 bit, and the CTA-861 vendor block of AMD FreeSync
    constexpr size_t k_VersionByte = 18u;
    constexpr size_t k_RevisionByte = 19u;
    constexpr size_t k_FeatureByte = 24u;
    constexpr uint8_t k_ContinuousFrequencyBit = 0x01u;
    constexpr size_t k_ExtensionCountByte = 126u;
    constexpr uint8_t k_CtaExtensionTag = 0x02u;
    constexpr uint8_t k_VendorSpecificBlock = 3u;
    constexpr uint32_t k_FreeSyncOui = 0x00001au;

    [[nodiscard]] bool ChecksumValid(std::span<const uint8_t> block) noexcept
    {
        uint32_t sum = 0u;

        for (const uint8_t value : block)
        {
            sum += value;
        }

        return (sum & 0xffu) == 0u;
    }

    [[nodiscard]] bool BaseBlockValid(std::span<const uint8_t> edid) noexcept
    {
        if (edid.size() < k_BaseBlockSize)
        {
            return false;
        }

        const std::span<const uint8_t> block = edid.first(k_BaseBlockSize);
        return std::equal(k_Header.begin(), k_Header.end(), block.begin()) && ChecksumValid(block);
    }

    // The data block collection of a CTA-861 extension: from byte 4 to the first detailed timing (byte 2)
    [[nodiscard]] bool CtaHasFreeSyncBlock(std::span<const uint8_t> block) noexcept
    {
        const size_t timingsStart = std::min<size_t>(block[2], block.size());
        size_t offset = 4u;

        while (offset < timingsStart)
        {
            const uint8_t header = block[offset];
            const uint8_t kind = header >> 5u;
            const size_t length = header & 0x1fu;
            const bool fits = offset + 1u + length <= timingsStart;

            if (!fits)
            {
                return false;
            }

            // a vendor block starts with its IEEE OUI, least significant byte first
            if (kind == k_VendorSpecificBlock && length >= 3u)
            {
                const uint32_t oui = block[offset + 1u] | (block[offset + 2u] << 8u) | (block[offset + 3u] << 16u);

                if (oui == k_FreeSyncOui)
                {
                    return true;
                }
            }

            offset += 1u + length;
        }

        return false;
    }

}

bool EdidDeclaresAdaptiveSync(std::span<const uint8_t> edid) noexcept
{
    if (!BaseBlockValid(edid))
    {
        return false;
    }

    // in EDID 1.3 the same bit means "GTF timings supported"
    const bool version14 = edid[k_VersionByte] == 1u && edid[k_RevisionByte] >= 4u;

    if (version14 && (edid[k_FeatureByte] & k_ContinuousFrequencyBit) != 0u)
    {
        return true;
    }

    // the registry can hold only the base block; the extensions it has follow it
    const size_t extensions = std::min<size_t>(edid[k_ExtensionCountByte], edid.size() / k_BaseBlockSize - 1u);

    for (size_t index = 1u; index <= extensions; ++index)
    {
        const std::span<const uint8_t> block = edid.subspan(index * k_BaseBlockSize, k_BaseBlockSize);
        const bool isCta = block[0] == k_CtaExtensionTag && ChecksumValid(block);

        if (isCta && CtaHasFreeSyncBlock(block))
        {
            return true;
        }
    }

    return false;
}

std::optional<EdidRefreshRange> ParseEdidRefreshRange(std::span<const uint8_t> edid) noexcept
{
    if (!BaseBlockValid(edid))
    {
        return std::nullopt;
    }

    const std::span<const uint8_t> block = edid.first(k_BaseBlockSize);

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
