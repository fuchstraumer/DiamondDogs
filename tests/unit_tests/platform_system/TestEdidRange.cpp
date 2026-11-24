#include <gtest/gtest.h>
#include "EdidRange.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace
{

    // The base EDID block of the author's LG ULTRAGEAR+ (GSM9E8B), from the registry, 2026-11-24. Its serial number
    // (bytes 12 to 15, and the text of the 0xFF descriptor at 108) is blanked and the checksum fixed. The range limits
    // descriptor is at 72: flags 0x08 (no offsets), vertical 48 to 240 Hz, horizontal 30 to 145 kHz.
    constexpr std::array<uint8_t, 128> k_LgUltragear
    {
        0x00u, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0x00u, 0x1eu, 0x6du, 0x8bu, 0x9eu, 0x00u, 0x00u, 0x00u, 0x00u,
        0x05u, 0x21u, 0x01u, 0x04u, 0xc5u, 0x69u, 0x2cu, 0x78u, 0xf8u, 0x39u, 0x8cu, 0xadu, 0x52u, 0x41u, 0xafu, 0x25u,
        0x0eu, 0x4au, 0x4du, 0x21u, 0x09u, 0x00u, 0xd1u, 0xc0u, 0x61u, 0x40u, 0x45u, 0x40u, 0x01u, 0x01u, 0x01u, 0x01u,
        0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x4eu, 0xd4u, 0x70u, 0xa0u, 0xd0u, 0xa0u, 0x46u, 0x50u, 0x30u, 0x20u,
        0x3au, 0x00u, 0x18u, 0xb9u, 0x41u, 0x00u, 0x00u, 0x1au, 0x00u, 0x00u, 0x00u, 0xfdu, 0x08u, 0x30u, 0xf0u, 0x1eu,
        0x91u, 0x8cu, 0x00u, 0x0au, 0x20u, 0x20u, 0x20u, 0x20u, 0x20u, 0x20u, 0x00u, 0x00u, 0x00u, 0xfcu, 0x00u, 0x4cu,
        0x47u, 0x20u, 0x55u, 0x4cu, 0x54u, 0x52u, 0x41u, 0x47u, 0x45u, 0x41u, 0x52u, 0x2bu, 0x00u, 0x00u, 0x00u, 0xffu,
        0x00u, 0x0au, 0x20u, 0x20u, 0x20u, 0x20u, 0x20u, 0x20u, 0x20u, 0x20u, 0x20u, 0x20u, 0x20u, 0x20u, 0x02u, 0xd0u,
    };

    // The same monitor after its firmware change turned adaptive sync on (2026-11-24): base block and CTA-861 extension,
    // serial blanked, the DisplayID extension dropped (its product block carries the serial too), checksums fixed. The
    // feature byte (24) is now 0xf9 (continuous frequency), and the CTA block has an AMD FreeSync block (6D 1A 00 00 ...).
    constexpr std::array<uint8_t, 256> k_LgUltragearAdaptiveSync
    {
        0x00u, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0x00u, 0x1eu, 0x6du, 0x8bu, 0x9eu, 0x00u, 0x00u, 0x00u, 0x00u,
        0x05u, 0x21u, 0x01u, 0x04u, 0xc5u, 0x69u, 0x2cu, 0x78u, 0xf9u, 0x39u, 0x8cu, 0xadu, 0x52u, 0x41u, 0xafu, 0x25u,
        0x0eu, 0x4au, 0x4du, 0x21u, 0x09u, 0x00u, 0xd1u, 0xc0u, 0x61u, 0x40u, 0x45u, 0x40u, 0x01u, 0x01u, 0x01u, 0x01u,
        0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x4eu, 0xd4u, 0x70u, 0xa0u, 0xd0u, 0xa0u, 0x46u, 0x50u, 0x30u, 0x20u,
        0x3au, 0x00u, 0x18u, 0xb9u, 0x41u, 0x00u, 0x00u, 0x1au, 0x00u, 0x00u, 0x00u, 0xfdu, 0x0cu, 0x30u, 0xf0u, 0x91u,
        0x91u, 0x8cu, 0x01u, 0x0au, 0x20u, 0x20u, 0x20u, 0x20u, 0x20u, 0x20u, 0x00u, 0x00u, 0x00u, 0xfcu, 0x00u, 0x4cu,
        0x47u, 0x20u, 0x55u, 0x4cu, 0x54u, 0x52u, 0x41u, 0x47u, 0x45u, 0x41u, 0x52u, 0x2bu, 0x00u, 0x00u, 0x00u, 0xffu,
        0x00u, 0x0au, 0x20u, 0x20u, 0x20u, 0x20u, 0x20u, 0x20u, 0x20u, 0x20u, 0x20u, 0x20u, 0x20u, 0x20u, 0x01u, 0x58u,
        0x02u, 0x03u, 0x28u, 0x72u, 0x23u, 0x0fu, 0x57u, 0x07u, 0x83u, 0x01u, 0x00u, 0x00u, 0xe3u, 0x05u, 0xc0u, 0x00u,
        0xe6u, 0x06u, 0x05u, 0x01u, 0x73u, 0x33u, 0x0au, 0xe2u, 0x00u, 0x6au, 0x6du, 0x1au, 0x00u, 0x00u, 0x02u, 0x01u,
        0x30u, 0xf0u, 0x00u, 0x04u, 0x73u, 0x0au, 0x33u, 0x15u, 0xe7u, 0x7cu, 0x70u, 0xa0u, 0xd0u, 0xa0u, 0x29u, 0x50u,
        0x30u, 0x20u, 0x3au, 0x00u, 0x18u, 0xb9u, 0x41u, 0x00u, 0x00u, 0x1au, 0x9du, 0x67u, 0x70u, 0xa0u, 0xd0u, 0xa0u,
        0x22u, 0x50u, 0x30u, 0x20u, 0x3au, 0x00u, 0x6fu, 0x6eu, 0x31u, 0x00u, 0x00u, 0x1au, 0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x8au,
    };

    constexpr size_t k_FeatureByte = 24u;
    constexpr size_t k_RevisionByte = 19u;
    constexpr size_t k_CtaChecksum = 255u;

    // Changes one byte of the adaptive-sync EDID and fixes the checksum of the block it is in
    [[nodiscard]] std::array<uint8_t, 256> AdaptiveWithByte(size_t index, uint8_t value)
    {
        std::array<uint8_t, 256> edid = k_LgUltragearAdaptiveSync;
        edid[index] = value;
        const size_t blockStart = index < 128u ? 0u : 128u;
        uint32_t sum = 0u;

        for (size_t i = blockStart; i < blockStart + 127u; ++i)
        {
            sum += edid[i];
        }

        edid[blockStart + 127u] = static_cast<uint8_t>((256u - (sum & 0xffu)) & 0xffu);
        return edid;
    }

    constexpr size_t k_RangeDescriptor = 72u;
    constexpr size_t k_RangeFlags = k_RangeDescriptor + 4u;
    constexpr size_t k_RangeMinVertical = k_RangeDescriptor + 5u;
    constexpr size_t k_RangeMaxVertical = k_RangeDescriptor + 6u;
    constexpr size_t k_Checksum = 127u;

    // Changes one byte and fixes the checksum, so that only the change is under test
    [[nodiscard]] std::array<uint8_t, 128> WithByte(size_t index, uint8_t value)
    {
        std::array<uint8_t, 128> edid = k_LgUltragear;
        edid[index] = value;
        uint32_t sum = 0u;

        for (size_t i = 0u; i < k_Checksum; ++i)
        {
            sum += edid[i];
        }

        edid[k_Checksum] = static_cast<uint8_t>((256u - (sum & 0xffu)) & 0xffu);
        return edid;
    }

}

