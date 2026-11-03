#pragma once
#ifndef DIAMOND_DOGS_OCEAN_ENVIRONMENT_HPP
#define DIAMOND_DOGS_OCEAN_ENVIRONMENT_HPP

#include "ExrLoader.hpp"
#include "math/Math.hpp"
#include "ResourceTypes.hpp"
#include <vulkan/vulkan_core.h>

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

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
    /** @brief 6 available ocean sky environment presets. */
    enum class OceanSkyPreset : uint32_t
    {
        AnalyticPreetham = 0,
        AuroralNight = 1,
        BrightDay = 2,
        StarryNight = 3,
        OvercastEvening = 4,
        Twilight = 5
    };

    /** @brief Returns asset relative path for an HDRI preset (empty for AnalyticPreetham). */
    [[nodiscard]] constexpr std::string_view SkyPresetRelativePath(OceanSkyPreset preset) noexcept
    {
        switch (preset)
        {
        case OceanSkyPreset::AuroralNight:
            return "textures/hdri/AuroralSky/NightSkyHDRI007_16K_HDR.exr";
        case OceanSkyPreset::BrightDay:
            return "textures/hdri/DaySky/DaySkyHDRI071B_16K_HDR.exr";
        case OceanSkyPreset::StarryNight:
            return "textures/hdri/NightSky/NightSkyHDRI002_16K_HDR.exr";
        case OceanSkyPreset::OvercastEvening:
            return "textures/hdri/OvercastEveningSky/EveningSkyHDRI045B_16K_HDR.exr";
        case OceanSkyPreset::Twilight:
            return "textures/hdri/TwilightSky/EveningSkyHDRI044B_16K_HDR.exr";
        default:
            return "";
        }
    }

    /** @brief Returns human-readable display name for preset. */
    [[nodiscard]] constexpr std::string_view SkyPresetName(OceanSkyPreset preset) noexcept
    {
        switch (preset)
        {
        case OceanSkyPreset::AnalyticPreetham: return "Analytic Preetham";
        case OceanSkyPreset::AuroralNight:    return "Auroral Night";
        case OceanSkyPreset::BrightDay:       return "Bright Day";
        case OceanSkyPreset::StarryNight:     return "Starry Night";
        case OceanSkyPreset::OvercastEvening: return "Overcast Evening";
        case OceanSkyPreset::Twilight:        return "Twilight";
        default:                              return "Unknown";
        }
    }

    struct OceanEnvironmentSettings
    {
        OceanSkyPreset Preset{ OceanSkyPreset::BrightDay };
        /** @brief Box-downsample EXR to 8K (8192x4096) on CPU. If false, loads full 16K (16384x8192). */
        bool Downsample8K{ true };
        /** @brief Atmospheric turbidity for Analytic Preetham [1.0, 10.0]. Default 2.5. */
        float PreethamTurbidity{ 2.5f };
        /** @brief Direction vector towards the sun (+Y is up). */
        std::array<float, 3> SunDirection{ 0.3f, 0.7f, -0.4f };
        /** @brief Global exposure multiplier for sky radiance. */
        float Exposure{ 1.0f };
        /** @brief MSAA sample count for fullscreen sky background rendering. */
        VkSampleCountFlagBits Samples{ VK_SAMPLE_COUNT_1_BIT };
    };

    struct OceanEnvironmentImpl;

    /**
     * @brief Manages 16K/8K OpenEXR HDRI skydomes, Analytic Preetham sky synthesis,
     * GPU equirectangular mip chain generation, L2 SH irradiance, and fullscreen sky background rendering.
     */
    class OceanEnvironment
    {
    public:
        [[nodiscard]] static std::expected<std::unique_ptr<OceanEnvironment>, std::string> Create(
            rhi::Device& device,
            ResourceContext& resources,
            const rhi::ShaderLibrary& library,
            const OceanEnvironmentSettings& settings = {});

        ~OceanEnvironment();

        OceanEnvironment(const OceanEnvironment&) = delete;
        OceanEnvironment& operator=(const OceanEnvironment&) = delete;

        /** @brief Switch active preset. Loads EXR if needed and schedules GPU mip generation. */
        [[nodiscard]] bool SetPreset(OceanSkyPreset preset);

        /** @brief Toggles 8K downsampling vs full 16K. Reloads current preset if HDRI. */
        void SetDownsample8K(bool downsample8k);

        /** @brief Updates Analytic Preetham parameters. */
        void SetPreethamParameters(float turbidity, const std::array<float, 3>& sun_dir, float exposure = 1.0f);

        /** @brief Sets exposure multiplier. */
        void SetExposure(float exposure) noexcept;

        /** @brief Sets MSAA sample count for sky background draw. */
        void SetSampleCount(VkSampleCountFlagBits samples) noexcept;

        /** @brief Currently active preset. */
        [[nodiscard]] OceanSkyPreset CurrentPreset() const noexcept;

        /** @brief Whether 8K downsampling is enabled (true = 8K, false = 16K). */
        [[nodiscard]] bool IsDownsample8K() const noexcept;

        /** @brief GPU image resource for equirectangular skydome map. */
        [[nodiscard]] GraphicsResource Image() const noexcept;

        /** @brief Full equirectangular texture view covering all mip levels (VkImageView). */
        [[nodiscard]] VkImageView TextureView() const noexcept;

        /** @brief Skydome bilinear/trilinear sampler resource. */
        [[nodiscard]] GraphicsResource Sampler() const noexcept;
        [[nodiscard]] VkSampler VkSamplerHandle() const noexcept;

        /** @brief Pre-convolved L2 spherical harmonics diffuse irradiance coefficients in AP1 linear space (9 float4). */
        [[nodiscard]] const SphericalHarmonicsL2& SphericalHarmonics() const noexcept;

        /** @brief Dominant directional light unit direction in the upper hemisphere (+Y is up). */
        [[nodiscard]] std::array<float, 3> DominantLightDirection() const noexcept;

        /** @brief Dominant directional light radiance in AP1 linear space. */
        [[nodiscard]] std::array<float, 3> DominantLightRadiance() const noexcept;

        /** @brief Evaluates diffuse irradiance from L2 spherical harmonics for a given normal. */
        [[nodiscard]] std::array<float, 3> EvaluateIrradiance(const std::array<float, 3>& normal) const noexcept;

        /**
         * @brief Records GPU mip chain generation (OceanEnvDownsampleMipCS) on the command buffer.
         * Scoped with ScopedGpuTimer "Env mipgen".
         */
        void RecordMipGen(rhi::CommandBuffer& cmd, rhi::GpuTimerBlock* timer_block = nullptr);

        /**
         * @brief Records fullscreen sky background triangle draw into current render pass.
         * Samples equirectangular skydome along world view ray and outputs AP1 linear radiance.
         * Scoped with ScopedGpuTimer "Sky background".
         */
        void RecordSkyBackground(
            rhi::CommandBuffer& cmd,
            rhi::GpuTimerBlock* timer_block,
            const math::Float4x4& inv_view_proj,
            const math::Float3& camera_pos,
            float lod = 0.0f,
            std::optional<VkSampleCountFlagBits> sample_count_override = std::nullopt);

        void RecordSkyBackground(
            rhi::CommandBuffer& cmd,
            rhi::GpuTimerBlock* timer_block,
            const std::array<std::array<float, 4>, 4>& inv_view_proj,
            const std::array<float, 3>& camera_pos,
            float lod = 0.0f,
            std::optional<VkSampleCountFlagBits> sample_count_override = std::nullopt)
        {
            RecordSkyBackground(cmd, timer_block, math::Float4x4{ inv_view_proj }, math::Float3{ camera_pos }, lod, sample_count_override);
        }

    private:
        explicit OceanEnvironment(std::unique_ptr<OceanEnvironmentImpl>&& impl) noexcept;

        std::unique_ptr<OceanEnvironmentImpl> impl;
    };
}

#endif //!DIAMOND_DOGS_OCEAN_ENVIRONMENT_HPP
