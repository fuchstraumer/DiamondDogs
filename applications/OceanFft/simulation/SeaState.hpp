#pragma once
#ifndef DIAMOND_DOGS_OCEAN_SEA_STATE_HPP
#define DIAMOND_DOGS_OCEAN_SEA_STATE_HPP
#include "OceanSimulation.hpp"
#include <array>
#include <cstdint>
#include <string_view>

// The editor's domain for the ocean, and the pipe of transforms that turns it into the physics domain
// (SpectrumParameters). An artist sets what they see: how windy it is, how open the water is, how high and long the
// swell is. The pipe works out the wind speeds, fetches and peaking factors the spectrum needs, and measures what came
// out (wave heights, periods, lengths), so the editor can show the result in units the artist can check.
//
//   SeaState -> ResolveWind -> ResolveSwell -> ResolveSpreading -> BuildSpectrum -> MeasureSpectrum
//
// Each stage is a small pure function on plain structs, so each one can be tested and replaced on its own.
namespace ocean
{
    /** @brief The editor domain. Every value is in units an artist can judge by eye */
    struct SeaState
    {
        /** @brief Beaufort force, 0 (calm) to 12 (hurricane), continuous */
        float WindForce{ 5.0f };
        /** @brief 0: sheltered water, the wind has 5 km to work with. 1: open ocean, a fully developed sea */
        float Openness{ 0.6f };
        /** @brief Where the wind sea travels towards, in degrees */
        float WindDirection{ 0.0f };
        /** @brief 0: waves line up with the wind. 1: a confused sea, waves from many directions */
        float Spread{ 0.2f };
        /** @brief 0: a confused, broad-banded sea. 1: clean, regular wave groups (JONSWAP gamma 1 to 7) */
        float Regularity{ 0.4f };
        /** @brief 0: the short ripples are smoothed away. 1: every ripple down to the finest cascade */
        float RippleDetail{ 0.9f };
        /** @brief Water depth in meters. Shallow water slows and steepens the long waves */
        float Depth{ 50.0f };

        /** @brief Significant height of the swell alone, in meters. 0 turns the swell off */
        float SwellHeight{ 1.0f };
        /** @brief Peak period of the swell, in seconds (8: short swell, 16: long ocean swell) */
        float SwellPeriod{ 11.0f };
        /** @brief Where the swell travels towards, in degrees */
        float SwellDirection{ 40.0f };
        /** @brief 0: a loose swell, mixed directions and periods. 1: long, parallel, regular crests */
        float SwellOrder{ 0.5f };

        /** @brief The artist's multiplier on every wave height. 1 is physical */
        float HeightScale{ 1.0f };
        /** @brief User-defined threshold for whitecap formation, in the range 0-1. Maps to [-0.2, 0.2] in shader */
        float WhitecapThreshold{ 0.5f };
        /** @brief Linear foam coverage multiplier. 0: none, 1: physical reference, 2+: heavy foam */
        float FoamAmount{ 1.0f };
        /** @brief Footprint dispersion factor. <1 creates tight crest lines, >1 diffuses foam across wave faces */
        float FoamSpread{ 1.0f };
        /** @brief Contrast/falloff exponent. >1 suppresses fringes for punchy cores, <1 broadens milky froth */
        float FoamContrast{ 1.0f };
    };

    /** @brief ResolveWind's output: the wind sea in the physics domain */
    struct WindSeaPhysics
    {
        float WindSpeed{ 0.0f };      // m/s, 10 m above the water
        float Fetch{ 0.0f };          // km
        float PeakingFactor{ 3.3f };  // JONSWAP gamma
        float FadeLength{ 0.0f };     // m
    };

    /** @brief ResolveSwell's output: the swell as a second JONSWAP, from a distant wind that would raise this period */
    struct SwellPhysics
    {
        float WindSpeed{ 0.0f };      // m/s
        float Fetch{ 0.0f };          // km
        float PeakingFactor{ 7.0f };
        float Spread{ 2.0f };
        float FadeLength{ 1.0f };     // m
        float TargetHeight{ 0.0f };   // m, significant height; BuildSpectrum scales the swell's energy to it
    };

    /** @brief ResolveSpreading's output */
    struct SpreadingPhysics
    {
        float Alignment{ 1.0f };
        float WindSeaNarrowness{ 0.2f };
    };

    /** @brief MeasureSpectrum's output: what the spectrum will look like, for the editor to show */
    struct SeaStateReadout
    {
        float SignificantHeight{ 0.0f };  // m, wind sea and swell together, with HeightScale
        float WindSeaHeight{ 0.0f };      // m
        float SwellHeight{ 0.0f };        // m
        float PeakPeriod{ 0.0f };         // s, of the wind sea
        float PeakWavelength{ 0.0f };     // m, of the wind sea (deep water)
        float SwellWavelength{ 0.0f };    // m (deep water)
        float WindSpeed{ 0.0f };          // m/s
        std::string_view BeaufortName;
    };

    [[nodiscard]] WindSeaPhysics ResolveWind(const SeaState& state) noexcept;
    [[nodiscard]] SwellPhysics ResolveSwell(const SeaState& state) noexcept;
    [[nodiscard]] SpreadingPhysics ResolveSpreading(const SeaState& state) noexcept;
    /** @brief Combines the stages into SpectrumParameters. Keeps the fields the sea state doesn't own (seed, spreading
     *  model) from base. Sets SwellScale so the swell's measured height is SwellPhysics::TargetHeight */
    [[nodiscard]] SpectrumParameters BuildSpectrum(const SpectrumParameters& base, const WindSeaPhysics& wind, const SwellPhysics& swell,
                                                   const SpreadingPhysics& spreading, float height_scale, float depth) noexcept;
    /** @brief Integrates the spectrum's frequency part on the CPU, the same way SpectrumInitCS evaluates it (the
     *  spreading integrates to 1). Cascade band limits are ignored, so it reads slightly high for very long waves */
    [[nodiscard]] SeaStateReadout MeasureSpectrum(const SpectrumParameters& spectrum) noexcept;

    /** @brief The whole pipe: the sea state to the spectrum, plus what it measures */
    struct SeaStateResult
    {
        SpectrumParameters Spectrum{};
        SeaStateReadout Readout{};
    };

    [[nodiscard]] SeaStateResult ApplySeaState(const SeaState& state, const SpectrumParameters& base) noexcept;

    /** @brief Named starting points for the editor */
    struct SeaStatePreset
    {
        std::string_view Name;
        SeaState State;
    };

    [[nodiscard]] const std::array<SeaStatePreset, 6>& SeaStatePresets() noexcept;
}

#endif //!DIAMOND_DOGS_OCEAN_SEA_STATE_HPP
