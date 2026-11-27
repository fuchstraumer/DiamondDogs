#include "PlanetPresets.hpp"
#include "Healpix.hpp"
#include "math/MathStorage.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <vector>

namespace planet_loom
{
    namespace
    {
        struct PlateSeed
        {
            double X{ 0.0 };
            double Y{ 0.0 };
            double Z{ 1.0 };
            double ContThresholdBias{ 0.0 };
        };

        [[nodiscard]] uint64_t MixSeed64(uint64_t state) noexcept;
        [[nodiscard]] double NextDouble01(uint64_t& rng) noexcept;
        [[nodiscard]] double NextDoubleSigned(uint64_t& rng) noexcept;
        [[nodiscard]] uint32_t HashInt3(int32_t x, int32_t y, int32_t z, uint32_t seed) noexcept;
        [[nodiscard]] double ValueNoise3D(double x, double y, double z, uint32_t seed) noexcept;
        [[nodiscard]] double Fbm3D(double x, double y, double z, uint32_t seed, int32_t octaves, double gain) noexcept;
        [[nodiscard]] double CellularF13D(double x, double y, double z, uint32_t seed) noexcept;
        void NormalizeVec3(double& x, double& y, double& z) noexcept;
    }

    std::string_view PlanetPresetName(PlanetPreset preset) noexcept
    {
        switch (preset)
        {
        case PlanetPreset::Reference:
            return "reference";
        case PlanetPreset::Earth:
            return "earth";
        case PlanetPreset::Atoll:
            return "atoll";
        default:
            return "invalid";
        }
    }

    std::optional<PlanetPreset> ParsePlanetPreset(std::string_view text) noexcept
    {
        if (text == "reference" || text == "ref")
        {
            return PlanetPreset::Reference;
        }

        if (text == "earth")
        {
            return PlanetPreset::Earth;
        }

        if (text == "atoll")
        {
            return PlanetPreset::Atoll;
        }

        return std::nullopt;
    }

    std::optional<ShapingPreset> ParseShapingPreset(std::string_view text) noexcept
    {
        if (text == "legacy")
        {
            return ShapingPreset::Legacy;
        }

        if (text == "earth")
        {
            return ShapingPreset::Earth;
        }

        if (text == "atoll")
        {
            return ShapingPreset::Atoll;
        }

        return std::nullopt;
    }

    ShapingState LegacyShapingState() noexcept
    {
        ShapingState state;
        state.ProfileLut = {
            0.0f, 0.25f, 0.65f, 0.95f, 1.0f, 0.92f, 0.78f, 0.62f,
            0.48f, 0.35f, 0.24f, 0.16f, 0.10f, 0.05f, 0.02f, 0.0f,
        };
        return state;
    }

    ShapingState EarthShapingState(uint32_t seed) noexcept
    {
        ShapingState state = LegacyShapingState();
        state.SlabDipEnabled = true;
        state.CoastalNoiseEnabled = true;
        state.AtollShapingEnabled = false;
        // the landscape budget, checked against Earth's hypsometry (README, "The landscape budget")
        state.ContinentErosion = 0.25f;
        // ocean relief relaxes in about 100 My, and arcs stay near their trench (README, "Ocean relief")
        state.OceanErosion = 0.1f;
        state.IslandArcReachKm = 250.0f;
        state.ContinentalReachKm = 800.0f;
        state.CollisionReachKm = 500.0f;
        state.SlabDipAgeWeight = 0.85f;
        state.SlabDipSpeedWeight = 0.65f;
        state.TrenchDepthKm = 1.4f;
        state.CoastalWarpAmplitude = 0.015f;
        state.CoastalDetailKm = 0.75f;
        state.AtollLagoonStrength = 0.0f;
        state.NoiseFrequency = 6.0f;
        state.NoiseSeed = seed;
        state.AtollLandBias = 0.0f;
        return state;
    }

