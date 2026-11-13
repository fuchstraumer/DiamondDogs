#pragma once
#ifndef DIAMOND_DOGS_RHI_SYSTEM_START_HPP
#define DIAMOND_DOGS_RHI_SYSTEM_START_HPP
#include "RhiFlags.hpp"
#include "ShaderManifestDevice.hpp"
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace rhi
{
    enum class DeviceNeedLevel : uint8_t
    {
        Invalid = 0,
        // the device must have it, or StartDevice fails
        Required,
        // StartDevice enables it when the device has it
        Wanted,
    };

    /** One extension, feature or capability that the instance or the device needs. */
    struct DeviceNeed
    {
        std::string_view Name;
        DeviceNeedLevel Level{ DeviceNeedLevel::Invalid };
    };

    /** What RhiSystem::StartInstance creates the instance with. */
    struct InstanceStartInfo
    {
        ApiVersion VkVersion{ ApiVersion::None };
        ValidationLayers ValidationLevel{ ValidationLayers::None };
        std::string_view ApplicationName;
        std::string_view EngineName;
        uint32_t AppVersion{ 0u };
        uint32_t EngineVersion{ 0u };
        std::span<const DeviceNeed> Extensions;
    };

    /** What RhiSystem::StartDevice chooses a physical device for, and enables on it. Nothing else is enabled: every
     *  feature struct starts zeroed. */
    struct DeviceStartInfo
    {
        // device extension names
        std::span<const DeviceNeed> Extensions;
        // API features as "Struct.member", for example "VkPhysicalDeviceVulkan13Features.synchronization2"
        std::span<const DeviceNeed> Features;
        // SPIR-V capability and extension names, in Lodestone's vocabulary. Lodestone's Vulkan capability table maps
        // each one to the versions, extensions, features and properties that enable it
        std::span<const DeviceNeed> Capabilities;
        // Wanted needs of any kind to leave off, so that the fallback paths run on a device that has them. A name
        // here does not stop an enable that another need pulls in, and it has no effect on a Required need
        std::span<const std::string_view> Declined;
        // a device below one of these limits cannot run some entry point at all, so the planner refuses it
        lodestone::manifest::DeviceLimits RequiredLimits;
        // the VkSurfaceKHR handle as an integer. Not zero: the device's graphics queue family must present to it.
        // Zero: headless, with no present check
        uint64_t PresentSurface{ 0u };
    };

    enum class RhiSystemResultCode : uint8_t
    {
        Invalid = 0,
        Success = 1,
        AlreadyStarted,
        InstanceNotStarted,
        InstanceExtensionMissing,
        InstanceCreateFailed,
        NoPhysicalDevice,
        NoDeviceMeetsRequirements,
        DeviceCreateFailed,
    };

    /** The status of RhiSystem::StartInstance or StartDevice. */
    struct RhiSystemResult
    {
        RhiSystemResultCode Code{ RhiSystemResultCode::Invalid };
        // the need that failed: an extension, "Struct.member", a capability, or a version
        std::string Subject;
        // why, and on which device
        std::string Detail;

        [[nodiscard]] bool Succeeded() const noexcept;
    };

    [[nodiscard]] std::string_view ToString(RhiSystemResultCode code) noexcept;
}

#endif // !DIAMOND_DOGS_RHI_SYSTEM_START_HPP
