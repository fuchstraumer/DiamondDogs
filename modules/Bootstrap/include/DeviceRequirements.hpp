#pragma once
#ifndef DIAMOND_DOGS_BOOTSTRAP_DEVICE_REQUIREMENTS_HPP
#define DIAMOND_DOGS_BOOTSTRAP_DEVICE_REQUIREMENTS_HPP
#include "DeviceCapability.hpp"
#include "ShaderManifestDevice.hpp"
#include <cstdint>
#include <span>
#include <string>
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
        // an API feature as "Struct.member", for example "VkPhysicalDeviceVulkan13Features.synchronization2"
        DeviceFeature,
        // SPIR-V capability or extension name, or a WGSL enable name
        Capability,
        // a DeviceCapability, in no API's vocabulary. Each device stage maps it to its own API's needs
        NeutralCapability,
    };

    enum class NeedLevel : uint8_t
    {
        Invalid = 0,
        Required, // the device must have it, or boot fails
        Wanted, // the device stage enables it when the device has it: optional, but optimal
    };

    /** @brief Describes the origin of a single device requirement, so that a boot failure can accurately
      * report the module, the bundle, and the entry point responsible for the need. */
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
        // for a NeutralCapability, ToString(Neutral), so that a failure and Declined name it as every other need
        std::string_view Name;
        NeedKind Kind{ NeedKind::Invalid };
        NeedLevel Level{ NeedLevel::Invalid };
        NeedSource Source;
        // set only for NeedKind::NeutralCapability
        DeviceCapability Neutral{ DeviceCapability::Invalid };
    };

    /** @brief A NeutralCapability need, with its Name set from the capability. */
    [[nodiscard]] constexpr Need NeutralNeed(DeviceCapability capability, NeedLevel level) noexcept
    {
        return Need
        {
            .Name = ToString(capability),
            .Kind = NeedKind::NeutralCapability,
            .Level = level,
            .Neutral = capability,
        };
    }

    /** The union of what the spine, the selected modules and the open bundles need from a device. Plain data with no
     *  graphics API. Every view points into a descriptor or a spine table (literals), or into a bundle that
     *  ContentStage holds open, so this record must not outlive the content stage. */
    struct DeviceRequirements
    {
        std::vector<Need> InstanceExtensions;
        std::vector<Need> DeviceExtensions;
        std::vector<Need> DeviceFeatures;
        std::vector<Need> Capabilities;
        std::vector<Need> NeutralCapabilities;
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

    /** One entry point of an open bundle, by the names that NeedSource reports: the request's file name, the shader
     *  module and the entry point. */
    struct EntryPointRef
    {
        std::string_view BundleFile;
        std::string_view ShaderModule;
        std::string_view EntryPoint;
    };

    /** @brief As above, except for the entry points in `wanted_only`: every capability that they need is Wanted, and
     *  their limits raise WantedLimits only. For the entry points that only a higher-tier path runs (Loom's path
     *  lists), so that a device without them still boots and the caller chooses a lower path. A Required need that
     *  another entry point or `extra_needs` names stays Required. A reference that matches no entry point does
     *  nothing. */
    [[nodiscard]] DeviceRequirements GatherDeviceRequirements(const ContentStage& content,
                                                              std::span<const Need> extra_needs,
                                                              std::span<const EntryPointRef> wanted_only);

    /** @brief Where a need came from, as a boot failure's Source spells it: "bundle/module/entry point", a module
     *  name, or "spine" for a need that names neither. */
    [[nodiscard]] std::string FormatNeedSource(const NeedSource& source);

    [[nodiscard]] std::string_view ToString(NeedKind kind) noexcept;
    [[nodiscard]] std::string_view ToString(NeedLevel level) noexcept;
}

#endif // !DIAMOND_DOGS_BOOTSTRAP_DEVICE_REQUIREMENTS_HPP
