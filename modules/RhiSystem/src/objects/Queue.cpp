#include "Queue.hpp"
#include "RhiHandle.hpp"
#include "RhiResult.hpp"
#include "RhiTypes.hpp"
#include "containers/SmallVector.hpp"
#include "core/VulkanConversions.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <vulkan/vulkan_core.h>

namespace rhi
{
#ifdef RHI_SYSTEM_USE_VULKAN
    namespace
    {
        struct ToVulkanFn
        {

            [[nodiscard]] constexpr VkSemaphoreSubmitInfo operator()(const SemaphoreSubmitInfo& info) noexcept
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

            [[nodiscard]] constexpr VkCommandBufferSubmitInfo operator()(const CommandBufferHandle& command_buffer) noexcept
            {
                return VkCommandBufferSubmitInfo
                {
                    VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                    nullptr,
                    command_buffer.As<VkCommandBuffer>(),
                    0
                };
            }

        };

        inline constexpr ToVulkanFn ToVulkan{};

    }

    Result QueueSubmit(const QueueHandle& queue, std::span<const SubmitInfo> submits, const FenceHandle& fence) noexcept
    {
        using namespace foundation;
        // every batch's arrays go into these three, and each VkSubmitInfo2 points at its own slice
        size_t semaphoreCount = 0;
        size_t commandBufferCount = 0;
        for (const SubmitInfo& submit : submits)
        {
            semaphoreCount += submit.Waits.size() + submit.Signals.size();
            commandBufferCount += submit.CommandBuffers.size();
        }

        // usually twice the number of semaphores as command buffers, and usually twice the number of
        // command buffers as submits (approximately). this is still ~3kb or so, under a stack page
        SmallVector<VkSemaphoreSubmitInfo, 32> semaphores;
        SmallVector<VkCommandBufferSubmitInfo, 16> commandBuffers;
        SmallVector<VkSubmitInfo2, 8> vkSubmits;
        // this catches if the precomputed counts are under what we need, and makes sure we 
        // don't lose the .data() pointer mid-walk from appending
        semaphores.reserve(semaphoreCount);
        commandBuffers.reserve(commandBufferCount);
        vkSubmits.reserve(submits.size());
        // sloppy, but I want to know ahead of time if we spill over the inline storage of these SmallVectors
        assert(semaphores.is_inline() && commandBuffers.is_inline() && vkSubmits.is_inline());

        for (const SubmitInfo& submit : submits)
        {

            const size_t firstWait = semaphores.size();
            semaphores.append_range(submit.Waits | std::views::transform(ToVulkan));
            const size_t firstSignal = semaphores.size();
            semaphores.append_range(submit.Signals | std::views::transform(ToVulkan));
            const size_t firstCommandBuffer = commandBuffers.size();
            commandBuffers.append_range(submit.CommandBuffers | std::views::transform(ToVulkan));

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
