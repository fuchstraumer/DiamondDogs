#pragma once
#ifndef DIAMOND_DOGS_BOOTSTRAP_DEVICE_REQUIREMENTS_HPP
#define DIAMOND_DOGS_BOOTSTRAP_DEVICE_REQUIREMENTS_HPP
#include "ShaderManifestDevice.hpp"
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace bootstrap
{
    class ContentStage;

    enum class NeedKind : uint8_t
    {
        Invalid = 0,
        InstanceExtension,
        DeviceExtension,
        // a name in Lodestone's vocabulary: a SPIR-V capability or extension name, or a WGSL enable name
        Capability,
    };

    enum class NeedLevel : uint8_t
    {
        Invalid = 0,
        // the device must have it, or boot fails
        Required,
        // the device stage enables it when the device has it
        Wanted,
    };

    /** Where one need came from, so that a boot failure names the module, the bundle and the entry point. */
    struct NeedSource
    {
        // the descriptor name of the module that asked, or empty for the executable's own bundles and the spine
        std::string_view Module;
        std::string_view BundleFile;
        std::string_view ShaderModule;
        std::string_view EntryPoint;
    };

    struct Need
    {
        std::string_view Name;
        NeedKind Kind{ NeedKind::Invalid };
        NeedLevel Level{ NeedLevel::Invalid };
        NeedSource Source;
    };

    /** The union of what the spine, the selected modules and the open bundles need from a device. Plain data with no
     *  graphics API. Every view points into a descriptor or a spine table (literals), or into a bundle that
     *  ContentStage holds open, so this record must not outlive the content stage. */
    struct DeviceRequirements
    {
        std::vector<Need> InstanceExtensions;
        std::vector<Need> DeviceExtensions;
        std::vector<Need> Capabilities;
        // for each entry point the smallest limit over its variants, then the largest over the entry points. A device
        // below one of these cannot run any variant of some entry point
        lodestone::manifest::DeviceLimits RequiredLimits;
        // the largest limit over every entry point instance: a device at these limits can run every variant
        lodestone::manifest::DeviceLimits WantedLimits;
    };

    /** @brief Returns the needs of the spine, of every selected module, and of every entry point in every open
     *  bundle. For one entry point, a capability that every variant needs is Required, and a capability that only
     *  some variants need is Wanted. One name keeps one entry: Required wins over Wanted, and the entry keeps the
     *  source of the first need at its level. Reads no device. Each spine need sets Name, Kind and Level. */
    [[nodiscard]] DeviceRequirements GatherDeviceRequirements(const ContentStage& content, std::span<const Need> spine_needs);

    [[nodiscard]] std::string_view ToString(NeedKind kind) noexcept;
    [[nodiscard]] std::string_view ToString(NeedLevel level) noexcept;
}

#endif // !DIAMOND_DOGS_BOOTSTRAP_DEVICE_REQUIREMENTS_HPP