    ShapingState AtollShapingState(uint32_t seed) noexcept
    {
        ShapingState state = LegacyShapingState();
        state.SlabDipEnabled = true;
        state.CoastalNoiseEnabled = true;
        state.AtollShapingEnabled = true;
        state.BaseUplift = 0.55f;
        state.ContinentErosion = 0.25f;
        state.AccretionRelief = 1.35f;
        state.AccretionStartRelief = 0.25f;
        state.ContinentalReachKm = 800.0f;
        state.IslandArcReachKm = 420.0f;
        state.ArcDistanceKm = 210.0f;
        state.CollisionReachKm = 500.0f;
        state.CollisionUpliftScale = 0.50f;
        state.SlabDipAgeWeight = 1.0f;
        state.SlabDipSpeedWeight = 0.85f;
        state.TrenchDepthKm = 1.8f;
        state.CoastalWarpAmplitude = 0.026f;
        state.CoastalDetailKm = 1.20f;
        state.AtollLagoonStrength = 0.65f;
        state.NoiseFrequency = 10.0f;
        state.NoiseSeed = seed;
        state.AtollLandBias = -0.36f;
        return state;
    }

    ShapingState ShapingStateForPreset(ShapingPreset preset, uint32_t seed) noexcept
    {
        switch (preset)
        {
        case ShapingPreset::Earth:
            return EarthShapingState(seed);
        case ShapingPreset::Atoll:
            return AtollShapingState(seed);
        default:
            return LegacyShapingState();
        }
    }

    ShapingParamsHost PackShapingUniform(const ShapingState& state) noexcept
    {
        ShapingParamsHost packed{};
        packed.BaseUplift = state.BaseUplift;
        packed.ContinentErosion = state.ContinentErosion;
        packed.AccretionRelief = state.AccretionRelief;
        packed.AccretionStartRelief = state.AccretionStartRelief;
        packed.ContinentalReachKm = state.ContinentalReachKm;
        packed.IslandArcReachKm = state.IslandArcReachKm;
        packed.ArcDistanceKm = state.ArcDistanceKm;
        packed.CollisionReachKm = state.CollisionReachKm;
        packed.CollisionUpliftScale = state.CollisionUpliftScale;
        packed.SlabDipAgeWeight = state.SlabDipAgeWeight;
        packed.SlabDipSpeedWeight = state.SlabDipSpeedWeight;
        packed.TrenchDepthKm = state.TrenchDepthKm;
        packed.CoastalWarpAmplitude = state.CoastalWarpAmplitude;
        packed.CoastalDetailKm = state.CoastalDetailKm;
        packed.AtollLagoonStrength = state.AtollLagoonStrength;
        packed.NoiseFrequency = state.NoiseFrequency;
        packed.Flags = (state.SlabDipEnabled ? k_ShapingFlagSlabDipEnabled : 0u) |
                       (state.CoastalNoiseEnabled ? k_ShapingFlagCoastalNoiseEnabled : 0u) |
                       (state.AtollShapingEnabled ? k_ShapingFlagAtollShapingEnabled : 0u) |
                       (state.CustomProfileLut ? k_ShapingFlagCustomProfileLut : 0u) |
                       (state.ErosionEnabled ? k_ShapingFlagErosionEnabled : 0u);
        packed.NoiseSeed = state.NoiseSeed;
        packed.AtollLandBias = state.AtollLandBias;
        packed.OceanErosion = state.OceanErosion;
        packed.ErosionDiffusivity = state.ErosionDiffusivity;
        packed.CollapseElevationKm = state.CollapseElevationKm;
        packed.CollapseTimeMy = state.CollapseTimeMy;
        packed.ErosionPad = 0.0f;

        for (uint32_t i = 0u; i < 4u; ++i)
        {
            const size_t base = static_cast<size_t>(i) * 4u;
            packed.ProfileLut[i] = math::Float4{
                state.ProfileLut[base + 0u],
                state.ProfileLut[base + 1u],
                state.ProfileLut[base + 2u],
                state.ProfileLut[base + 3u],
            };
        }

        return packed;
    }

