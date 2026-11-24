#pragma once
#ifndef DIAMOND_DOGS_LOOM_TARGET_EXTENTS_HPP
#define DIAMOND_DOGS_LOOM_TARGET_EXTENTS_HPP
#include <cstdint>

// The three extents of a frame target (step 7, round 2 Q1), and the settle rule that keeps a resize from reallocating
// every frame. Pure, so every case is tested with no window; each backend calls it once per acquire.
//  - Target: what the frame target reports (the swapchain extent, or a canvas's CSS size x devicePixelRatio).
//  - Allocation: the extent the OfTarget images are made at. It lags the target, and takes the target's value only
//    after the target has kept one value for the settle window, or when the target says its resize finished.
//  - Render: min(target x render scale, allocation). Viewports and scissors use it, so neither a drag nor a change of
//    render scale reallocates anything. A scene rendered at the old allocation is scaled to the frame target by the
//    pass that writes it (LoomFrame gives the ratio).
namespace loom
{

    struct Extent2D
    {
        int32_t Width{ 0 };
        int32_t Height{ 0 };

        [[nodiscard]] bool IsEmpty() const noexcept
        {
            return Width <= 0 || Height <= 0;
        }

        bool operator==(const Extent2D&) const noexcept = default;
    };

    struct TargetExtents
    {
        Extent2D Target;
        Extent2D Allocation;            // empty until the first non-empty report
        Extent2D Render;
        float RenderScale{ 1.0f };      // 0 < RenderScale <= 1 (SetRenderScale)
        double StableSeconds{ 0.0 };    // how long Target has kept its value
    };

    enum class ExtentChange : uint8_t
    {
        Invalid = 0,
        None = 1,
        Render = 2,             // only the render extent changed: viewports, no new images
        Reallocate = 3          // the allocation extent changed: make the OfTarget images again
    };

    // One acquire's report. delta_seconds is Tick's dT. An empty extent (a minimized window) never becomes the
    // allocation. The first non-empty report allocates at once: there is nothing to keep showing yet.
    [[nodiscard]] ExtentChange UpdateTargetExtents(TargetExtents& state,
                                                   Extent2D reported,
                                                   double delta_seconds,
                                                   double settle_seconds,
                                                   bool resize_finished);

    // The scalability hook (step 7 Q4). False, and no change, unless 0 < scale <= 1. The render extent follows at once.
    [[nodiscard]] bool SetRenderScale(TargetExtents& state, float scale);

    // extent x scale per axis, rounded to the nearest pixel, at least 1 pixel. An OfTarget image is
    // ScaledExtent(allocation, its declared scale).
    [[nodiscard]] Extent2D ScaledExtent(Extent2D extent, float scale);

}

#endif // !DIAMOND_DOGS_LOOM_TARGET_EXTENTS_HPP
