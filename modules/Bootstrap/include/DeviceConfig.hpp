#pragma once
#ifndef DIAMOND_DOGS_BOOTSTRAP_DEVICE_CONFIG_HPP
#define DIAMOND_DOGS_BOOTSTRAP_DEVICE_CONFIG_HPP
#include "RhiFlags.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace bootstrap
{
    struct BootResult;
    class LaunchStage;

    enum class PowerPreference : uint8_t
    {
        Default = 0,
        LowPower = 1,
        HighPerformance = 2
    };

    /** What the device stage creates the instance and the device with. ReadDeviceConfig fills it from the config
     *  file's "Device" section. A field that the section does not name keeps its value. */
    struct DeviceConfig
    {
        rhi::ApiVersion ApiVersion{ rhi::ApiVersion::Vulkan13 };
        rhi::ValidationLayers Validation{ rhi::ValidationLayers::None };
        std::string ApplicationName{ "DiamondDogs Application" };
        // Wanted needs of any kind to leave off, so that the fallback paths run on a device that has them
        std::vector<std::string> Declined;
        PowerPreference Power{ PowerPreference::Default };
    };

    /** @brief Reads the "Device" section of the config file into `config`. No config file, or no section, leaves
     *  `config` unchanged. Fails with ConfigInvalid, naming the key, when a key is unknown or a value has the wrong
     *  type or is not a known name. Keys: ApiVersion ("1.0" to "1.4", "Latest"), Validation ("None", "BaseOnly",
     *  "WithSynchronization", "Full", "CrashDiagnostic"), ApplicationName, Declined (an array of names). */
    [[nodiscard]] BootResult ReadDeviceConfig(const LaunchStage& launch, DeviceConfig& config);
}

#endif // !DIAMOND_DOGS_BOOTSTRAP_DEVICE_CONFIG_HPP
