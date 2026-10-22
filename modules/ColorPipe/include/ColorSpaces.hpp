#pragma once
#ifndef DIAMOND_DOGS_COLOR_PIPE_COLOR_SPACES_HPP
#define DIAMOND_DOGS_COLOR_PIPE_COLOR_SPACES_HPP
#include <array>
#include <cstdint>

namespace color
{
    /** @brief A color or a position in a three-component space, in double precision. The CPU reference of the
     *  color pipe uses this in place of a graphics math type. */
    struct Vec3d
    {
        double X{ 0.0 };
        double Y{ 0.0 };
        double Z{ 0.0 };
    };

    /** @brief A row-major 3x3 matrix in double precision. It transforms a column vector: `Multiply(matrix, vector)`
     *  computes `matrix * vector`. The Slang constants are the transpose of these matrices. */
    struct Mat3d
    {
        std::array<std::array<double, 3>, 3> Rows{};
    };

    /** @brief A CIE 1931 xy chromaticity coordinate. */
    struct Xy
    {
        double X;
        double Y;
    };

    /** @brief The three primaries and the white point of an RGB color space, as xy chromaticities. */
    struct Gamut
    {
        Xy Red;
        Xy Green;
        Xy Blue;
        Xy White;
    };

    enum class ChromaticAdaptation : uint8_t
    {
        Invalid = 0,
        None,
        Bradford,
        Cat02,
    };

    inline constexpr Xy WhiteD65{ 0.3127, 0.3290 };
    inline constexpr Xy WhiteAces{ 0.32168, 0.33767 };

    /** @brief Rec.709 primaries, which sRGB shares. */
    inline constexpr Gamut Rec709{ { 0.640, 0.330 }, { 0.300, 0.600 }, { 0.150, 0.060 }, WhiteD65 };
    inline constexpr Gamut Rec2020{ { 0.708, 0.292 }, { 0.170, 0.797 }, { 0.131, 0.046 }, WhiteD65 };
    /** @brief The AP1 primaries of ACEScg. The white point is the ACES white, not D65. */
    inline constexpr Gamut AP1{ { 0.713, 0.293 }, { 0.165, 0.830 }, { 0.128, 0.044 }, WhiteAces };
    inline constexpr Gamut DisplayP3{ { 0.680, 0.320 }, { 0.265, 0.690 }, { 0.150, 0.060 }, WhiteD65 };

    /** @brief The published luminance weights of AP1. `Luminance(AP1, rgb)` derives the same values from the
     *  chromaticities. */
    inline constexpr Vec3d Ap1LuminanceWeights{ 0.2722287, 0.6740818, 0.0536895 };

    inline constexpr double KelvinMinimum = 1000.0;
    inline constexpr double KelvinMaximum = 15000.0;

    [[nodiscard]] Vec3d Multiply(const Mat3d& matrix, const Vec3d& vector) noexcept;
    [[nodiscard]] Mat3d Multiply(const Mat3d& left, const Mat3d& right) noexcept;
    [[nodiscard]] Mat3d Transpose(const Mat3d& matrix) noexcept;
    [[nodiscard]] Mat3d Inverse(const Mat3d& matrix) noexcept;
    [[nodiscard]] Mat3d Identity() noexcept;

    /** @brief Returns the XYZ of a chromaticity at Y = 1. */
    [[nodiscard]] Vec3d XyToXyz(Xy chromaticity) noexcept;

    /** @brief Returns the matrix from linear RGB of `gamut` to XYZ. It is derived from the chromaticities, and it
     *  maps (1, 1, 1) to the white point at Y = 1. */
    [[nodiscard]] Mat3d RgbToXyz(const Gamut& gamut) noexcept;
    [[nodiscard]] Mat3d XyzToRgb(const Gamut& gamut) noexcept;

    /** @brief Returns the matrix that adapts XYZ under `source_white` to XYZ under `destination_white`. An Invalid
     *  adaptation returns a matrix of NaN, so that the mistake is visible. */
    [[nodiscard]] Mat3d AdaptationMatrix(Xy source_white, Xy destination_white, ChromaticAdaptation adaptation) noexcept;

    /** @brief Returns the matrix from linear RGB of `source` to linear RGB of `destination`, through XYZ. The
     *  adaptation runs between the two white points. */
    [[nodiscard]] Mat3d RgbToRgb(const Gamut& source, const Gamut& destination, ChromaticAdaptation adaptation) noexcept;

    /** @brief Returns the Y row of RgbToXyz(gamut), applied to `rgb`. */
    [[nodiscard]] double Luminance(const Gamut& gamut, const Vec3d& rgb) noexcept;

    /** @brief Returns the chromaticity of a blackbody at `kelvin`, from the Krystek (1985) approximation of the
     *  Planckian locus in CIE 1960 uv. Valid from 1000 K to 15000 K: `kelvin` is clamped to that range. */
    [[nodiscard]] Xy KelvinToXy(double kelvin) noexcept;

    /** @brief Returns the linear RGB of a blackbody in `gamut`, normalized to Y = 1. There is no adaptation: the
     *  XYZ is the blackbody's own. */
    [[nodiscard]] Vec3d KelvinToRgb(const Gamut& gamut, double kelvin) noexcept;
}

#endif // !DIAMOND_DOGS_COLOR_PIPE_COLOR_SPACES_HPP