    PlumeSystemState PresetPlumes(PlanetPreset preset, float initial_time, uint64_t seed)
    {
        if (preset != PlanetPreset::Atoll && preset != PlanetPreset::Earth)
        {
            return DefaultPresetPlumes(initial_time);
        }

        PlumeSystemState state;
        state.Enabled = true;
        state.DriftEnabled = true;
        state.GlobalStrength = 1.0f;
        state.OceanicErosionRate = (preset == PlanetPreset::Atoll) ? 0.032f : 0.045f;
        state.SwellScale = (preset == PlanetPreset::Atoll) ? 0.85f : 1.0f;
        state.AccretionThresholdKm = 0.0f;

        uint64_t rng = MixSeed64(seed ^ 0x6a09e667f3bcc909ull);
        const int32_t plumeCount = (preset == PlanetPreset::Atoll) ? 10 : 6;
        const double goldenAngle = std::numbers::pi * (3.0 - std::sqrt(5.0));

        for (int32_t i = 0; i < plumeCount; ++i)
        {
            const double t = (static_cast<double>(i) + 0.5) / static_cast<double>(plumeCount);
            const double sinLat = std::clamp(1.0 - 2.0 * t + 0.12 * NextDoubleSigned(rng), -0.88, 0.88);
            const double latDeg = std::asin(sinLat) * (180.0 / std::numbers::pi);
            const double lonRad = std::fmod(goldenAngle * static_cast<double>(i) + NextDoubleSigned(rng) * 0.35, 2.0 * std::numbers::pi);
            double lonDeg = lonRad * (180.0 / std::numbers::pi);
            if (lonDeg > 180.0)
            {
                lonDeg -= 360.0;
            }
            if (lonDeg < -180.0)
            {
                lonDeg += 360.0;
            }

            const bool finiteLife = (i % 4 == 3);
            state.Plumes.push_back(MantlePlumeDesc
            {
                .LatDeg = static_cast<float>(latDeg),
                .LonDeg = static_cast<float>(lonDeg),
                .ConduitRadiusKm = static_cast<float>(62.0 + 18.0 * NextDouble01(rng)),
                .SwellRadiusKm = static_cast<float>(280.0 + 120.0 * NextDouble01(rng)),
                .PeakSwellKm = static_cast<float>(0.80 + 0.30 * NextDouble01(rng)),
                .BuoyancyFlux = static_cast<float>(6.0 + 1.8 * NextDouble01(rng)),
                .VolcanicUpliftRate = static_cast<float>(2.6 + 0.7 * NextDouble01(rng)),
                .DriftAzimuthDeg = static_cast<float>(360.0 * NextDouble01(rng)),
                .DriftSpeedKmPerMy = static_cast<float>(8.0 + 10.0 * NextDouble01(rng)),
                .BirthTimeMy = initial_time + static_cast<float>((i % 3) * 4),
                .LifetimeMy = finiteLife ? static_cast<float>(85.0 + 45.0 * NextDouble01(rng)) : 0.0f,
                .PulsePeriodMy = static_cast<float>(6.8 + 3.6 * NextDouble01(rng)),
                .PulseAmplitude = static_cast<float>(0.86 + 0.06 * NextDouble01(rng)),
            });
        }

        return state;
    }

