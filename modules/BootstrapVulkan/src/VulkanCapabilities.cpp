#include "VulkanCapabilities.hpp"
#include "DeviceCapability.hpp"
#include "DeviceRequirements.hpp"
#include <span>

namespace
{
    using bootstrap::NeedKind;
    using bootstrap::VulkanEnable;

    constexpr VulkanEnable k_DrawIndirectCount[]
    {
        VulkanEnable{ .Kind = NeedKind::DeviceFeature, .Name = "VkPhysicalDeviceVulkan12Features.drawIndirectCount" },
    };
}

namespace bootstrap
{
    std::span<const VulkanEnable> VulkanEnablesOf(DeviceCapability capability) noexcept
    {
        switch (capability)
        {
        case DeviceCapability::DrawIndirectCount:
            return k_DrawIndirectCount;
        default:
            return {};
        }
    }
}
