#pragma once
#ifndef DIAMOND_DOGS_LOOM_VULKAN_FRAME_TARGET_HPP
#define DIAMOND_DOGS_LOOM_VULKAN_FRAME_TARGET_HPP
#include "LoomKeys.hpp"
#include "ResourceFuture.hpp"
#include "ResourceFlags.hpp"
#include "ResourceTypes.hpp"
#include "RhiFlags.hpp"
#include "RhiHandle.hpp"
#include "TargetExtents.hpp"
#include <cstdint>
#include <optional>

// A frame target (spec T2, step 7 Q6): what Loom draws the frame into and hands back. The swapchain, an offscreen
// image and (on WebGPU) a canvas implement it. Loom holds a reference; it never owns the target. No call blocks.
namespace loom
{

    // One acquired image. Acquired is the binary semaphore that the acquire signals; Loom's submit waits on it before
    // the first use of the image. RenderDone is the binary semaphore that Loom's submit signals, and that the present
    // waits on. The target owns both. An offscreen target has neither (null handles).
    struct AcquiredTarget
    {
        rhi::ImageHandle Image;
        rhi::ImageViewHandle View;
        lodestone::TextureFormat Format{ lodestone::TextureFormat::Invalid };
        Extent2D Extent;
        rhi::BinarySemaphoreHandle Acquired;
        rhi::BinarySemaphoreHandle RenderDone;
    };

    class FrameTarget
    {
    public:
        virtual ~FrameTarget() = default;

        // The extent the target has now (a swapchain's extent, a canvas's drawing size). Loom feeds it to the settle
        // rule each Tick, before it decides, so the OfTarget images exist before the first acquire.
        [[nodiscard]] virtual Extent2D CurrentExtent() const = 0;

        // An optional hint: the user finished a resize, so the allocation may follow at once.
        [[nodiscard]] virtual bool ResizeFinished() const = 0;

        // An image to draw into, or nothing this Tick (a swapchain that is out of date, or a minimized window). Never
        // blocks. last_submit is Loom's last submit: a swapchain recreates itself only after it passed.
        [[nodiscard]] virtual std::optional<AcquiredTarget> TryAcquire(const rhi::GpuWait& last_submit) = 0;

        // Hands the acquired image back after its RenderDone semaphore signals. Never blocks.
        virtual void Present() = 0;

        // Gives back an acquired image that this Tick did not use (another target gave nothing). False: the target
        // cannot give it back without a present, so Loom keeps it acquired for the next Tick.
        [[nodiscard]] virtual bool Release() = 0;

        // The layout the target takes its image back in: PresentSource for a swapchain. Undefined keeps the image in
        // the layout of its last use (an offscreen target that a test reads afterwards picks TransferSource).
        [[nodiscard]] virtual rhi::ImageLayout FinalLayout() const = 0;

        // Blocks until every present that this target made has finished, or until timeout_ns passes. Loom calls it
        // before it destroys the render-done semaphores. False: the time passed, or the target cannot tell (a
        // swapchain without present fences), and the caller must wait for the device instead.
        [[nodiscard]] virtual bool WaitForPresents(uint64_t timeout_ns) = 0;
    };

    // Which object implements each imported FrameTargetKey (RealizeInputs::FrameTargets)
    struct FrameTargetBinding
    {
        FrameTargetKey Key;
        FrameTarget& Target;
    };

    // An offscreen frame target over one image that the caller created, for headless runs and tests. It needs no
    // semaphore: Loom's own submit orders every use. Acquire always succeeds while the extent is not empty.
    class OffscreenFrameTarget final : public FrameTarget
    {
    public:
        OffscreenFrameTarget(GraphicsResource image, lodestone::TextureFormat format, Extent2D extent,
                             rhi::ImageLayout final_layout) noexcept;

        [[nodiscard]] Extent2D CurrentExtent() const override;
        [[nodiscard]] bool ResizeFinished() const override;
        [[nodiscard]] std::optional<AcquiredTarget> TryAcquire(const rhi::GpuWait& last_submit) override;
        void Present() override;
        [[nodiscard]] bool Release() override;
        [[nodiscard]] rhi::ImageLayout FinalLayout() const override;
        // Nothing to wait for: an offscreen present is only a count
        [[nodiscard]] bool WaitForPresents(uint64_t timeout_ns) override;

        // The count of Present calls: the frames that were handed back
        [[nodiscard]] uint32_t PresentCount() const noexcept;

    private:
        GraphicsResource image;
        lodestone::TextureFormat format;
        Extent2D extent;
        rhi::ImageLayout finalLayout;
        uint32_t presentCount{ 0u };
    };

}

#endif // !DIAMOND_DOGS_LOOM_VULKAN_FRAME_TARGET_HPP
