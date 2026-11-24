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
    // the first use of the image. An offscreen target has no semaphore (a null handle). ImageIndex and ImageCount let
    // Loom keep one render-done semaphore per image, because a present may not reuse one that is still pending.
    struct AcquiredTarget
    {
        rhi::ImageHandle Image;
        rhi::ImageViewHandle View;
        lodestone::TextureFormat Format{ lodestone::TextureFormat::Invalid };
        Extent2D Extent;
        rhi::BinarySemaphoreHandle Acquired;
        uint32_t ImageIndex{ 0u };
        uint32_t ImageCount{ 0u };
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

        // Hands the image back after `render_done` signals. Never blocks. An offscreen target gets a null handle.
        virtual void Present(rhi::BinarySemaphoreHandle render_done) = 0;

        // The layout the target takes its image back in: PresentSource for a swapchain. Undefined keeps the image in
        // the layout of its last use (an offscreen target that a test reads afterwards picks TransferSource).
        [[nodiscard]] virtual rhi::ImageLayout FinalLayout() const = 0;
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
        void Present(rhi::BinarySemaphoreHandle render_done) override;
        [[nodiscard]] rhi::ImageLayout FinalLayout() const override;

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
