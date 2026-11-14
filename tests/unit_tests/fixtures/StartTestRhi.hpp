#pragma once
#ifndef DIAMOND_DOGS_START_TEST_RHI_HPP
#define DIAMOND_DOGS_START_TEST_RHI_HPP
#include "QueryPool.hpp"
#include "RhiFlags.hpp"
#include "RhiSystemStart.hpp"
#include "ShaderProgram.hpp"
#include <span>
#include <string_view>

namespace rhi
{
    class RhiSystem;
}

// The unit tests sit below bootstrap, so they start RhiSystem directly. These are the needs that bootstrap's
// GpuSpineNeeds and ShaderObjectNeeds name, for the tests that use the same systems
namespace test_rhi
{
    // ResourceContext, rhi::QueueSubmit and the frame timeline: timeline semaphores and synchronization2
    inline constexpr rhi::DeviceNeed k_SyncFeatures[]
    {
        rhi::DeviceNeed{ .Name = "VkPhysicalDeviceVulkan12Features.timelineSemaphore", .Level = rhi::DeviceNeedLevel::Required },
        rhi::DeviceNeed{ .Name = "VkPhysicalDeviceVulkan13Features.synchronization2", .Level = rhi::DeviceNeedLevel::Required },
    };

    inline constexpr rhi::DeviceNeed k_ShaderObjectFeatures[]
    {
        rhi::DeviceNeed{ .Name = rhi::k_ShaderObjectFeature, .Level = rhi::DeviceNeedLevel::Required },
    };

    // GpuTimers resets its query pools from the host, and records into command buffers that rhi::QueueSubmit sends
    inline constexpr rhi::DeviceNeed k_GpuTimersFeatures[]
    {
        rhi::DeviceNeed{ .Name = "VkPhysicalDeviceVulkan12Features.timelineSemaphore", .Level = rhi::DeviceNeedLevel::Required },
        rhi::DeviceNeed{ .Name = "VkPhysicalDeviceVulkan13Features.synchronization2", .Level = rhi::DeviceNeedLevel::Required },
        rhi::DeviceNeed{ .Name = rhi::k_QueryPoolFeature, .Level = rhi::DeviceNeedLevel::Required },
    };

    // CommandBufferTest draws with vkCmdBeginRendering
    inline constexpr rhi::DeviceNeed k_RenderingFeatures[]
    {
        rhi::DeviceNeed{ .Name = "VkPhysicalDeviceVulkan12Features.timelineSemaphore", .Level = rhi::DeviceNeedLevel::Required },
        rhi::DeviceNeed{ .Name = "VkPhysicalDeviceVulkan13Features.synchronization2", .Level = rhi::DeviceNeedLevel::Required },
        rhi::DeviceNeed{ .Name = "VkPhysicalDeviceVulkan13Features.dynamicRendering", .Level = rhi::DeviceNeedLevel::Required },
    };

    // VMA reads the budget when the device has it
    inline constexpr rhi::DeviceNeed k_MemoryBudgetExtension[]
    {
        rhi::DeviceNeed{ .Name = "VK_EXT_memory_budget", .Level = rhi::DeviceNeedLevel::Wanted },
    };

    /** What one test starts its RhiSystem with. Each list may be empty. */
    struct Needs
    {
        std::span<const rhi::DeviceNeed> InstanceExtensions;
        std::span<const rhi::DeviceNeed> DeviceExtensions;
        std::span<const rhi::DeviceNeed> Features;
    };

    /** @brief Starts `system` headless: StartInstance with VK_EXT_debug_utils (Wanted) and the instance extensions in
     *  `needs`, then StartDevice with the device extensions and features in `needs`. Returns the first failure. */
    [[nodiscard]] rhi::RhiSystemResult StartTestRhi(rhi::RhiSystem& system,
                                                    std::string_view application_name,
                                                    rhi::ApiVersion api_version,
                                                    rhi::ValidationLayers validation,
                                                    const Needs& needs);
}

#endif // !DIAMOND_DOGS_START_TEST_RHI_HPP
