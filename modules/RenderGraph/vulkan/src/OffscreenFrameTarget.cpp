#include "FrameTarget.hpp"
#include <optional>

namespace loom
{

    OffscreenFrameTarget::OffscreenFrameTarget(GraphicsResource _image, lodestone::TextureFormat _format,
                                               Extent2D _extent, rhi::ImageLayout final_layout) noexcept :
        image{ _image },
        format{ _format },
        extent{ _extent },
        finalLayout{ final_layout }
    {
    }

    Extent2D OffscreenFrameTarget::CurrentExtent() const
    {
        return extent;
    }

    bool OffscreenFrameTarget::ResizeFinished() const
    {
        return true;
    }

    std::optional<AcquiredTarget> OffscreenFrameTarget::TryAcquire([[maybe_unused]] const rhi::GpuWait& last_submit)
    {
        if (extent.IsEmpty())
        {
            return std::nullopt;
        }

        return AcquiredTarget
        {
            .Image = rhi::ImageHandle{ image.VkHandle },
            .View = rhi::ImageViewHandle{ image.VkViewHandle },
            .Format = format,
            .Extent = extent,
            .Acquired = rhi::BinarySemaphoreHandle{},
            .ImageIndex = 0u,
            .ImageCount = 0u,
        };
    }

    void OffscreenFrameTarget::Present([[maybe_unused]] rhi::BinarySemaphoreHandle render_done)
    {
        ++presentCount;
    }

    rhi::ImageLayout OffscreenFrameTarget::FinalLayout() const
    {
        return finalLayout;
    }

    bool OffscreenFrameTarget::WaitForPresents([[maybe_unused]] uint64_t timeout_ns)
    {
        return true;
    }

    uint32_t OffscreenFrameTarget::PresentCount() const noexcept
    {
        return presentCount;
    }

}
