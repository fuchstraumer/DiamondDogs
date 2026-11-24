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

    SwapchainFrameTarget::SwapchainFrameTarget(const rhi::Device& _device, Swapchain& _swapchain) :
        device{ _device },
        swapchain{ _swapchain }
    {
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

        // without maintenance1, a present may still wait on a render-done semaphore, and only an idle device proves
        // that it finished; with it, the old swapchain waits for its own presents
        if (!swapchain.UsesMaintenance1())
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

        // with present scaling, a minimized window is not out of date: it has no area, and nothing is drawn
        if (extent.IsEmpty() || !swapchain.WindowHasArea())
        {
            return std::nullopt;
        }

        const SwapchainAcquire acquire = swapchain.TryAcquire(0u);

        switch (acquire.Status)
        {
        case SwapchainAcquireStatus::Acquired:
            break;
        case SwapchainAcquireStatus::Suboptimal:
            // the image is acquired and its semaphore signals: draw into it, then recreate
            needsRecreate = true;
            break;
        case SwapchainAcquireStatus::OutOfDate:
            needsRecreate = true;
            return std::nullopt;
        case SwapchainAcquireStatus::NotReady:
            [[fallthrough]];
        case SwapchainAcquireStatus::Failed:
            [[fallthrough]];
        case SwapchainAcquireStatus::Invalid:
            return std::nullopt;
        }

        acquiredIndex = acquire.ImageIndex;
        const std::optional<lodestone::TextureFormat> format = ToTextureFormat(swapchain.ImageFormat());

        return AcquiredTarget
        {
            .Image = rhi::ImageHandle{ reinterpret_cast<uint64_t>(acquire.Image) },
            .View = rhi::ImageViewHandle{ reinterpret_cast<uint64_t>(acquire.View) },
            .Format = format.value_or(lodestone::TextureFormat::Invalid),
            .Extent = extent,
            .Acquired = rhi::BinarySemaphoreHandle{ acquire.WaitSemaphore },
            .RenderDone = rhi::BinarySemaphoreHandle{ acquire.RenderDoneSemaphore },
        };
    }

    void SwapchainFrameTarget::Present()
    {
        const SwapchainPresentStatus status = swapchain.Present(acquiredIndex);

        if (status == SwapchainPresentStatus::OutOfDate || status == SwapchainPresentStatus::Suboptimal)
        {
            needsRecreate = true;
        }
    }

    bool SwapchainFrameTarget::Release()
    {
        return swapchain.Release(acquiredIndex) == SwapchainReleaseStatus::Released;
    }

    rhi::ImageLayout SwapchainFrameTarget::FinalLayout() const
    {
        return rhi::ImageLayout::PresentSource;
    }

    bool SwapchainFrameTarget::WaitForPresents(uint64_t timeout_ns)
    {
        return swapchain.WaitForPresents(timeout_ns);
    }

    PresentModeChange SwapchainFrameTarget::SetPresentMode(PresentMode mode)
    {
        const PresentModeChange change = swapchain.SetPresentMode(mode);

        if (change == PresentModeChange::NeedsRecreate)
        {
            needsRecreate = true;
        }

        return change;
    }

    uint32_t SwapchainFrameTarget::RecreateCount() const noexcept
    {
        return recreateCount;
    }

}
