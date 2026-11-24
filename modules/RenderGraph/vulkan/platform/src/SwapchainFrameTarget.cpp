#include "SwapchainFrameTarget.hpp"
#include "Device.hpp"
#include "LoomFormats.hpp"
#include "PlatformTypes.hpp"
#include "Swapchain.hpp"
#include <vulkan/vulkan_core.h>
#include <cstdint>
#include <optional>

namespace loom
{

    SwapchainFrameTarget::SwapchainFrameTarget(const rhi::Device& _device, Swapchain& _swapchain, int32_t frame_slots) :
        device{ _device },
        swapchain{ _swapchain }
    {
        // a semaphore is free again once the submit that waited on it finished: one more than the frames in flight
        const int32_t count = frame_slots + 1;
        acquireSemaphores.reserve(static_cast<size_t>(count));

        for (int32_t index = 0; index < count; ++index)
        {
            acquireSemaphores.emplace_back(device.Handle());
        }
    }

    SwapchainFrameTarget::~SwapchainFrameTarget() = default;

    Extent2D SwapchainFrameTarget::CurrentExtent() const
    {
        const math::Float2 extent = swapchain.GetExtent();
        return Extent2D{ .Width = static_cast<int32_t>(extent.x), .Height = static_cast<int32_t>(extent.y) };
    }

    bool SwapchainFrameTarget::ResizeFinished() const
    {
        // GLFW and the browsers give no end-of-resize event: the settle window decides
        return false;
    }

    bool SwapchainFrameTarget::recreateWhenIdle(const rhi::GpuWait& last_submit)
    {
        if (last_submit.Submitted && last_submit.Value > 0u)
        {
            const VkSemaphore timeline = reinterpret_cast<VkSemaphore>(last_submit.Semaphore.Get());
            uint64_t completed = 0u;

            if (vkGetSemaphoreCounterValue(device.Handle().As<VkDevice>(), timeline, &completed) != VK_SUCCESS ||
                completed < last_submit.Value)
            {
                return false;
            }
        }

        // the graph's frames finished; the present engine may still hold an image, so the wait is short, not none
        vkDeviceWaitIdle(device.Handle().As<VkDevice>());

        if (swapchain.Recreate() != SwapchainRecreateResult::Success)
        {
            return false;
        }

        needsRecreate = false;
        ++recreateCount;
        return true;
    }

    std::optional<AcquiredTarget> SwapchainFrameTarget::TryAcquire(const rhi::GpuWait& last_submit)
    {
        if (needsRecreate && !recreateWhenIdle(last_submit))
        {
            return std::nullopt;
        }

        const Extent2D extent = CurrentExtent();

        if (extent.IsEmpty())
        {
            return std::nullopt;
        }

        const rhi::BinarySemaphore& semaphore = acquireSemaphores[nextSemaphore];
        const VkResult result = vkAcquireNextImageKHR(device.Handle().As<VkDevice>(),
                                                      static_cast<VkSwapchainKHR>(swapchain.GetNativeHandle()), 0u,
                                                      semaphore.Handle().As<VkSemaphore>(), VK_NULL_HANDLE,
                                                      &acquiredIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            needsRecreate = true;
            return std::nullopt;
        }

        // a suboptimal image is acquired, and its semaphore signals: draw into it, then recreate
        if (result == VK_SUBOPTIMAL_KHR)
        {
            needsRecreate = true;
        }
        else if (result != VK_SUCCESS)
        {
            // VK_NOT_READY or VK_TIMEOUT: no image yet, never a wait
            return std::nullopt;
        }

        nextSemaphore = (nextSemaphore + 1u) % static_cast<uint32_t>(acquireSemaphores.size());
        const std::optional<lodestone::TextureFormat> format = ToTextureFormat(swapchain.ImageFormat());

        return AcquiredTarget
        {
            .Image = rhi::ImageHandle{ reinterpret_cast<uint64_t>(swapchain.ImageHandle(acquiredIndex)) },
            .View = rhi::ImageViewHandle{ reinterpret_cast<uint64_t>(swapchain.ImageViewHandle(acquiredIndex)) },
            .Format = format.value_or(lodestone::TextureFormat::Invalid),
            .Extent = extent,
            .Acquired = semaphore.Handle(),
            .ImageIndex = acquiredIndex,
            .ImageCount = swapchain.ImageCount(),
        };
    }

    void SwapchainFrameTarget::Present(rhi::BinarySemaphoreHandle render_done)
    {
        const VkSemaphore wait = render_done.As<VkSemaphore>();
        const VkSwapchainKHR handle = static_cast<VkSwapchainKHR>(swapchain.GetNativeHandle());

        const VkPresentInfoKHR presentInfo
        {
            VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            nullptr,
            1u,
            &wait,
            1u,
            &handle,
            &acquiredIndex,
            nullptr
        };

        const VkResult result = vkQueuePresentKHR(device.GetGeneralQueue().As<VkQueue>(), &presentInfo);

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        {
            needsRecreate = true;
        }
    }

    rhi::ImageLayout SwapchainFrameTarget::FinalLayout() const
    {
        return rhi::ImageLayout::PresentSource;
    }

    uint32_t SwapchainFrameTarget::RecreateCount() const noexcept
    {
        return recreateCount;
    }

}
