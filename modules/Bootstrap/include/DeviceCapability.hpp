#pragma once
#ifndef DIAMOND_DOGS_BOOTSTRAP_DEVICE_CAPABILITY_HPP
#define DIAMOND_DOGS_BOOTSTRAP_DEVICE_CAPABILITY_HPP
#include <algorithm>
#include <cstdint>
#include <span>
#include <string_view>

// This header includes no graphics API and no other Bootstrap header, so a target that links no RHI (Loom's core)
// can include it
namespace bootstrap
{
    /** A device capability in no API's vocabulary. Each device stage maps a value to the extensions and features of
     *  its own API, and reports which values the device was created with (DeviceFilter::Capabilities). */
    enum class DeviceCapability : uint16_t
    {
        Invalid = 0,
        // indirect draws that read their draw count from a buffer
        DrawIndirectCount,
    };

    // the last value, so that a loop or a test can visit every capability
    inline constexpr DeviceCapability k_LastDeviceCapability{ DeviceCapability::DrawIndirectCount };

    /** @brief The name of the capability, as a need, a boot failure and DeviceConfig::Declined spell it. Not the name
     *  of any API's extension, feature or shader capability. */
    [[nodiscard]] constexpr std::string_view ToString(DeviceCapability capability) noexcept
    {
        switch (capability)
        {
        case DeviceCapability::DrawIndirectCount:
            return "DrawIndirectCount";
        default:
            return "Invalid";
        }
    }

    /** @brief Returns the capability with this name, or Invalid when no capability has it. */
    [[nodiscard]] constexpr DeviceCapability DeviceCapabilityFromName(std::string_view name) noexcept
    {
        if (name == ToString(DeviceCapability::DrawIndirectCount))
        {
            return DeviceCapability::DrawIndirectCount;
        }

        return DeviceCapability::Invalid;
    }

    /** @brief Returns true when `enabled` holds `capability`. `enabled` is sorted, as DeviceFilter::Capabilities is. */
    [[nodiscard]] inline bool HasCapability(std::span<const DeviceCapability> enabled, DeviceCapability capability) noexcept
    {
        return std::ranges::binary_search(enabled, capability);
    }
}

#endif // !DIAMOND_DOGS_BOOTSTRAP_DEVICE_CAPABILITY_HPP
