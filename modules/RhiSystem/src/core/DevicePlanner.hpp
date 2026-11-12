#pragma once
#ifndef DIAMOND_DOGS_RHI_DEVICE_PLANNER_HPP
#define DIAMOND_DOGS_RHI_DEVICE_PLANNER_HPP
#include "FeatureChain.hpp"
#include "ShaderManifestDevice.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace rhi
{
    struct DeviceStartInfo;

    /** What StartDevice would enable on one physical device. */
    struct DevicePlan
    {
        VkPhysicalDevice PhysicalDevice{ VK_NULL_HANDLE };
        std::string DeviceName;
        // the lower of the instance version and the device version
        uint32_t ApiVersion{ 0u };
        // discrete before integrated, then more device-local memory
        int32_t Score{ 0 };
        // every name is supported: the requested extensions, and the extensions that a chosen enable needs
        std::vector<std::string> Extensions;
        // zeroed structs, with only the chosen members set
        FeatureChain Enabled;
        // the primary names of the capabilities that the device can run code for, sorted
        std::vector<std::string> EnabledCapabilities;
        // the "Struct.member" names that were requested and enabled, sorted
        std::vector<std::string> EnabledFeatures;
        lodestone::manifest::DeviceLimits Limits;
        // the first Required need that the device does not meet, and why. Both empty when it meets every one
        std::string UnmetNeed;
        std::string UnmetReason;
    };

    /** @brief Plans the enables for one physical device. For a capability, picks the first enable that the device
     *  supports, in this order: a Vulkan version, a feature of a core struct, a feature of an extension struct, an
     *  extension, a property. A feature of a struct that a core version absorbed is set in that version's
     *  VkPhysicalDeviceVulkanNNFeatures, because device creation must not chain both. Reads the device, and creates
     *  nothing. */
    [[nodiscard]] DevicePlan PlanDevice(VkPhysicalDevice physical_device,
                                        uint32_t instance_api_version,
                                        const DeviceStartInfo& start_info);
}

#endif // !DIAMOND_DOGS_RHI_DEVICE_PLANNER_HPP
