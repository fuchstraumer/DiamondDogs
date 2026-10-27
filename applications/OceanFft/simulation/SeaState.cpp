#include "SeaState.hpp"
#include "OceanSimulation.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <string_view>

namespace ocean
{
    namespace
    {
        constexpr float k_G = 9.81f;
        constexpr float k_TwoPi = 2.0f * std::numbers::pi_v<float>;

        // the dimensionless fetch g F / U^2 at which JONSWAP's peak (3.5 chi^-0.33 g / U) reaches Pierson-Moskowitz's
        // (0.13 g / U): past it the sea is fully developed, and more fetch changes little
        constexpr float k_FullyDevelopedFetch = 2.17e4f;
        constexpr float k_ShelteredFetchKm = 5.0f;

        constexpr std::array<std::string_view, 13> k_BeaufortNames
        {
            "calm", "light air", "light breeze", "gentle breeze", "moderate breeze", "fresh breeze", "strong breeze",
            "near gale", "gale", "strong gale", "storm", "violent storm", "hurricane"
        };

        // Beaufort's own scale: U = 0.836 B^(3/2) m/s
        float BeaufortToWindSpeed(float force) noexcept
        {
            return std::max(0.836f * std::pow(std::max(force, 0.0f), 1.5f), 0.5f);
        }

        float WindSpeedToBeaufort(float wind_speed) noexcept
        {
            return std::pow(wind_speed / 0.836f, 2.0f / 3.0f);
        }

        float FullyDevelopedFetchKm(float wind_speed) noexcept
        {
            return k_FullyDevelopedFetch * wind_speed * wind_speed / k_G / 1000.0f;
        }

        float DimensionlessFetch(float wind_speed, float fetch_km) noexcept
        {
            // the same clamp as OceanEnergySpectrum
            return std::min(k_G * fetch_km * 1000.0f / (wind_speed * wind_speed), 1.0e6f);
        }

        float JonswapPeakOmega(float wind_speed, float fetch_km) noexcept
        {
            return k_TwoPi * 3.5f * (k_G / wind_speed) * std::pow(DimensionlessFetch(wind_speed, fetch_km), -0.33f);
        }

        /** The variance (m0) of one JONSWAP component as SpectrumInitCS evaluates it: alpha-beta base, peak sharpening,
         *  the smoothstep depth correction, and the short-wave fade (deep-water k = w^2 / g) */
        float JonswapVariance(float wind_speed, float fetch_km, float gamma, float fade_length, float depth) noexcept
        {
            const float chi = DimensionlessFetch(wind_speed, fetch_km);
            const float alpha = 0.076f * std::pow(chi, -0.22f);
            const float peakOmega = JonswapPeakOmega(wind_speed, fetch_km);

            // trapezoid in log(omega), from 1/50 to 30 times the peak: enough for every term above
            constexpr int k_Samples = 4096;
            const double logLow = std::log(0.02 * peakOmega);
            const double logHigh = std::log(30.0 * peakOmega);
            const double step = (logHigh - logLow) / (k_Samples - 1);
            double variance = 0.0;
            double previous = 0.0;
            for (int i = 0; i < k_Samples; ++i)
            {
                const double omega = std::exp(logLow + step * i);
                const double sigma = omega <= peakOmega ? 0.07 : 0.09;
                const double peakDistance = (omega - peakOmega) / (sigma * peakOmega);
                const double sharpening = std::pow(static_cast<double>(gamma), std::exp(-0.5 * peakDistance * peakDistance));
                const double ratio = peakOmega / omega;
                const double base = alpha * k_G * k_G / std::pow(omega, 5.0) * std::exp(-1.25 * ratio * ratio * ratio * ratio);

                const double omegaH = std::clamp(omega * std::sqrt(depth / k_G) / 2.0, 0.0, 1.0);
                const double depthCorrection = omegaH * omegaH * (3.0 - 2.0 * omegaH);
                const double k = omega * omega / k_G;
                const double fade = std::exp(-static_cast<double>(fade_length) * fade_length * k * k);

                // d omega = omega d(log omega)
                const double integrand = sharpening * base * depthCorrection * fade * omega;
                if (i > 0)
                {
                    variance += 0.5 * (previous + integrand) * step;
                }

                previous = integrand;
            }

            return static_cast<float>(variance);
        }

        float DeepWaterWavelength(float omega) noexcept
        {
            return k_TwoPi * k_G / (omega * omega);
        }
    }

