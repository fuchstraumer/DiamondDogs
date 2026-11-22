#include "FutureTable.hpp"
#include "RhiHandle.hpp"
#include <cstdint>
#include <vulkan/vulkan_core.h>

namespace rhi
{

bool WaitForTransferTimeline(void* wait_device,
                             TimelineSemaphoreHandle transfer_timeline,
                             uint64_t wait_value,
                             uint64_t timeout_ns) noexcept
{
    if (wait_device == nullptr || !transfer_timeline.IsValid())
    {
        return true;
    }

    const VkDevice vkDevice = reinterpret_cast<VkDevice>(wait_device);
    const VkSemaphore semaphore = transfer_timeline.As<VkSemaphore>();
    const VkSemaphoreWaitInfo waitInfo
    {
        VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        nullptr,
        0u,
        1u,
        &semaphore,
        &wait_value
    };

    const VkResult result = vkWaitSemaphores(vkDevice, &waitInfo, timeout_ns);
    return result == VK_SUCCESS;
}

} // namespace rhi
