#include "HeadlessBoot.hpp"
#include "BootResult.hpp"
#include "DeviceConfig.hpp"
#include "DeviceRequirements.hpp"
#include "ModuleDescriptor.hpp"
#include "SpineNeeds.hpp"
#include <iostream>
#include <span>
#include <vector>

namespace
{
    [[nodiscard]] bool Report(const bootstrap::BootResult& result);
}

bool StartHeadlessBoot(HeadlessBoot& boot,
                       std::span<char* const> arguments,
                       std::span<const bootstrap::BundleRequest> bundles,
                       std::span<const bootstrap::Need> app_needs,
                       const bootstrap::DeviceConfig& config)
{
    if (!Report(boot.Launch.Start(arguments, std::span<const bootstrap::ModuleDescriptor>{}, bootstrap::Stage::Gpu)))
    {
        return false;
    }

    if (!Report(boot.Core.Start(boot.Launch)))
    {
        return false;
    }

    if (!Report(boot.Content.Start(boot.Core, bundles)))
    {
        return false;
    }

    std::vector<bootstrap::Need> needs = bootstrap::HeadlessSpineNeeds();

    needs.append_range(app_needs);

    const bootstrap::DeviceRequirements requirements = bootstrap::GatherDeviceRequirements(boot.Content, needs);

    if (!Report(boot.Device.StartHeadless(boot.Content, requirements, config)))
    {
        return false;
    }

    return Report(boot.Gpu.StartHeadless(boot.Device));
}

namespace
{
    bool Report(const bootstrap::BootResult& result)
    {
        if (!result.Succeeded())
        {
            std::cerr << bootstrap::FormatFailure(result) << "\n";
        }

        return result.Succeeded();
    }
}