    std::optional<planet::HealpixInitialState> GenerateProceduralHealpixState(
        PlanetPreset preset,
        uint32_t order,
        uint32_t plate_count,
        uint64_t seed,
        planet::CrustFlagStorage storage)
    {
        if (order > 13u || plate_count < 4u || plate_count > 32u ||
            (storage != planet::CrustFlagStorage::Bytes && storage != planet::CrustFlagStorage::Words) ||
            (preset != PlanetPreset::Atoll && preset != PlanetPreset::Earth))
        {
            return std::nullopt;
        }

        const uint32_t pointCount = static_cast<uint32_t>(planet::HealpixPixelCount(order));
        const int32_t plates = static_cast<int32_t>(plate_count);
        const int32_t points = static_cast<int32_t>(pointCount);
        const size_t totalCells = static_cast<size_t>(plates) * static_cast<size_t>(points);

        uint64_t rng = MixSeed64(seed ^ (preset == PlanetPreset::Atoll ? 0x9e3779b97f4a7c15ull : 0x517cc1b727220a95ull));
        const uint32_t noiseSeed = static_cast<uint32_t>(MixSeed64(seed) & 0xffffffffu);

        std::vector<PlateSeed> plateSeeds(static_cast<size_t>(plates));
        const double goldenAngle = std::numbers::pi * (3.0 - std::sqrt(5.0));

        for (int32_t p = 0; p < plates; ++p)
        {
            const double y = 1.0 - 2.0 * ((static_cast<double>(p) + 0.5) / static_cast<double>(plates));
            const double r = std::sqrt(std::max(0.0, 1.0 - y * y));
            const double theta = goldenAngle * static_cast<double>(p) + 0.35 * NextDoubleSigned(rng);
            double sx = r * std::cos(theta) + 0.14 * NextDoubleSigned(rng);
            double sy = r * std::sin(theta) + 0.14 * NextDoubleSigned(rng);
            double sz = y + 0.14 * NextDoubleSigned(rng);
            NormalizeVec3(sx, sy, sz);

            plateSeeds[static_cast<size_t>(p)] = PlateSeed
            {
                .X = sx,
                .Y = sy,
                .Z = sz,
                .ContThresholdBias = 0.08 * NextDoubleSigned(rng),
            };
        }

        planet::HealpixInitialState state;
        state.PointCount = pointCount;
        state.PlateCount = plate_count;
        state.InitialTime = 0.0f;

        state.Rotations.reserve(static_cast<size_t>(plates) * 3u);
        state.PoleRates.reserve(static_cast<size_t>(plates));

        const double k_PlanetRadiusKm = 6370.0;
        for (int32_t p = 0; p < plates; ++p)
        {
            state.Rotations.emplace_back(1.0f, 0.0f, 0.0f, 0.0f);
            state.Rotations.emplace_back(0.0f, 1.0f, 0.0f, 0.0f);
            state.Rotations.emplace_back(0.0f, 0.0f, 1.0f, 0.0f);

            // Bias poles nearly orthogonal to each plate center so every plate translates cleanly across the mantle.
            const PlateSeed& seedInfo = plateSeeds[static_cast<size_t>(p)];
            double px = NextDoubleSigned(rng);
            double py = NextDoubleSigned(rng);
            double pz = NextDoubleSigned(rng);
            const double parallel = px * seedInfo.X + py * seedInfo.Y + pz * seedInfo.Z;
            px -= 0.75 * parallel * seedInfo.X;
            py -= 0.75 * parallel * seedInfo.Y;
            pz -= 0.75 * parallel * seedInfo.Z;
            NormalizeVec3(px, py, pz);

            const double speedKmMy = (preset == PlanetPreset::Atoll)
                ? (28.0 + 44.0 * NextDouble01(rng))
                : (25.0 + 40.0 * NextDouble01(rng));
            const float angularRate = static_cast<float>(speedKmMy / k_PlanetRadiusKm);
            state.PoleRates.emplace_back(static_cast<float>(px), static_cast<float>(py), static_cast<float>(pz), angularRate);
        }

        state.PreviousPlate.resize(static_cast<size_t>(points));
        std::vector<uint8_t> healpixAlive(totalCells, 0u);
        std::vector<uint8_t> flagBytes;
        std::vector<uint32_t> flagWords;

        if (storage == planet::CrustFlagStorage::Bytes)
        {
            flagBytes.resize(totalCells, 0u);
        }
        else
        {
            flagWords.resize(totalCells, 0u);
        }

        state.CrustState.resize(totalCells, math::Float4{ 0.0f, 0.0f, 0.0f, 0.0f });

        const double baseContThresh = (preset == PlanetPreset::Atoll) ? 0.315 : 0.56;

        for (int32_t cell = 0; cell < points; ++cell)
        {
            const std::array<double, 3> vec = planet::HealpixPixToVec(static_cast<uint32_t>(cell), order);
            const double px = vec[0];
            const double py = vec[1];
            const double pz = vec[2];

            // Curved organic tectonic plate domains via 3D Fbm coordinate perturbation
            const double warpX = (Fbm3D(px * 2.2 + 3.1, py * 2.2 - 1.7, pz * 2.2 + 0.5, noiseSeed + 101u, 3, 0.5) - 0.5) * 0.48;
            const double warpY = (Fbm3D(px * 2.2 - 4.3, py * 2.2 + 2.9, pz * 2.2 - 1.2, noiseSeed + 202u, 3, 0.5) - 0.5) * 0.48;
            const double warpZ = (Fbm3D(px * 2.2 + 1.4, py * 2.2 + 3.7, pz * 2.2 + 2.3, noiseSeed + 303u, 3, 0.5) - 0.5) * 0.48;
            double wx = px + warpX;
            double wy = py + warpY;
            double wz = pz + warpZ;
            NormalizeVec3(wx, wy, wz);

            int32_t bestPlate = 0;
            double bestDot = -2.0;
            double secondDot = -2.0;

            for (int32_t p = 0; p < plates; ++p)
            {
                const PlateSeed& s = plateSeeds[static_cast<size_t>(p)];
                const double dotProd = wx * s.X + wy * s.Y + wz * s.Z;
                if (dotProd > bestDot)
                {
                    secondDot = bestDot;
                    bestDot = dotProd;
                    bestPlate = p;
                }
                else if (dotProd > secondDot)
                {
                    secondDot = dotProd;
                }
            }

            state.PreviousPlate[static_cast<size_t>(cell)] = bestPlate;

            // Margin proximity [0 near plate boundary, 1 inside plate interior]
            const double marginDist = std::max(0.0, bestDot - secondDot);
            const double interiorFactor = std::clamp(marginDist / 0.14, 0.0, 1.0);

            double score = 0.0;
            double mountainSeed = 0.0;

            if (preset == PlanetPreset::Atoll)
            {
                // Keep ~35% of micro-plates purely oceanic so plates don't lock up into one mega-continent.
                const bool plateIsOceanic = (bestPlate % 3 == 1);
                if (!plateIsOceanic)
                {
                    // Gate archipelagos by a broad province mask so wide open ocean basins separate island clusters.
                    const double macroProvince = Fbm3D(wx * 1.9 + 7.2, wy * 1.9 - 2.1, wz * 1.9 + 5.4, noiseSeed + 404u, 3, 0.5);
                    const double provinceGate = std::clamp((macroProvince - 0.34) / 0.30, 0.0, 1.0);
                    const double archipelagos = Fbm3D(px * 4.4 - 1.9, py * 4.4 + 4.3, pz * 4.4 - 3.7, noiseSeed + 505u, 4, 0.52);
                    const double fineBreakup = Fbm3D(px * 8.8 + 1.2, py * 8.8 - 2.8, pz * 8.8 + 4.1, noiseSeed + 606u, 3, 0.5);
                    const double craterF1 = CellularF13D(px * 4.2 + 2.0, py * 4.2 + 2.0, pz * 4.2 + 2.0, noiseSeed + 707u);
                    const double basinHole = std::exp(-std::pow(craterF1 / 0.24, 2.0));
                    const double polarFade = 1.0 - 0.22 * std::pow(std::abs(pz), 3.0);

                    score = (provinceGate * (0.68 * archipelagos + 0.32 * fineBreakup) - 0.18 * basinHole) * polarFade;
                    mountainSeed = std::max(0.0, score - 0.38);
                }
            }
            else
            {
                const double macro = Fbm3D(wx * 1.7 + 4.0, wy * 1.7 - 3.0, wz * 1.7 + 1.5, noiseSeed + 707u, 4, 0.5);
                const double detail = Fbm3D(px * 4.5 + 2.1, py * 4.5 + 0.8, pz * 4.5 - 2.4, noiseSeed + 808u, 3, 0.5);
                score = 0.72 * macro + 0.28 * detail;
                mountainSeed = std::max(0.0, score - 0.56);
            }

            const double thresh = baseContThresh + 0.5 * plateSeeds[static_cast<size_t>(bestPlate)].ContThresholdBias;
            const bool isCont = score > thresh;

            const size_t flat = static_cast<size_t>(bestPlate) * static_cast<size_t>(points) + static_cast<size_t>(cell);
            healpixAlive[flat] = 1u;

            const uint8_t flagVal = static_cast<uint8_t>(1u | (isCont ? 2u : 0u));
            if (storage == planet::CrustFlagStorage::Bytes)
            {
                flagBytes[flat] = flagVal;
            }
            else
            {
                flagWords[flat] = static_cast<uint32_t>(flagVal);
            }

            if (isCont)
            {
                const float initialRelief = static_cast<float>(0.15 + 0.85 * mountainSeed);
                state.CrustState[flat] = math::Float4{ initialRelief, -100.0f, -100.0f, std::bit_cast<float>(int32_t{ 0 }) };
            }
            else
            {
                // Seafloor age increases from ~5 My near plate boundaries to ~110 My in plate interiors.
                const double ageMy = 5.0 + 105.0 * interiorFactor;
                state.CrustState[flat] = math::Float4{ 0.0f, static_cast<float>(-ageMy), 0.0f, std::bit_cast<float>(int32_t{ 0 }) };
            }
        }

        if (storage == planet::CrustFlagStorage::Bytes)
        {
            state.CrustFlags.resize(flagBytes.size());
            std::memcpy(state.CrustFlags.data(), flagBytes.data(), flagBytes.size());
        }
        else
        {
            const std::span<const std::byte> wordBytes = std::as_bytes(std::span{ flagWords });
            state.CrustFlags.assign(wordBytes.begin(), wordBytes.end());
        }

        state.Caps = planet::ComputeHealpixPlateCaps(healpixAlive, order, plates);
        return state;
    }

