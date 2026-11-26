#pragma once
#ifndef DIAMOND_DOGS_PLANET_LOOM_MANTLE_PLUMES_HPP
#define DIAMOND_DOGS_PLANET_LOOM_MANTLE_PLUMES_HPP

#include "ShaderLibrary.host.hpp"
#include <cstdint>
#include <vector>

namespace planet_loom
{
    inline constexpr uint32_t k_MaxMantlePlumes = 64u;
    inline constexpr uint32_t k_PlumeFlagEnabled = 1u << 0u;
    inline constexpr uint32_t k_PlumeFlagDriftEnabled = 1u << 1u;

    using PlumeParamsHost = planet_gen_shaders::PlumeParams<
        planet_gen_shaders::CookProfile::Spirv,
        ::lodestone::LayoutRule::Std140>;

    struct MantlePlumeDesc
    {
        float LatDeg{ 0.0f };
        float LonDeg{ 0.0f };
        float ConduitRadiusKm{ 95.0f };
        float SwellRadiusKm{ 500.0f };
        float PeakSwellKm{ 1.3f };
        float BuoyancyFlux{ 6.0f };
        float VolcanicUpliftRate{ 2.0f };
        float DriftAzimuthDeg{ 90.0f };
        float DriftSpeedKmPerMy{ 12.0f };
        float BirthTimeMy{ 0.0f };
        float LifetimeMy{ 0.0f };
        float PulsePeriodMy{ 12.0f };
        float PulseAmplitude{ 0.45f };
    };

    struct PlumeSystemState
    {
        bool Enabled{ true };
        bool DriftEnabled{ true };
        float GlobalStrength{ 1.0f };
        float OceanicErosionRate{ 0.015f };
        float SwellScale{ 1.0f };
        float AccretionThresholdKm{ 0.0f };
        std::vector<MantlePlumeDesc> Plumes;
    };

    /** Returns 4 curated hotspot plumes matching the reference planet basins for volcanic chains & calderas. */
    [[nodiscard]] PlumeSystemState DefaultPresetPlumes(float initial_time);

    /** Returns an empty plume state (PlumeCount = 0) so parity verification matches legacy PlanetStep bit-for-bit. */
    [[nodiscard]] PlumeSystemState EmptyPlumeSystem();

    /** Packs authored lat/lon & physical parameters into the Std140 GPU uniform buffer layout. */
    [[nodiscard]] PlumeParamsHost PackPlumeUniform(const PlumeSystemState& state, float time_my);
}

#endif // !DIAMOND_DOGS_PLANET_LOOM_MANTLE_PLUMES_HPP
