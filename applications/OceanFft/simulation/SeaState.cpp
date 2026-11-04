#include "SeaState.hpp"
#include "OceanSimulation.hpp"
#include "reflect/Describe.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <string_view>
#include <tuple>

namespace ocean
{
    namespace
    {
        constexpr float k_G = 9.81f;
        constexpr float k_TwoPi = 2.0f * std::numbers::pi_v<float>;
        constexpr float k_SigmaOverRho = 0.074e-3f;
        constexpr float k_ElfouhailyKm = 370.0f;
        constexpr float k_ElfouhailyCm = 0.23f;
        constexpr float k_BringerK1 = 3.9269908169872414f;
        constexpr float k_BringerK3 = 308.0f;

        double KitaigorodskiiDepthCorrection(double omega, double depth) noexcept
        {
            const double omegaH = std::clamp(omega * std::sqrt(depth / k_G), 0.0, 2.0);
            return (omegaH <= 1.0) ? (0.5 * omegaH * omegaH) : (1.0 - 0.5 * (2.0 - omegaH) * (2.0 - omegaH));
        }

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

                const double depthCorrection = KitaigorodskiiDepthCorrection(omega, depth);
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

        float ShoaledWavenumber(float omega, float depth) noexcept
        {
            const float kDeep = std::max((omega * omega) / k_G, 1.0e-6f);
            const float x = std::min(kDeep * depth, 10.0f);
            float k = kDeep / std::sqrt(std::tanh(std::max(x, 1.0e-6f)));

            for (int32_t iter = 0; iter < 2; ++iter)
            {
                const float kd = std::min(k * depth, 10.0f);
                const float th = std::tanh(kd);
                const float ch = std::cosh(kd);
                const float sech2 = 1.0f / (ch * ch);
                const float f = k * th - kDeep;
                const float df = std::max(th + k * depth * sech2, 1.0e-6f);
                k = std::max(k - f / df, 1.0e-6f);
            }

            return k;
        }

        float FiniteDepthWavelength(float omega, float depth) noexcept
        {
            return k_TwoPi / ShoaledWavenumber(omega, depth);
        }

        float ElfouhailyInverseWaveAge(float wind_speed, float fetch_km) noexcept
        {
            const float chi = DimensionlessFetch(wind_speed, fetch_km);
            const float ratio = std::max(chi / 2.2e4f, 1.0e-4f);
            const float tanhTerm = std::tanh(std::pow(ratio, 0.4f));
            return std::min(0.84f * std::pow(std::max(tanhTerm, 1.0e-4f), -0.75f), 5.0f);
        }

        float ElfouhailyPeakOmega(float wind_speed, float fetch_km) noexcept
        {
            const float omegaC = ElfouhailyInverseWaveAge(wind_speed, fetch_km);
            return k_G * omegaC / std::max(wind_speed, 1.0e-4f);
        }

        double ElfouhailyCurvatureB(
            double k,
            double depth,
            double kp,
            double omega_p,
            double omega_c,
            double wind_speed,
            double gamma) noexcept
        {
            const double omega = std::sqrt((k_G * k + k_SigmaOverRho * k * k * k) * std::tanh(std::min(k * depth, 10.0)));
            const double cp = omega_p / kp;
            const double c = std::max(omega / k, 1.0e-6);

            const double alphaP = 6.0e-3 * std::sqrt(omega_c);
            const double gammaBase = (omega_c <= 1.0) ? 1.7 : (1.7 + 6.0 * std::log10(omega_c));
            const double gammaEff = std::max(1.0, gammaBase * (gamma / 1.7));
            const double sigma = 0.08 * (1.0 + 4.0 / (omega_c * omega_c * omega_c));

            const double sqrtKOverKp = std::sqrt(k / kp);
            const double kpOverK = kp / k;
            const double lpm = std::exp(-1.25 * kpOverK * kpOverK);
            const double peakDiff = sqrtKOverKp - 1.0;
            const double gammaExp = std::exp(-(peakDiff * peakDiff) / (2.0 * sigma * sigma));
            const double jp = std::pow(gammaEff, gammaExp);
            const double fp = lpm * jp * std::exp(-(omega_c / std::sqrt(10.0)) * peakDiff);
            const double bl = 0.5 * alphaP * (cp / c) * fp;

            const double cd = (0.8 + 0.065 * wind_speed) * 1.0e-3;
            const double uStar = std::sqrt(cd) * wind_speed;
            const double logUStarOverCm = std::log(std::max(uStar, 1.0e-4) / k_ElfouhailyCm);
            const double alphaM = 1.0e-2 * ((uStar <= k_ElfouhailyCm) ? (1.0 + logUStarOverCm) : (1.0 + 3.0 * logUStarOverCm));
            const double kOverKmDiff = (k / k_ElfouhailyKm) - 1.0;
            const double fm = lpm * std::exp(-0.25 * kOverKmDiff * kOverKmDiff);
            const double bh = 0.5 * alphaM * (k_ElfouhailyCm / c) * fm;

            return bl + bh;
        }