    namespace
    {
        uint64_t MixSeed64(uint64_t state) noexcept
        {
            state += 0x9e3779b97f4a7c15ull;
            state = (state ^ (state >> 30u)) * 0xbf58476d1ce4e5b9ull;
            state = (state ^ (state >> 27u)) * 0x94d049bb133111ebull;
            return state ^ (state >> 31u);
        }

        double NextDouble01(uint64_t& rng) noexcept
        {
            rng = MixSeed64(rng);
            return static_cast<double>(rng >> 11u) * (1.0 / 9007199254740992.0);
        }

        double NextDoubleSigned(uint64_t& rng) noexcept
        {
            return NextDouble01(rng) * 2.0 - 1.0;
        }

        uint32_t HashInt3(int32_t x, int32_t y, int32_t z, uint32_t seed) noexcept
        {
            uint32_t hx = static_cast<uint32_t>(x) * 374761393u;
            uint32_t hy = static_cast<uint32_t>(y) * 668265263u;
            uint32_t hz = static_cast<uint32_t>(z) * 2147483647u;
            uint32_t h = hx ^ hy ^ hz ^ (seed * 1664525u + 1013904223u);
            h = (h ^ (h >> 13u)) * 1274126177u;
            return h ^ (h >> 16u);
        }

        double ValueNoise3D(double x, double y, double z, uint32_t seed) noexcept
        {
            const double fx = std::floor(x);
            const double fy = std::floor(y);
            const double fz = std::floor(z);
            const int32_t ix = static_cast<int32_t>(fx);
            const int32_t iy = static_cast<int32_t>(fy);
            const int32_t iz = static_cast<int32_t>(fz);

            const double tx = x - fx;
            const double ty = y - fy;
            const double tz = z - fz;
            const double ux = tx * tx * tx * (tx * (tx * 6.0 - 15.0) + 10.0);
            const double uy = ty * ty * ty * (ty * (ty * 6.0 - 15.0) + 10.0);
            const double uz = tz * tz * tz * (tz * (tz * 6.0 - 15.0) + 10.0);

            const auto val = [seed](int32_t cx, int32_t cy, int32_t cz) noexcept -> double
            {
                return static_cast<double>(HashInt3(cx, cy, cz, seed) & 0x00ffffffu) / 16777215.0;
            };

            const double c000 = val(ix, iy, iz);
            const double c100 = val(ix + 1, iy, iz);
            const double c010 = val(ix, iy + 1, iz);
            const double c110 = val(ix + 1, iy + 1, iz);
            const double c001 = val(ix, iy, iz + 1);
            const double c101 = val(ix + 1, iy, iz + 1);
            const double c011 = val(ix, iy + 1, iz + 1);
            const double c111 = val(ix + 1, iy + 1, iz + 1);

            const double x00 = c000 + (c100 - c000) * ux;
            const double x10 = c010 + (c110 - c010) * ux;
            const double x01 = c001 + (c101 - c001) * ux;
            const double x11 = c011 + (c111 - c011) * ux;
            const double y0 = x00 + (x10 - x00) * uy;
            const double y1 = x01 + (x11 - x01) * uy;
            return y0 + (y1 - y0) * uz;
        }