TEST(EdidRangeTest, ReadsTheLgUltragearRange)
{
    const std::optional<EdidRefreshRange> range = ParseEdidRefreshRange(k_LgUltragear);

    ASSERT_TRUE(range.has_value());
    EXPECT_FLOAT_EQ(range->MinHz, 48.0f);
    EXPECT_FLOAT_EQ(range->MaxHz, 240.0f);
}

TEST(EdidRangeTest, AddsTheMaxOffset)
{
    // EDID 1.4 flags bits 1..0 = 10: the max vertical rate is 255 Hz more than its byte
    const std::optional<EdidRefreshRange> range = ParseEdidRefreshRange(WithByte(k_RangeFlags, 0x0au));

    ASSERT_TRUE(range.has_value());
    EXPECT_FLOAT_EQ(range->MinHz, 48.0f);
    EXPECT_FLOAT_EQ(range->MaxHz, 495.0f);
}

TEST(EdidRangeTest, AddsBothOffsets)
{
    // bits 1..0 = 11: both rates are 255 Hz more (48 -> 303, 240 -> 495)
    const std::optional<EdidRefreshRange> range = ParseEdidRefreshRange(WithByte(k_RangeFlags, 0x0bu));

    ASSERT_TRUE(range.has_value());
    EXPECT_FLOAT_EQ(range->MinHz, 303.0f);
    EXPECT_FLOAT_EQ(range->MaxHz, 495.0f);
}

