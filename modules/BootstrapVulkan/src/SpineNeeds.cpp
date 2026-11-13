#include "SpineNeeds.hpp"
#include "WindowStage.hpp"
#include <span>
#include <vector>

namespace
{
    using bootstrap::Need;
    using bootstrap::NeedKind;
    using bootstrap::NeedLevel;

    // ResourceContext: the transfer timeline, vkQueueSubmit2 and vkCmdPipelineBarrier2, and VMA's budget data
    constexpr Need k_GpuSpineNeeds[]
    {
        Need
        {
            .Name = "VkPhysicalDeviceVulkan12Features.timelineSemaphore",
            .Kind = NeedKind::DeviceFeature,
            .Level = NeedLevel::Required,
        },
        Need
        {
            .Name = "VkPhysicalDeviceVulkan13Features.synchronization2",
            .Kind = NeedKind::DeviceFeature,
            .Level = NeedLevel::Required,
        },
        Need
        {
            .Name = "VK_EXT_memory_budget",
            .Kind = NeedKind::DeviceExtension,
            .Level = NeedLevel::Wanted,
        },
    };
}

namespace bootstrap
{
    std::span<const Need> GpuSpineNeeds() noexcept
    {
        return k_GpuSpineNeeds;
    }

    std::vector<Need> SpineNeeds(const WindowStage& window)
    {
        std::vector<Need> needs = HeadlessSpineNeeds();

        needs.append_range(window.Needs());
        return needs;
    }

    std::vector<Need> HeadlessSpineNeeds()
    {
        const std::span<const Need> gpuNeeds = GpuSpineNeeds();

        return std::vector<Need>(gpuNeeds.begin(), gpuNeeds.end());
    }
}