        float ElfouhailyOrBringerVariance(
            float wind_speed,
            float fetch_km,
            float gamma,
            float fade_length,
            float depth,
            bool use_bringer) noexcept
        {
            const double omegaC = ElfouhailyInverseWaveAge(wind_speed, fetch_km);
            const double omegaP = ElfouhailyPeakOmega(wind_speed, fetch_km);
            const double kp = ShoaledWavenumber(static_cast<float>(omegaP), depth);

            const double b1 = ElfouhailyCurvatureB(k_BringerK1, depth, kp, omegaP, omegaC, wind_speed, gamma);
            const double u = std::clamp(static_cast<double>(wind_speed), 5.0, 15.0);
            const double u2 = u * u;
            const double u3 = u2 * u;
            const double u4 = u2 * u2;
            const double u5 = u4 * u;
            const double g1 = 6.1e-2 * u - 3.3e-2;
            const double g2 = -1.314e-4 * u5 + 7.036e-3 * u4 - 1.468e-1 * u3 + 1.4741 * u2 - 6.9122 * u + 12.162;
            const double g3 = 1.5378e-4 * u5 - 7.1804e-3 * u4 + 1.2835e-1 * u3 - 1.095 * u2 + 4.4721 * u - 5.52;
            const double cPeak = -8.6e-6 * u3 + 3.0e-4 * u2 - 1.6e-3 * u + 5.1e-3;

            constexpr int32_t k_Samples = 4096;
            const double kMin = std::max(1.0e-4, 0.001 * kp);
            const double kMax = 2000.0;
            const double logLow = std::log(kMin);
            const double logHigh = std::log(kMax);
            const double step = (logHigh - logLow) / static_cast<double>(k_Samples - 1);
            double variance = 0.0;
            double previous = 0.0;

            for (int32_t i = 0; i < k_Samples; ++i)
            {
                const double k = std::exp(logLow + step * static_cast<double>(i));
                double bTotal = 0.0;

                if (!use_bringer || k < k_BringerK1)
                {
                    bTotal = ElfouhailyCurvatureB(k, depth, kp, omegaP, omegaC, wind_speed, gamma);
                }
                else
                {
                    const double kpOverK = kp / k;
                    const double lpm = std::exp(-1.25 * kpOverK * kpOverK);

                    if (k <= k_BringerK3)
                    {
                        const double logKOverK1 = std::log10(std::max(k / k_BringerK1, 1.0e-6));
                        const double logKOverK3 = std::log10(std::max(k / k_BringerK3, 1.0e-6));
                        const double bDip = b1 * std::exp(-g1 * logKOverK1 * logKOverK1);
                        const double bCap = lpm * cPeak * std::exp(-g2 * logKOverK3 * logKOverK3);
                        bTotal = std::max(bDip, bCap);
                    }
                    else
                    {
                        const double kRatio = k / k_BringerK3;
                        bTotal = lpm * cPeak * std::exp(-g3 * (kRatio * kRatio - 1.0));
                    }
                }

                const double fade = std::exp(-static_cast<double>(fade_length) * fade_length * k * k);
                const double sk = (bTotal / (k * k * k)) * fade;
                // d k = k d(log k)
                const double integrand = sk * k;

                if (i > 0)
                {
                    variance += 0.5 * (previous + integrand) * step;
                }

                previous = integrand;
            }

            return static_cast<float>(variance);
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

        wind.PeakingFactor = 1.0f + 6.0f * std::clamp(state.Regularity, 0.0f, 3.0f);
        const float fadeComplement = 1.0f - std::clamp(state.RippleDetail, 0.0f, 1.0f);
        wind.FadeLength = 0.08f * fadeComplement * fadeComplement * fadeComplement;
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
            const float variance = JonswapVariance(swell.WindSpeed, swell.Fetch, swell.PeakingFactor, swell.FadeLength, 1000.0f);
            const float targetVariance = swell.TargetHeight * swell.TargetHeight / 16.0f;
            spectrum.SwellScale = variance > 0.0f ? targetVariance / variance : 0.0f;
        }