    WindSeaPhysics ResolveWind(const SeaState& state) noexcept
    {
        WindSeaPhysics wind{};
        wind.WindSpeed = BeaufortToWindSpeed(state.WindForce);

        // openness walks the fetch from sheltered to fully developed, evenly in log(fetch): each step looks about as
        // large as the last. A light wind is fully developed within the sheltered fetch already
        const float fullyDeveloped = FullyDevelopedFetchKm(wind.WindSpeed);
        const float sheltered = std::min(k_ShelteredFetchKm, fullyDeveloped);
        const float openness = std::clamp(state.Openness, 0.0f, 1.0f);
        wind.Fetch = std::exp(std::lerp(std::log(sheltered), std::log(fullyDeveloped), openness));

        wind.PeakingFactor = 1.0f + 6.0f * std::clamp(state.Regularity, 0.0f, 1.0f);
        wind.FadeLength = 0.1f * (1.0f - std::clamp(state.RippleDetail, 0.0f, 1.0f));
        return wind;
    }

    SwellPhysics ResolveSwell(const SeaState& state) noexcept
    {
        SwellPhysics swell{};
        swell.TargetHeight = std::max(state.SwellHeight, 0.0f);

        // a swell is the fully developed sea of a distant wind. Invert JONSWAP's peak for that wind:
        // 2 pi / T = 2 pi 3.5 (g / U) chi^-0.33, so U = 3.5 g T chi^-0.33
        const float period = std::max(state.SwellPeriod, 2.0f);
        swell.WindSpeed = 3.5f * k_G * period * std::pow(k_FullyDevelopedFetch, -0.33f);
        swell.Fetch = FullyDevelopedFetchKm(swell.WindSpeed);

        // order narrows both the band (gamma) and the directions (the swell shaping term)
        const float order = std::clamp(state.SwellOrder, 0.0f, 1.0f);
        swell.PeakingFactor = 1.0f + 9.0f * order;
        swell.Spread = 4.0f * order;

        // a swell carries no short waves: fade out those under about a tenth of its length
        swell.FadeLength = DeepWaterWavelength(k_TwoPi / period) / 100.0f;
        return swell;
    }

    SpreadingPhysics ResolveSpreading(const SeaState& state) noexcept
    {
        // never fully isotropic: even a confused sea keeps some memory of the wind
        const float spread = std::clamp(state.Spread, 0.0f, 1.0f);
        SpreadingPhysics spreading{};
        spreading.Alignment = 1.0f - 0.85f * spread;
        spreading.WindSeaNarrowness = 0.4f * (1.0f - spread);
        return spreading;
    }

    SpectrumParameters BuildSpectrum(const SpectrumParameters& base, const WindSeaPhysics& wind, const SwellPhysics& swell,
                                     const SpreadingPhysics& spreading, float height_scale, float depth) noexcept
    {
        constexpr float k_DegreesToRadians = std::numbers::pi_v<float> / 180.0f;

        SpectrumParameters spectrum = base;
        spectrum.WindSpeed = wind.WindSpeed;
        spectrum.Fetch = wind.Fetch;
        spectrum.PeakingFactor = wind.PeakingFactor;
        spectrum.FadeLength = wind.FadeLength;
        spectrum.Depth = depth;
        spectrum.Alignment = spreading.Alignment;
        spectrum.SwellAmount = spreading.WindSeaNarrowness;
        spectrum.WaveHeightScale = std::max(height_scale, 0.0f);

        spectrum.SwellWindSpeed = swell.WindSpeed;
        spectrum.SwellFetch = swell.Fetch;
        spectrum.SwellPeakingFactor = swell.PeakingFactor;
        spectrum.SwellSpread = swell.Spread;
        spectrum.SwellFadeLength = swell.FadeLength;

        // the swell's energy scale, so its significant height (4 sqrt(m0)) is the one asked for
        spectrum.SwellScale = 0.0f;
        if (swell.TargetHeight > 0.0f)
        {
            const float variance = JonswapVariance(swell.WindSpeed, swell.Fetch, swell.PeakingFactor, swell.FadeLength, depth);
            const float targetVariance = swell.TargetHeight * swell.TargetHeight / 16.0f;
            spectrum.SwellScale = variance > 0.0f ? targetVariance / variance : 0.0f;
        }

        return spectrum;
    }

    SeaStateReadout MeasureSpectrum(const SpectrumParameters& spectrum) noexcept
    {
        const float windVariance = JonswapVariance(spectrum.WindSpeed, spectrum.Fetch, spectrum.PeakingFactor, spectrum.FadeLength, spectrum.Depth);
        const float swellVariance = spectrum.SwellScale > 0.0f ?
            spectrum.SwellScale * JonswapVariance(spectrum.SwellWindSpeed, spectrum.SwellFetch, spectrum.SwellPeakingFactor, spectrum.SwellFadeLength, spectrum.Depth) :
            0.0f;

        // heights scale with the amplitude, variances with its square
        const float scale = spectrum.WaveHeightScale;
        SeaStateReadout readout{};
        readout.SignificantHeight = 4.0f * std::sqrt(windVariance + swellVariance) * scale;
        readout.WindSeaHeight = 4.0f * std::sqrt(windVariance) * scale;
        readout.SwellHeight = 4.0f * std::sqrt(swellVariance) * scale;

        const float peakOmega = JonswapPeakOmega(spectrum.WindSpeed, spectrum.Fetch);
        readout.PeakPeriod = k_TwoPi / peakOmega;
        readout.PeakWavelength = DeepWaterWavelength(peakOmega);
        readout.SwellWavelength = DeepWaterWavelength(JonswapPeakOmega(spectrum.SwellWindSpeed, spectrum.SwellFetch));
        readout.WindSpeed = spectrum.WindSpeed;

        const int force = static_cast<int>(std::lround(WindSpeedToBeaufort(spectrum.WindSpeed)));
        readout.BeaufortName = k_BeaufortNames[static_cast<size_t>(std::clamp(force, 0, 12))];
        return readout;
    }

