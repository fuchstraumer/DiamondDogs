#pragma once
#ifndef DIAMOND_DOGS_COLOR_PIPE_DISPLAY_TARGET_HPP
#define DIAMOND_DOGS_COLOR_PIPE_DISPLAY_TARGET_HPP
#include "ColorSpaces.hpp"
#include "ImageDataFormats.hpp"
#include "PlatformTypes.hpp"
#include <cstdint>
#include <optional>
#include <span>

namespace color
{
    enum class DisplayOutput : uint8_t
    {
        Invalid = 0,
        Sdr,
        Hdr,
    };

    /** @brief How the composite pass encodes its result. It decides the output primaries too. */
    enum class DisplayEncoding : uint8_t
    {
        Invalid = 0,
        SrgbNonlinear,
        ScRgbLinear,
        Hdr10Pq,
    };

    enum class TonemapperKind : uint8_t
    {
        Invalid = 0,
        Passthrough,
        FalseColorEv,
        KhronosNeutral,
        AgX,
    };

    /** @brief What the application asks for. ColorPipe::Target reports what the pipe got. */
    struct ColorPipePreferences
    {
        DisplayOutput Output{ DisplayOutput::Invalid };
        DisplayEncoding Encoding{ DisplayEncoding::Invalid };
        TonemapperKind Tonemapper{ TonemapperKind::Invalid };
    };

    /** @brief Values that replace what the platform reports */
    struct DisplayOverrides
    {
        std::optional<float> PeakNits{ std::nullopt };
        std::optional<float> BlackNits{ std::nullopt };
        std::optional<float> PaperWhiteNits{ std::nullopt };
    };

    /** @brief The pair that ChooseSurface picked. `Encoding` is Invalid when no pair is usable. */
    struct SurfaceChoice
    {
        ImageFormat Format{};
        ColorSpace Space{ ColorSpace::Invalid };
        DisplayEncoding Encoding{ DisplayEncoding::Invalid };
    };

    /** @brief The numbers that the composite pass needs about the display */
    struct DisplayTarget
    {
        float PeakNits{ 0.0f };
        float BlackNits{ 0.0f };
        float PaperWhiteNits{ 0.0f };
        /** @brief Rec.709 for sRGB and scRGB, and Rec.2020 for PQ */
        Gamut Primaries{ Rec709 };
        DisplayEncoding Encoding{ DisplayEncoding::Invalid };
    };

    /** @brief The lowest paper white and peak that ResolveDisplayTarget returns */
    inline constexpr float k_MinimumPaperWhiteNits = 1.0f;

    /** @brief Returns the best pair of `available` for `preferences`, and falls back toward SDR sRGB. The pair is never
     *  an `_SRGB` format. */
    [[nodiscard]] SurfaceChoice ChooseSurface(const ColorPipePreferences& preferences, std::span<const SurfaceFormatPair> available) noexcept;

    /** @brief Returns the primaries that the composite pass writes for `encoding`. */
    [[nodiscard]] Gamut PrimariesOf(DisplayEncoding encoding) noexcept;

    /** @brief Combines the capabilities of the display, the chosen surface and the overrides. */
    [[nodiscard]] DisplayTarget ResolveDisplayTarget(const DisplayColorCapabilities& capabilities, const SurfaceChoice& surface, const DisplayOverrides& overrides) noexcept;

    /** @brief Returns the peak divided by the paper white: how far above SDR white the display goes. */
    [[nodiscard]] double HeadroomOf(const DisplayTarget& target) noexcept;
}

#endif // !DIAMOND_DOGS_COLOR_PIPE_DISPLAY_TARGET_HPP
