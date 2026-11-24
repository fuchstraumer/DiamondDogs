#pragma once
#ifndef DIAMOND_DOGS_PLATFORM_SYSTEM_EDID_RANGE_HPP
#define DIAMOND_DOGS_PLATFORM_SYSTEM_EDID_RANGE_HPP
#include <cstdint>
#include <optional>
#include <span>

// The vertical refresh range of a display, from the Display Range Limits descriptor (tag 0xFD) of its base EDID block
struct EdidRefreshRange
{
    float MinHz{ 0.0f };
    float MaxHz{ 0.0f };
};

/** @brief Reads the vertical range from the base EDID block, with the EDID 1.4 +255 Hz offsets. Nothing when the block
 * is shorter than 128 bytes, its header or checksum is wrong, it has no range descriptor, or the range is empty.
 * Fixed-refresh displays list a range too (the timings they accept), so a range alone does not prove VRR. */
[[nodiscard]] std::optional<EdidRefreshRange> ParseEdidRefreshRange(std::span<const uint8_t> edid) noexcept;

/** @brief True when the EDID says the display can refresh at a variable rate: the EDID 1.4 "continuous frequency"
 * feature bit, or an AMD FreeSync block in a CTA-861 extension. It says the display can; whether the driver has it on
 * (G-SYNC Compatible, FreeSync) is not in the EDID. HDMI Forum VRR and DisplayID Adaptive-Sync blocks are not read. */
[[nodiscard]] bool EdidDeclaresAdaptiveSync(std::span<const uint8_t> edid) noexcept;

#endif // !DIAMOND_DOGS_PLATFORM_SYSTEM_EDID_RANGE_HPP