        double Fbm3D(double x, double y, double z, uint32_t seed, int32_t octaves, double gain) noexcept
        {
            double sum = 0.0;
            double norm = 0.0;
            double amp = 1.0;
            double freq = 1.0;

            for (int32_t o = 0; o < octaves; ++o)
            {
                sum += amp * ValueNoise3D(x * freq, y * freq, z * freq, seed + static_cast<uint32_t>(o) * 97u);
                norm += amp;
                amp *= gain;
                freq *= 2.07;
            }

            return (norm > 0.0) ? (sum / norm) : 0.0;
        }

        double CellularF13D(double x, double y, double z, uint32_t seed) noexcept
        {
            const int32_t ix = static_cast<int32_t>(std::floor(x));
            const int32_t iy = static_cast<int32_t>(std::floor(y));
            const int32_t iz = static_cast<int32_t>(std::floor(z));
            double bestDist2 = 1.0e9;

            for (int32_t dz = -1; dz <= 1; ++dz)
            {
                for (int32_t dy = -1; dy <= 1; ++dy)
                {
                    for (int32_t dx = -1; dx <= 1; ++dx)
                    {
                        const int32_t cx = ix + dx;
                        const int32_t cy = iy + dy;
                        const int32_t cz = iz + dz;
                        const uint32_t h0 = HashInt3(cx, cy, cz, seed);
                        const uint32_t h1 = HashInt3(cx, cy, cz, seed ^ 0x9e3779b9u);
                        const uint32_t h2 = HashInt3(cx, cy, cz, seed ^ 0x85ebca6bu);

                        const double jx = static_cast<double>(cx) + 0.15 + 0.70 * (static_cast<double>(h0 & 0xffffu) / 65535.0);
                        const double jy = static_cast<double>(cy) + 0.15 + 0.70 * (static_cast<double>(h1 & 0xffffu) / 65535.0);
                        const double jz = static_cast<double>(cz) + 0.15 + 0.70 * (static_cast<double>(h2 & 0xffffu) / 65535.0);
                        const double ddx = jx - x;
                        const double ddy = jy - y;
                        const double ddz = jz - z;
                        bestDist2 = std::min(bestDist2, ddx * ddx + ddy * ddy + ddz * ddz);
                    }
                }
            }

            return std::sqrt(bestDist2);
        }

