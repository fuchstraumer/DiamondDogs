#include "MantlePlumes.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace planet_loom
{
    namespace
    {
        [[nodiscard]] math::Float3 UnitSpherePosition(float lat_deg, float lon_deg) noexcept;
        [[nodiscard]] math::Float3 DriftRotationAxis(float lat_deg, float lon_deg, float azimuth_deg) noexcept;
    }

    PlumeSystemState DefaultPresetPlumes(float initial_time)
    {
        PlumeSystemState state;
        state.Enabled = true;
        state.DriftEnabled = true;
        state.GlobalStrength = 1.0f;
        state.OceanicErosionRate = 0.015f;
        state.SwellScale = 1.0f;
        state.AccretionThresholdKm = 0.0f;

        state.Plumes.push_back(MantlePlumeDesc
        {
            .LatDeg = 22.0f,
            .LonDeg = -35.0f,
            .ConduitRadiusKm = 95.0f,
            .SwellRadiusKm = 520.0f,
            .PeakSwellKm = 1.4f,
            .BuoyancyFlux = 6.5f,
            .VolcanicUpliftRate = 2.2f,
            .DriftAzimuthDeg = 45.0f,
            .DriftSpeedKmPerMy = 12.0f,
            .BirthTimeMy = initial_time,
            .LifetimeMy = 0.0f,
            .PulsePeriodMy = 14.0f,
            .PulseAmplitude = 0.48f,
        });

        state.Plumes.push_back(MantlePlumeDesc
        {
            .LatDeg = -28.0f,
            .LonDeg = 15.0f,
            .ConduitRadiusKm = 90.0f,
            .SwellRadiusKm = 480.0f,
            .PeakSwellKm = 1.3f,
            .BuoyancyFlux = 6.0f,
            .VolcanicUpliftRate = 2.0f,
            .DriftAzimuthDeg = 120.0f,
            .DriftSpeedKmPerMy = 10.0f,
            .BirthTimeMy = initial_time,
            .LifetimeMy = 0.0f,
            .PulsePeriodMy = 11.0f,
            .PulseAmplitude = 0.50f,
        });

        state.Plumes.push_back(MantlePlumeDesc
        {
            .LatDeg = -8.0f,
            .LonDeg = 155.0f,
            .ConduitRadiusKm = 85.0f,
            .SwellRadiusKm = 450.0f,
            .PeakSwellKm = 1.2f,
            .BuoyancyFlux = 5.5f,
            .VolcanicUpliftRate = 1.9f,
            .DriftAzimuthDeg = 310.0f,
            .DriftSpeedKmPerMy = 15.0f,
            .BirthTimeMy = initial_time,
            .LifetimeMy = 0.0f,
            .PulsePeriodMy = 10.0f,
            .PulseAmplitude = 0.45f,
        });

        state.Plumes.push_back(MantlePlumeDesc
        {
            .LatDeg = 42.0f,
            .LonDeg = 65.0f,
            .ConduitRadiusKm = 110.0f,
            .SwellRadiusKm = 600.0f,
            .PeakSwellKm = 1.6f,
            .BuoyancyFlux = 7.0f,
            .VolcanicUpliftRate = 1.8f,
            .DriftAzimuthDeg = 225.0f,
            .DriftSpeedKmPerMy = 8.0f,
            .BirthTimeMy = initial_time,
            .LifetimeMy = 0.0f,
            .PulsePeriodMy = 18.0f,
            .PulseAmplitude = 0.35f,
        });

        return state;
    }

    PlumeSystemState EmptyPlumeSystem()
    {
        PlumeSystemState state;
        state.Enabled = false;
        state.DriftEnabled = false;
        return state;
    }

    PlumeParamsHost PackPlumeUniform(const PlumeSystemState& state, float time_my)
    {
        PlumeParamsHost packed{};
        const uint32_t count = static_cast<uint32_t>(
            std::min<size_t>(state.Plumes.size(), static_cast<size_t>(k_MaxMantlePlumes)));

        packed.PlumeCount = count;
        packed.Time = time_my;
        packed.GlobalStrength = state.GlobalStrength;
        packed.OceanicErosionRate = state.OceanicErosionRate;
        packed.SwellScale = state.SwellScale;
        packed.AccretionThresholdKm = state.AccretionThresholdKm;
        packed.Flags = (state.Enabled ? k_PlumeFlagEnabled : 0u) |
                       (state.DriftEnabled ? k_PlumeFlagDriftEnabled : 0u);
        packed.Pad = 0u;

        for (uint32_t i = 0u; i < count; ++i)
        {
            const MantlePlumeDesc& src = state.Plumes[i];
            const math::Float3 pos = UnitSpherePosition(src.LatDeg, src.LonDeg);
            const math::Float3 axis = DriftRotationAxis(src.LatDeg, src.LonDeg, src.DriftAzimuthDeg);

            auto& dst = packed.Plumes[i];
            dst.PositionAndRadius = math::Float4{ pos.x, pos.y, pos.z, src.ConduitRadiusKm };
            dst.DriftAndSpeed = math::Float4{ axis.x, axis.y, axis.z, src.DriftSpeedKmPerMy };
            dst.SwellAndFlux = math::Float4{ src.SwellRadiusKm, src.PeakSwellKm, src.BuoyancyFlux, src.VolcanicUpliftRate };
            dst.TimingAndPulse = math::Float4{ src.BirthTimeMy, src.LifetimeMy, src.PulsePeriodMy, src.PulseAmplitude };
        }

        return packed;
    }

    namespace
    {
        math::Float3 UnitSpherePosition(float lat_deg, float lon_deg) noexcept
        {
            const float degToRad = std::numbers::pi_v<float> / 180.0f;
            const float lat = lat_deg * degToRad;
            const float lon = lon_deg * degToRad;
            const float cosLat = std::cos(lat);
            return math::Float3{ cosLat * std::cos(lon), cosLat * std::sin(lon), std::sin(lat) };
        }

        math::Float3 DriftRotationAxis(float lat_deg, float lon_deg, float azimuth_deg) noexcept
        {
            const float degToRad = std::numbers::pi_v<float> / 180.0f;
            const float lat = lat_deg * degToRad;
            const float lon = lon_deg * degToRad;
            const float az = azimuth_deg * degToRad;

            const float sinLat = std::sin(lat);
            const float cosLat = std::cos(lat);
            const float sinLon = std::sin(lon);
            const float cosLon = std::cos(lon);

            const math::Float3 pos{ cosLat * cosLon, cosLat * sinLon, sinLat };
            const math::Float3 east{ -sinLon, cosLon, 0.0f };
            const math::Float3 north{ -sinLat * cosLon, -sinLat * sinLon, cosLat };

            const float sinAz = std::sin(az);
            const float cosAz = std::cos(az);
            const math::Float3 tangent
            {
                north.x * cosAz + east.x * sinAz,
                north.y * cosAz + east.y * sinAz,
                north.z * cosAz + east.z * sinAz,
            };

            const math::Float3 axis
            {
                pos.y * tangent.z - pos.z * tangent.y,
                pos.z * tangent.x - pos.x * tangent.z,
                pos.x * tangent.y - pos.y * tangent.x,
            };
            const float len = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);

            if (len <= 1.0e-6f)
            {
                return math::Float3{ 0.0f, 0.0f, 1.0f };
            }

            return math::Float3{ axis.x / len, axis.y / len, axis.z / len };
        }
    }
}