TEST(EdidRangeTest, RefusesABadBlock)
{
    std::array<uint8_t, 128> badChecksum = k_LgUltragear;
    badChecksum[k_Checksum] ^= 0x01u;
    const std::array<uint8_t, 128> badHeader = WithByte(1u, 0x00u);

    EXPECT_FALSE(ParseEdidRefreshRange(badChecksum).has_value());
    EXPECT_FALSE(ParseEdidRefreshRange(badHeader).has_value());
    EXPECT_FALSE(ParseEdidRefreshRange(std::span{ k_LgUltragear }.first(127u)).has_value());
    EXPECT_FALSE(ParseEdidRefreshRange(std::span<const uint8_t>{}).has_value());
}

TEST(EdidRangeTest, NeedsARangeDescriptor)
{
    // the range descriptor's tag changed to a dummy descriptor (0x10)
    const std::array<uint8_t, 128> noRange = WithByte(k_RangeDescriptor + 3u, 0x10u);

    EXPECT_FALSE(ParseEdidRefreshRange(noRange).has_value());
}

TEST(EdidRangeTest, AdaptiveSyncAfterTheFirmwareChange)
{
    EXPECT_TRUE(EdidDeclaresAdaptiveSync(k_LgUltragearAdaptiveSync));

    // the range is the same either way: a range alone says nothing about adaptive sync
    const std::optional<EdidRefreshRange> range = ParseEdidRefreshRange(k_LgUltragearAdaptiveSync);
    ASSERT_TRUE(range.has_value());
    EXPECT_FLOAT_EQ(range->MinHz, 48.0f);
    EXPECT_FLOAT_EQ(range->MaxHz, 240.0f);
}

TEST(EdidRangeTest, NoAdaptiveSyncBeforeTheFirmwareChange)
{
    EXPECT_FALSE(EdidDeclaresAdaptiveSync(k_LgUltragear));
}

TEST(EdidRangeTest, EitherSignAloneDeclaresAdaptiveSync)
{
    // the FreeSync block without the feature bit
    const std::array<uint8_t, 256> blockOnly = AdaptiveWithByte(k_FeatureByte, 0xf8u);
    // the feature bit without the extension (the registry can hold only the base block)
    const std::span<const uint8_t> bitOnly = std::span{ k_LgUltragearAdaptiveSync }.first(128u);

    EXPECT_TRUE(EdidDeclaresAdaptiveSync(blockOnly));
    EXPECT_TRUE(EdidDeclaresAdaptiveSync(bitOnly));
}

TEST(EdidRangeTest, TheFeatureBitCountsOnlyInEdid14)
{
    // in EDID 1.3 bit 0 of the feature byte means "GTF timings", not continuous frequency
    std::array<uint8_t, 128> edid13{};
    const std::array<uint8_t, 256> source = AdaptiveWithByte(k_RevisionByte, 3u);
    std::copy_n(source.begin(), 128u, edid13.begin());

    EXPECT_FALSE(EdidDeclaresAdaptiveSync(edid13));
}

TEST(EdidRangeTest, ABrokenCtaBlockIsIgnored)
{
    // no feature bit, and the CTA block's checksum broken: its FreeSync block must not count
    std::array<uint8_t, 256> edid = AdaptiveWithByte(k_FeatureByte, 0xf8u);
    edid[k_CtaChecksum] ^= 0x01u;

    EXPECT_FALSE(EdidDeclaresAdaptiveSync(edid));
}

TEST(EdidRangeTest, RefusesAnEmptyRange)
{
    const std::array<uint8_t, 128> fixed = WithByte(k_RangeMinVertical, k_LgUltragear[k_RangeMaxVertical]);

    EXPECT_FALSE(ParseEdidRefreshRange(fixed).has_value());
}
