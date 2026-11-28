#pragma once
#ifndef DIAMOND_DOGS_PLANET_LOOM_PRESETS_HPP
#define DIAMOND_DOGS_PLANET_LOOM_PRESETS_HPP

#include "MantlePlumes.hpp"
#include "PlanetShaders.hpp"
#include "ShaderLibrary.host.hpp"
#include "StepState.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace planet_loom
{
    inline constexpr uint32_t k_ShapingFlagSlabDipEnabled      = 1u << 0u;
    inline constexpr uint32_t k_ShapingFlagCoastalNoiseEnabled = 1u << 1u;
    inline constexpr uint32_t k_ShapingFlagAtollShapingEnabled = 1u << 2u;
    inline constexpr uint32_t k_ShapingFlagCustomProfileLut    = 1u << 3u;
    inline constexpr uint32_t k_ShapingFlagErosionEnabled      = 1u << 4u;

    using ShapingParamsHost = planet_gen_shaders::ShapingParams<
        planet_gen_shaders::CookProfile::Spirv,
        ::lodestone::LayoutRule::Std140>;

    enum class PlanetPreset : uint8_t
    {
        Invalid = 0,
        Reference,
        Earth,
        Atoll,
    };

    enum class ShapingPreset : uint8_t
    {
        Invalid = 0,
        Legacy,
        Earth,
        Atoll,
    };

    struct ShapingState
    {
        bool SlabDipEnabled{ false };
        bool CoastalNoiseEnabled{ false };
        bool AtollShapingEnabled{ false };
        bool CustomProfileLut{ false };
        float BaseUplift{ 0.6f };
        float ContinentErosion{ 0.06f };
        // the same decay for ocean relief, whatever its orogeny tag (0 = none, as in the reference)
        float OceanErosion{ 0.0f };
        float AccretionRelief{ 1.5f };
        float AccretionStartRelief{ 0.3f };
        float ContinentalReachKm{ 1800.0f };
        float IslandArcReachKm{ 450.0f };
        float ArcDistanceKm{ 250.0f };
        float CollisionReachKm{ 900.0f };
        float CollisionUpliftScale{ 0.65f };
        float SlabDipAgeWeight{ 0.0f };
        float SlabDipSpeedWeight{ 0.0f };
        float TrenchDepthKm{ 0.0f };
        float CoastalWarpAmplitude{ 0.0f };
        float CoastalDetailKm{ 0.0f };
        float AtollLagoonStrength{ 0.0f };
        float NoiseFrequency{ 6.0f };
        uint32_t NoiseSeed{ 1337u };
        float AtollLandBias{ 0.0f };
        // Erosion.slang: off in every preset; the app turns it on (--erosion), and the parity checks keep it off
        bool ErosionEnabled{ false };
        float ErosionDiffusivity{ 500.0f };
        float CollapseElevationKm{ 5.0f };
        float CollapseTimeMy{ 2.0f };
        // The age-depth curve of the ocean floor (OceanFloorElevationKm in Buffers.slang). The defaults give the
        // reference curve, max(-(2.6 + 0.345 sqrt(age)), -6) km, with no plate-model flattening (OceanPlateDepthKm <= 0)
        // and no sediment.
        float OceanRidgeDepthKm{ 2.6f };
        float OceanSubsidenceKm{ 0.345f };
        float OceanMaxDepthKm{ 6.0f };
        float OceanPlateModelAgeMy{ 0.0f };
        float OceanPlateDepthKm{ 0.0f };
        float OceanPlateRelaxKm{ 0.0f };
        float OceanPlateRelaxPerMy{ 0.0f };
        float OceanSedimentMaxKm{ 0.0f };
        float OceanSedimentTimeMy{ 1.0f };
        std::array<float, 16> ProfileLut{};
    };

    /** Returns canonical preset name for logging and tool replies. */
    [[nodiscard]] std::string_view PlanetPresetName(PlanetPreset preset) noexcept;

    /** Parses "reference", "earth", or "atoll" into PlanetPreset. */
    [[nodiscard]] std::optional<PlanetPreset> ParsePlanetPreset(std::string_view text) noexcept;

    /** Parses "legacy", "earth", or "atoll" into ShapingPreset. */
    [[nodiscard]] std::optional<ShapingPreset> ParseShapingPreset(std::string_view text) noexcept;

    /** Returns bit-exact legacy PlanetLab shaping constants (Flags = 0) for verification parity. */
    [[nodiscard]] ShapingState LegacyShapingState() noexcept;

    /** Returns mild slab-dip + spherical Swiss coastal shaping tuned for Earth-like worlds. */
    [[nodiscard]] ShapingState EarthShapingState(uint32_t seed) noexcept;

    /** Returns archipelago/Atoll World shaping: Cortial slab dip, Swiss coastlines, and Cellular3 lagoons. */
    [[nodiscard]] ShapingState AtollShapingState(uint32_t seed) noexcept;

    /** Returns curated ShapingState for a named ShapingPreset. */
    [[nodiscard]] ShapingState ShapingStateForPreset(ShapingPreset preset, uint32_t seed) noexcept;

    /** Packs host ShapingState into the std140 ShapingConstants GPU uniform layout. */
    [[nodiscard]] ShapingParamsHost PackShapingUniform(const ShapingState& state) noexcept;

    /** Returns curated oceanic hotspots matched to the selected PlanetPreset. */
    [[nodiscard]] PlumeSystemState PresetPlumes(PlanetPreset preset, float initial_time, uint64_t seed);

    /** Generates a procedural nested-HEALPix initial state for Earth or Atoll worlds without a reference dump. */
    [[nodiscard]] std::optional<planet::HealpixInitialState> GenerateProceduralHealpixState(
        PlanetPreset preset,
        uint32_t order,
        uint32_t plate_count,
        uint64_t seed,
        planet::CrustFlagStorage storage);

    /** The most plates the step shaders take: several passes run one wave with one lane for each plate. */
    inline constexpr uint32_t k_MaxPlateSlots = 32u;
    /** Free plate slots that AddRiftSlots gives a world, when the 32-plate limit leaves room. */
    inline constexpr uint32_t k_DefaultRiftSlots = 8u;

    /** @brief Appends up to `spare_slots` empty plates (no crust, an empty cap, an identity rotation) to `state`, and
     *  stops at k_MaxPlateSlots. DecideRiftCS puts the new half of a rifting plate into an empty plate. */
    void AddRiftSlots(planet::HealpixInitialState& state, uint32_t spare_slots, planet::CrustFlagStorage storage);
}

#endif // !DIAMOND_DOGS_PLANET_LOOM_PRESETS_HPP
