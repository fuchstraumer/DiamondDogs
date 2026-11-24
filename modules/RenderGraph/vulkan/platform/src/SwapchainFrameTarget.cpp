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

namespace
{

    // Two presents more than the frames in flight before a fence comes round again
    constexpr size_t k_ExtraPresentFences = 2u;
    constexpr uint64_t k_DestroyTimeoutNs = 10'000'000'000ull;

}

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

        if (!device.IsFeatureEnabled(k_PresentFenceFeature))
        {
            return;
        }

        // a present reuses a fence only after it signaled; the ring is long enough that the wait is almost never real
        const size_t fenceCount = static_cast<size_t>(frame_slots) + k_ExtraPresentFences;
        const VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, 0u };
        presentFences.resize(fenceCount, VK_NULL_HANDLE);
        fencePending.resize(fenceCount, 0u);

        for (VkFence& fence : presentFences)
        {
            vkCreateFence(device.Handle().As<VkDevice>(), &fenceInfo, nullptr, &fence);
        }
    }

    SwapchainFrameTarget::~SwapchainFrameTarget()
    {
        [[maybe_unused]] const bool finished = WaitForPresents(k_DestroyTimeoutNs);

        for (const VkFence fence : presentFences)
        {
            vkDestroyFence(device.Handle().As<VkDevice>(), fence, nullptr);
        }
    }

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

        // the graph's frames finished, but a present may still wait on a render-done semaphore: its fence proves it
        // finished, and without fences only an idle device does
        if (!WaitForPresents(k_DestroyTimeoutNs))
        {
            vkDeviceWaitIdle(device.Handle().As<VkDevice>());
        }

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
        VkFence fence = VK_NULL_HANDLE;

        if (!presentFences.empty())
        {
            fence = takeFence();
        }

        const VkSwapchainPresentFenceInfoKHR fenceInfo
        {
            VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_KHR,
            nullptr,
            1u,
            &fence
        };

        const VkPresentInfoKHR presentInfo
        {
            VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            fence != VK_NULL_HANDLE ? &fenceInfo : nullptr,
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

    VkFence SwapchainFrameTarget::takeFence()
    {
        const VkDevice vulkanDevice = device.Handle().As<VkDevice>();
        const VkFence fence = presentFences[nextFence];

        if (fencePending[nextFence] != 0u)
        {
            [[maybe_unused]] const VkResult waited = vkWaitForFences(vulkanDevice, 1u, &fence, VK_TRUE, k_DestroyTimeoutNs);
        }

        vkResetFences(vulkanDevice, 1u, &fence);
        fencePending[nextFence] = 1u;
        nextFence = (nextFence + 1u) % static_cast<uint32_t>(presentFences.size());
        return fence;
    }

    bool SwapchainFrameTarget::WaitForPresents(uint64_t timeout_ns)
    {
        if (presentFences.empty())
        {
            return false;
        }

        const VkDevice vulkanDevice = device.Handle().As<VkDevice>();

        for (size_t index = 0u; index < presentFences.size(); ++index)
        {
            if (fencePending[index] == 0u)
            {
                continue;
            }

            if (vkWaitForFences(vulkanDevice, 1u, &presentFences[index], VK_TRUE, timeout_ns) != VK_SUCCESS)
            {
                return false;
            }

            fencePending[index] = 0u;
        }

        return true;
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
