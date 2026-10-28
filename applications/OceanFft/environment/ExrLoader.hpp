#pragma once
#ifndef DIAMOND_DOGS_OCEAN_EXR_LOADER_HPP
#define DIAMOND_DOGS_OCEAN_EXR_LOADER_HPP

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace ocean
{
    /**
     * @brief 9 float4 L2 spherical harmonics coefficients in AP1 linear color space.
     * Index 0: l=0, m=0
     * Indices 1-3: l=1 (m=-1, 0, 1)
     * Indices 4-8: l=2 (m=-2, -1, 0, 1, 2)
     * Pre-convolved with the cosine lobe (A_l factors: pi, 2pi/3, pi/4) for direct diffuse irradiance evaluation.
     */
    using SphericalHarmonicsL2 = std::array<std::array<float, 4>, 9>;

    struct ExrLoadOptions
    {
        /** @brief Box-downsample 2x2 on CPU when EXR width exceeds this limit (default 8192, or 16384 for full). */
        uint32_t TargetMaxWidth{ 8192u };
    };

    struct ExrImageResult
    {
        uint32_t Width{ 0u };
        uint32_t Height{ 0u };
        /**
         * @brief Texels in RGBA16_Float (IEEE-754 half-precision float) in AP1 linear space.
         * Size in bytes = Width * Height * 4 * sizeof(uint16_t).
         */
        std::vector<uint16_t> PixelsFp16;
        /** @brief 9 float4 L2 spherical harmonics diffuse irradiance coefficients in AP1 linear space. */
        SphericalHarmonicsL2 SphericalHarmonics{};
        /** @brief Dominant directional light unit direction in the upper hemisphere (+Y is up). */
        std::array<float, 3> DominantLightDirection{ 0.0f, 1.0f, 0.0f };
        /** @brief AP1 linear radiance / illuminance of dominant directional light in the upper hemisphere. */
        std::array<float, 3> DominantLightRadiance{ 0.0f, 0.0f, 0.0f };
    };

    /**
     * @brief Evaluates diffuse irradiance from L2 spherical harmonics for a given surface normal.
     * @param sh 9 pre-convolved float4 SH coefficients.
     * @param normal Unit surface normal in world space (+Y up).
     * @return AP1 linear diffuse irradiance (RGB).
     */
    [[nodiscard]] std::array<float, 3> EvaluateShIrradiance(
        const SphericalHarmonicsL2& sh,
        const std::array<float, 3>& normal) noexcept;

    /**
     * @brief Loads an OpenEXR equirectangular skydome from disk using tinyexr and basisu_miniz.
     * Optionally box-downsamples 2x2 when width > TargetMaxWidth.
     * Converts linear Rec.709 RGB texels to AP1 linear RGB using Bradford chromatic adaptation.
     * Converts texels to IEEE-754 float16 (RGBA16_Float).
     * Computes CPU L2 spherical harmonics (9 float4 in AP1) and upper-hemisphere dominant directional light.
     */
    [[nodiscard]] std::expected<ExrImageResult, std::string> LoadExrEnvironmentMap(
        const std::filesystem::path& path,
        const ExrLoadOptions& options = {});
}

#endif //!DIAMOND_DOGS_OCEAN_EXR_LOADER_HPP
