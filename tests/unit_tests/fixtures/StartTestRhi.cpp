#include "StartTestRhi.hpp"
#include "RhiSystem.hpp"
#include <algorithm>
#include <vector>

namespace test_rhi
{
    rhi::RhiSystemResult StartTestRhi(rhi::RhiSystem& system,
                                      std::string_view application_name,
                                      rhi::ApiVersion api_version,
                                      rhi::ValidationLayers validation,
                                      const Needs& needs)
    {
        std::vector<rhi::DeviceNeed> instanceExtensions(needs.InstanceExtensions.begin(), needs.InstanceExtensions.end());

        // the messenger and object names need debug utils
        if (!std::ranges::contains(instanceExtensions, std::string_view{ "VK_EXT_debug_utils" }, &rhi::DeviceNeed::Name))
        {
            instanceExtensions.push_back(rhi::DeviceNeed{ .Name = "VK_EXT_debug_utils", .Level = rhi::DeviceNeedLevel::Wanted });
        }

        const rhi::InstanceStartInfo instanceInfo
        {
            .VkVersion = api_version,
            .ValidationLevel = validation,
            .ApplicationName = application_name,
            .EngineName = "DiamondDogsTestEngine",
            .Extensions = instanceExtensions,
        };
        rhi::RhiSystemResult started = system.StartInstance(instanceInfo);

        if (!started.Succeeded())
        {
            return started;
        }

        const rhi::DeviceStartInfo deviceInfo
        {
            .Extensions = needs.DeviceExtensions,
            .Features = needs.Features,
        };

        return system.StartDevice(deviceInfo);
    }
}