        void NormalizeVec3(double& x, double& y, double& z) noexcept
        {
            const double len = std::sqrt(x * x + y * y + z * z);
            if (len > 1.0e-12)
            {
                x /= len;
                y /= len;
                z /= len;
            }
            else
            {
                x = 0.0;
                y = 0.0;
                z = 1.0;
            }
        }
    }
}

namespace planet_loom
{
    void AddRiftSlots(planet::HealpixInitialState& state, uint32_t spare_slots, planet::CrustFlagStorage storage)
    {
        const uint32_t room = state.PlateCount < k_MaxPlateSlots ? k_MaxPlateSlots - state.PlateCount : 0u;
        const uint32_t added = std::min(spare_slots, room);
        const size_t pointCount = static_cast<size_t>(state.PointCount);
        const size_t flagSize = storage == planet::CrustFlagStorage::Bytes ? 1u : 4u;
        // the lowest plate speed, as MovePlatesAndClearCountersCS clamps it: 10 km/My on a 6370 km radius
        constexpr float k_SlotRate = 10.0f / 6370.0f;

        for (uint32_t slot = 0u; slot < added; ++slot)
        {
            state.Rotations.emplace_back(1.0f, 0.0f, 0.0f, 0.0f);
            state.Rotations.emplace_back(0.0f, 1.0f, 0.0f, 0.0f);
            state.Rotations.emplace_back(0.0f, 0.0f, 1.0f, 0.0f);
            state.PoleRates.emplace_back(0.0f, 0.0f, 1.0f, k_SlotRate);
            state.Caps.Centres.emplace_back(0.0f, 0.0f, 0.0f, planet::k_CapEmpty);
            state.Caps.Spreads.push_back(0u);
        }

        state.CrustFlags.resize(state.CrustFlags.size() + static_cast<size_t>(added) * pointCount * flagSize, std::byte{ 0 });
        state.CrustState.resize(state.CrustState.size() + static_cast<size_t>(added) * pointCount, math::Float4{ 0.0f, 0.0f, 0.0f, 0.0f });
        state.PlateCount += added;
    }
}
