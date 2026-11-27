#pragma once
#ifndef DIAMOND_DOGS_BOOTSTRAP_VULKAN_CAPABILITIES_HPP
#define DIAMOND_DOGS_BOOTSTRAP_VULKAN_CAPABILITIES_HPP
#include "DeviceCapability.hpp"
#include "DeviceRequirements.hpp"
#include <span>
#include <string_view>

namespace bootstrap
{
    /** One Vulkan need that a neutral capability maps to: a device extension, or a "Struct.member" feature. */
    struct VulkanEnable
    {
        NeedKind Kind{ NeedKind::Invalid };
        std::string_view Name;
    };

    /** @brief The Vulkan needs of a neutral capability. The device has the capability only when it has every one.
     *  Empty for Invalid. */
    [[nodiscard]] std::span<const VulkanEnable> VulkanEnablesOf(DeviceCapability capability) noexcept;
}

#endif // !DIAMOND_DOGS_BOOTSTRAP_VULKAN_CAPABILITIES_HPP
