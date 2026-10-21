#include "Queue.hpp"
#include "RhiHandle.hpp"
#include "RhiResult.hpp"
#include "RhiTypes.hpp"
#include "core/VulkanConversions.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace rhi
{
#ifdef RHI_SYSTEM_USE_VULKAN
    namespace
    {
        VkSemaphoreSubmitInfo ToVulkan(const SemaphoreSubmitInfo& info) noexcept
        {
            return VkSemaphoreSubmitInfo
            {
                VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                nullptr,
                reinterpret_cast<VkSemaphore>(info.Semaphore),
                info.Value,
                vk::ToVulkan(info.Stages),
                0 // deviceIndex
            };
        }
    }

    Result QueueSubmit(const QueueHandle& queue, std::span<const SubmitInfo> submits, const FenceHandle& fence) noexcept
    {
        // every batch's arrays go into these three, and each VkSubmitInfo2 points at its own slice
        size_t semaphoreCount = 0;
        size_t commandBufferCount = 0;
        for (const SubmitInfo& submit : submits)
        {
            semaphoreCount += submit.Waits.size() + submit.Signals.size();
            commandBufferCount += submit.CommandBuffers.size();
        }
        std::vector<VkSemaphoreSubmitInfo> semaphores;
        semaphores.reserve(semaphoreCount);
        std::vector<VkCommandBufferSubmitInfo> commandBuffers;
        commandBuffers.reserve(commandBufferCount);
        std::vector<VkSubmitInfo2> vkSubmits;
        vkSubmits.reserve(submits.size());

        for (const SubmitInfo& submit : submits)
        {
            const size_t firstWait = semaphores.size();
            for (const SemaphoreSubmitInfo& wait : submit.Waits)
            {
                semaphores.push_back(ToVulkan(wait));
            }
            const size_t firstSignal = semaphores.size();
            for (const SemaphoreSubmitInfo& signal : submit.Signals)
            {
                semaphores.push_back(ToVulkan(signal));
            }
            const size_t firstCommandBuffer = commandBuffers.size();
            for (const CommandBufferHandle& commandBuffer : submit.CommandBuffers)
            {
                commandBuffers.push_back(VkCommandBufferSubmitInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, nullptr, commandBuffer.As<VkCommandBuffer>(), 0 });
            }

            vkSubmits.push_back(VkSubmitInfo2
            {
                VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                nullptr,
                0,
                static_cast<uint32_t>(submit.Waits.size()),
                semaphores.data() + firstWait,
                static_cast<uint32_t>(submit.CommandBuffers.size()),
                commandBuffers.data() + firstCommandBuffer,
                static_cast<uint32_t>(submit.Signals.size()),
                semaphores.data() + firstSignal
            });
        }

        return Result{ vkQueueSubmit2(queue.As<VkQueue>(), static_cast<uint32_t>(vkSubmits.size()), vkSubmits.data(), fence.As<VkFence>()) };
    }

    Result QueueSubmit(const QueueHandle& queue, const SubmitInfo& submit, const FenceHandle& fence) noexcept
    {
        return QueueSubmit(queue, std::span<const SubmitInfo>{ &submit, 1 }, fence);
    }

    Result QueueWaitIdle(const QueueHandle& queue) noexcept
    {
        return Result{ vkQueueWaitIdle(queue.As<VkQueue>()) };
    }
#endif // RHI_SYSTEM_USE_VULKAN
}