        return spectrum;
    }

    SeaStateReadout MeasureSpectrum(const SpectrumParameters& spectrum) noexcept
    {
        const bool useElfouhailyFamily = spectrum.EnergySpectrumModel == 1u || spectrum.EnergySpectrumModel == 2u;
        const bool useBringer = spectrum.EnergySpectrumModel == 2u;
        const float windVariance = useElfouhailyFamily ?
            ElfouhailyOrBringerVariance(spectrum.WindSpeed, spectrum.Fetch, spectrum.PeakingFactor, spectrum.FadeLength, spectrum.Depth, useBringer) :
            JonswapVariance(spectrum.WindSpeed, spectrum.Fetch, spectrum.PeakingFactor, spectrum.FadeLength, spectrum.Depth);
        const float swellVariance = spectrum.SwellScale > 0.0f ?
            spectrum.SwellScale * JonswapVariance(spectrum.SwellWindSpeed, spectrum.SwellFetch, spectrum.SwellPeakingFactor, spectrum.SwellFadeLength, spectrum.Depth) :
            0.0f;

        // heights scale with the amplitude, variances with its square
        const float scale = spectrum.WaveHeightScale;
        SeaStateReadout readout{};
        readout.SignificantHeight = 4.0f * std::sqrt(windVariance + swellVariance) * scale;
        readout.WindSeaHeight = 4.0f * std::sqrt(windVariance) * scale;
        readout.SwellHeight = 4.0f * std::sqrt(swellVariance) * scale;

        const float peakOmega = useElfouhailyFamily ?
            ElfouhailyPeakOmega(spectrum.WindSpeed, spectrum.Fetch) :
            JonswapPeakOmega(spectrum.WindSpeed, spectrum.Fetch);
        readout.PeakPeriod = k_TwoPi / peakOmega;
        readout.PeakWavelength = FiniteDepthWavelength(peakOmega, spectrum.Depth);
        readout.SwellWavelength = FiniteDepthWavelength(JonswapPeakOmega(spectrum.SwellWindSpeed, spectrum.SwellFetch), spectrum.Depth);
        readout.WindSpeed = spectrum.WindSpeed;

        const int32_t force = static_cast<int32_t>(std::lround(WindSpeedToBeaufort(spectrum.WindSpeed)));
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

// The descriptor lives here until a second file encodes a SeaState (a preset save, a recipe key). Then it moves to
// SeaState.hpp, because every file that encodes a type must see its specialization.
template<>
struct foundation::reflect::Describe<ocean::SeaState>
{
    using SeaState = ocean::SeaState;

    static constexpr std::string_view Name{ "ocean.SeaState" };

    static constexpr std::tuple Fields
    {
        Field<SeaState, float>{ "WindForce", &SeaState::WindForce },
        Field<SeaState, float>{ "Openness", &SeaState::Openness },
        Field<SeaState, float>{ "WindDirection", &SeaState::WindDirection },
        Field<SeaState, float>{ "Spread", &SeaState::Spread },
        Field<SeaState, float>{ "Regularity", &SeaState::Regularity },
        Field<SeaState, float>{ "RippleDetail", &SeaState::RippleDetail },
        Field<SeaState, float>{ "Depth", &SeaState::Depth },
        Field<SeaState, float>{ "SwellHeight", &SeaState::SwellHeight },
        Field<SeaState, float>{ "SwellPeriod", &SeaState::SwellPeriod },
        Field<SeaState, float>{ "SwellDirection", &SeaState::SwellDirection },
        Field<SeaState, float>{ "SwellOrder", &SeaState::SwellOrder },
        Field<SeaState, float>{ "HeightScale", &SeaState::HeightScale },
        Field<SeaState, float>{ "WhitecapThreshold", &SeaState::WhitecapThreshold },
        Field<SeaState, float>{ "FoamAmount", &SeaState::FoamAmount },
        Field<SeaState, float>{ "CrestSharpness", &SeaState::CrestSharpness },
    };
};

static_assert(foundation::reflect::DescribedStruct<ocean::SeaState>);

// a new SeaState field changes the size, so this fails until the field is in the descriptor too
static_assert(sizeof(ocean::SeaState) == 15 * sizeof(float), "add the new SeaState field to its descriptor");
