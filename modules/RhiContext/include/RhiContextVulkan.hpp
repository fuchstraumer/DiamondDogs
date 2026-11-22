#pragma once
#ifndef DIAMOND_DOGS_RHI_CONTEXT_VULKAN_HPP
#define DIAMOND_DOGS_RHI_CONTEXT_VULKAN_HPP
#include "RhiContext.hpp"

#include <cstdint>

namespace rhi
{

class Instance;
class Device;
class RhiSystem;

struct VulkanRhiContextView
{
    const Instance* InstancePtr{ nullptr };
    const Device* DevicePtr{ nullptr };
    uint32_t VulkanApiVersion{ 0u };
    bool ValidationEnabled{ false };
};

[[nodiscard]] RhiContext MakeVulkanRhiContext(RhiSystem& rhi_system, bool validation_enabled);
[[nodiscard]] RhiContext MakeVulkanRhiContext(const VulkanRhiContextView& view,
                                              const lodestone::manifest::DeviceLimits& limits);
[[nodiscard]] VulkanRhiContextView UnwrapVulkanRhiContext(const RhiContext& context) noexcept;

} // namespace rhi

#endif // !DIAMOND_DOGS_RHI_CONTEXT_VULKAN_HPP