    SeaStateResult ApplySeaState(const SeaState& state, const SpectrumParameters& base) noexcept
    {
        const WindSeaPhysics wind = ResolveWind(state);
        const SwellPhysics swell = ResolveSwell(state);
        const SpreadingPhysics spreading = ResolveSpreading(state);

        SeaStateResult result{};
        result.Spectrum = BuildSpectrum(base, wind, swell, spreading, state.HeightScale, state.Depth);
        result.Spectrum.LocalWindDirection = state.WindDirection * std::numbers::pi_v<float> / 180.0f;
        result.Spectrum.SwellDirection = state.SwellDirection * std::numbers::pi_v<float> / 180.0f;
        result.Readout = MeasureSpectrum(result.Spectrum);
        return result;
    }

    const std::array<SeaStatePreset, 6>& SeaStatePresets() noexcept
    {
        static const std::array<SeaStatePreset, 6> k_Presets
        {
            SeaStatePreset
            { 
                "Glassy",
                SeaState
                {
                    .WindForce=1.0f,
                    .Openness=0.3f,
                    .WindDirection=0.0f,
                    .Spread=0.4f,
                    .Regularity=0.2f,
                    .RippleDetail=0.6f,
                    .Depth=50.0f,
                    .SwellHeight=0.3f,
                    .SwellPeriod=9.0f,
                    .SwellDirection=30.0f,
                    .SwellOrder=0.6f,
                    .HeightScale=1.0f,
                    .WhitecapThreshold=0.5f,
                    .FoamAmount=1.0f,
                    .CrestSharpness=0.2f
                }
            },
            SeaStatePreset
            { 
                "Breezy",
                SeaState
                {
                    .WindForce=3.0f,
                    .Openness=0.5f,
                    .WindDirection=0.0f,
                    .Spread=0.3f,
                    .Regularity=0.3f,
                    .RippleDetail=0.9f,
                    .Depth=50.0f,
                    .SwellHeight=0.6f,
                    .SwellPeriod=10.0f,
                    .SwellDirection=40.0f,
                    .SwellOrder=0.5f,
                    .HeightScale=1.0f,
                    .WhitecapThreshold=0.5f,
                    .FoamAmount=1.0f,
                    .CrestSharpness=0.2f
                }
            },
            SeaStatePreset
            { 
                "Fresh",
                SeaState{} 
            },
            SeaStatePreset
            { 
                "Gale",
                SeaState
                {
                    .WindForce=9.0f,
                    .Openness=0.95f,
                    .WindDirection=0.0f,
                    .Spread=0.15f,
                    .Regularity=0.6f,
                    .RippleDetail=1.0f,
                    .Depth=100.0f,
                    .SwellHeight=2.5f,
                    .SwellPeriod=13.0f,
                    .SwellDirection=20.0f,
                    .SwellOrder=0.4f,
                    .HeightScale=1.0f
                }
            },
            SeaStatePreset
            { 
                "GaleFoam",
                SeaState
                {
                    .WindForce=9.0f,
                    .Openness=0.10f,
                    .WindDirection=0.0f,
                    .Spread=0.15f,
                    .Regularity=0.6f,
                    .RippleDetail=1.0f,
                    .Depth=4.0f,
                    .SwellHeight=2.5f,
                    .SwellPeriod=13.0f,
                    .SwellDirection=20.0f,
                    .SwellOrder=0.4f,
                    .HeightScale=1.5f,
                    .WhitecapThreshold=0.50f,
                    .FoamAmount=3.0f,
                    .CrestSharpness=0.35f
                }
            },
            SeaStatePreset
            { 
                "Long swell",
                SeaState
                {
                    .WindForce=2.0f,
                    .Openness=0.4f,
                    .WindDirection=0.0f,
                    .Spread=0.3f,
                    .Regularity=0.3f,
                    .RippleDetail=0.8f,
                    .Depth=80.0f,
                    .SwellHeight=2.0f,
                    .SwellPeriod=15.0f,
                    .SwellDirection=60.0f,
                    .SwellOrder=0.85f,
                    .HeightScale=1.0f
                }
            }
        };
        return k_Presets;
    }
}
