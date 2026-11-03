#pragma once
#ifndef DIAMOND_DOGS_OCEAN_ATMOSPHERE_HPP
#define DIAMOND_DOGS_OCEAN_ATMOSPHERE_HPP

#include "ResourceTypes.hpp"
#include "math/Math.hpp"
#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <string>

class ResourceContext;

namespace rhi
{
    struct CommandBuffer;
    class Device;
    class GpuTimerBlock;
    class ShaderLibrary;
}

namespace ocean
{
    /**
     * @brief Physical atmosphere settings based on Bruneton & Neyret (2008).
     * Defines planetary radii, Rayleigh molecular scattering, anisotropic Mie aerosols,
     * stratospheric Ozone absorption (Chappuis band), and multiple scattering iterations.
     */
    struct OceanAtmosphereSettings
    {
        /** @brief Planetary radius at sea level Rg in kilometers. Default 6360.0 km. */
        float BottomRadiusKm{ 6360.0f };
        /** @brief Top-of-atmosphere outer radius Rt in kilometers. Default 6420.0 km. */
        float TopRadiusKm{ 6420.0f };
        /** @brief Rayleigh density scale height HR in kilometers. Default 8.0 km. */
        float RayleighScaleHeightKm{ 8.0f };
        /** @brief Mie aerosol density scale height HM in kilometers. Default 1.2 km. */
        float MieScaleHeightKm{ 1.2f };
        /** @brief Cornette-Shanks Mie phase function asymmetry factor g in [0.7, 0.85]. Default 0.8. */
        float MieAnisotropy{ 0.8f };
        /** @brief Rayleigh scattering coefficient beta_R^s at sea level in km^-1. Default (5.802e-3, 13.558e-3, 33.1e-3). */
        math::Float3 RayleighScattering{ 5.802e-3f, 13.558e-3f, 33.1e-3f };
        /** @brief Mie scattering coefficient beta_M^s at sea level in km^-1. Default (3.996e-3, 3.996e-3, 3.996e-3). */
        math::Float3 MieScattering{ 3.996e-3f, 3.996e-3f, 3.996e-3f };
        /** @brief Total Mie extinction coefficient beta_M^e at sea level in km^-1 (beta_M^s / 0.9). Default 4.440e-3. */
        math::Float3 MieExtinction{ 4.440e-3f, 4.440e-3f, 4.440e-3f };
        /** @brief Stratospheric Ozone absorption coefficient beta_O^a at peak in km^-1. Default (0.650e-3, 1.881e-3, 0.085e-3). */
        math::Float3 OzoneAbsorption{ 0.650e-3f, 1.881e-3f, 0.085e-3f };
        /** @brief Stratospheric Ozone layer peak altitude in kilometers. Default 25.0 km. */
        float OzoneCenterKm{ 25.0f };
        /** @brief Stratospheric Ozone layer half-width in kilometers. Default 15.0 km. */
        float OzoneWidthKm{ 15.0f };
        /** @brief Average ground/ocean Lambertian albedo. Default 0.1. */
        float GroundAlbedo{ 0.1f };
        /** @brief Top-of-atmosphere solar spectral irradiance. Default (18.0, 18.5, 19.5). */
        math::Float3 SolarIrradiance{ 18.0f, 18.5f, 19.5f };
        /** @brief Number of scattering orders to compute (k >= 2). Default 4. */
        uint32_t ScatteringOrders{ 4u };
    };

    struct OceanAtmosphereImpl;

    /**
     * @brief Self-contained Bruneton & Neyret (2008) 4-LUT precomputed atmospheric scattering engine.
     * Manages Transmittance LUT (256x64), Irradiance LUT (64x16), and 4D Inscatter LUT (32x128x256 packed into 3D).
     * Computes order k = 1..4 scattering on GPU compute passes and provides textures for ocean rendering.
     */
    class OceanAtmosphere
    {
    public:
        [[nodiscard]] static std::expected<std::unique_ptr<OceanAtmosphere>, std::string> Create(
            rhi::Device& device,
            ResourceContext& resources,
            const rhi::ShaderLibrary& library,
            const OceanAtmosphereSettings& settings = {});

        ~OceanAtmosphere();

        OceanAtmosphere(const OceanAtmosphere&) = delete;
        OceanAtmosphere& operator=(const OceanAtmosphere&) = delete;

        /**
         * @brief Records all precomputation compute passes onto cmd:
         * 1. AtmosphereTransmittanceCS (256x64)
         * 2. AtmosphereDirectIrradianceCS (64x16)
         * 3. AtmosphereSingleScatteringCS (256x128x32)
         * 4. For k = 2..ScatteringOrders:
         *    - AtmosphereScatteringDensityCS (256x128x32)
         *    - AtmosphereIndirectIrradianceCS (64x16)
         *    - AtmosphereMultipleScatteringCS (256x128x32)
         * Profiled under ScopedGpuTimer "Ocean_AtmoPrecompute".
         */
        void RecordPrecompute(rhi::CommandBuffer& cmd, rhi::GpuTimerBlock* timer_block = nullptr);

        /** @brief Transmittance LUT (256x64 RGBA16F). */
        [[nodiscard]] GraphicsResource TransmittanceLut() const noexcept;
        [[nodiscard]] VkImageView TransmittanceLutView() const noexcept;

        /** @brief Ground/ocean irradiance LUT (64x16 RGBA16F). */
        [[nodiscard]] GraphicsResource IrradianceLut() const noexcept;
        [[nodiscard]] VkImageView IrradianceLutView() const noexcept;

        /** @brief 4D Inscatter LUT packed into 3D (256x128x32 RGBA16F). */
        [[nodiscard]] GraphicsResource InscatterLut() const noexcept;
        [[nodiscard]] VkImageView InscatterLutView() const noexcept;

        /** @brief Linear clamp sampler for atmospheric LUT sampling. */
        [[nodiscard]] GraphicsResource Sampler() const noexcept;
        [[nodiscard]] VkSampler VkSamplerHandle() const noexcept;

        /** @brief Active atmosphere physical settings. */
        [[nodiscard]] const OceanAtmosphereSettings& Settings() const noexcept;

        /** @brief Returns true if RecordPrecompute has been executed. */
        [[nodiscard]] bool IsPrecomputed() const noexcept;

    private:
        explicit OceanAtmosphere(std::unique_ptr<OceanAtmosphereImpl>&& impl) noexcept;

        std::unique_ptr<OceanAtmosphereImpl> impl;
    };
}

#endif //!DIAMOND_DOGS_OCEAN_ATMOSPHERE_HPP
