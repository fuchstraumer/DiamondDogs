#pragma once
#ifndef DIAMOND_DOGS_COLOR_PIPE_COLOR_COMPOSITE_HPP
#define DIAMOND_DOGS_COLOR_PIPE_COLOR_COMPOSITE_HPP
#include "ColorSpaces.hpp"
#include "DisplayTarget.hpp"
#include <array>
#include <cstdint>

// double-precision CPU reference of ColorComposite.slang, with the same steps in the same order
namespace color
{
    /** @brief One UI texel as the RGBA8 UNORM target stores it */
    using UiTexel = std::array<uint8_t, 4>;

    struct CompositeSettings
    {
        DisplayTarget Target{};
        TonemapperKind Tonemapper{ TonemapperKind::Invalid };
    };

    /** @brief The number of exposure zones of FalseColorEv: -5 EV to +5 EV in steps of 1 EV */
    inline constexpr int32_t k_FalseColorZoneCount = 11;
    inline constexpr int32_t k_FalseColorLowestZoneEv = -5;
    /** @brief The scene-linear value of 0 EV in FalseColorEv */
    inline constexpr double k_MiddleGray = 0.18;

    [[nodiscard]] Vec3d ApplyExposure(const Vec3d& scene_ap1, double exposure_ev) noexcept;

    /** @brief Returns the paper-white-relative linear Rec.2020 color. Invalid, KhronosNeutral and AgX return NaN: no
     *  reference exists for them yet. */
    [[nodiscard]] Vec3d Tonemap(TonemapperKind kind, const Vec3d& exposed_ap1, const DisplayTarget& target) noexcept;

    /** @brief Returns the exposure of `luminance` in stops from middle gray, which FalseColorEv rounds to a zone. A
     *  luminance of 0 or less reads as a very low value, and not as minus infinity. */
    [[nodiscard]] double FalseColorEvOf(double luminance) noexcept;

    /** @brief Returns the zone of FalseColorEv for the exposure `ev`: 0 for -5 EV and below, 10 for +5 EV and above */
    [[nodiscard]] int32_t FalseColorZoneOf(double ev) noexcept;

    [[nodiscard]] Vec3d ToTargetPrimaries(const Vec3d& rec2020, const DisplayTarget& target) noexcept;
    [[nodiscard]] Vec3d ClipToHeadroom(const Vec3d& relative, const DisplayTarget& target) noexcept;
    [[nodiscard]] Vec3d CompositeUi(const Vec3d& scene_nits, const UiTexel& ui, const DisplayTarget& target) noexcept;

    /** @brief Returns the value that the pass writes to the swapchain image: the sRGB signal and the PQ signal are in
     *  [0, 1], and an scRGB value is linear with 1.0 at 80 nits. */
    [[nodiscard]] Vec3d EncodeForDisplay(const Vec3d& nits, const DisplayTarget& target) noexcept;

    /** @brief Runs every step for one pixel. `scene_ap1` is the rgb of the scene target. */
    [[nodiscard]] Vec3d CompositePixel(const CompositeSettings& settings, const Vec3d& scene_ap1, const UiTexel& ui, double exposure_ev) noexcept;
}

#endif // !DIAMOND_DOGS_COLOR_PIPE_COLOR_COMPOSITE_HPP
