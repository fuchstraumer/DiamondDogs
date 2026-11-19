#include "PlanetMath.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace planet
{
    Double3 LatticePointDouble(int32_t index, int32_t count) noexcept
    {
        const double goldenAngle = std::numbers::pi * (3.0 - std::sqrt(5.0));
        const double z = 1.0 - (2.0 * static_cast<double>(index) + 1.0) / static_cast<double>(count);
        const double radius = std::sqrt(std::max(1.0 - z * z, 0.0));
        const double angle = (static_cast<double>(index) + 0.5) * goldenAngle;
        return Double3{ radius * std::cos(angle), radius * std::sin(angle), z };
    }

    double PointSpacingRadians(int32_t point_count) noexcept
    {
        return std::sqrt(4.0 * std::numbers::pi / static_cast<double>(point_count));
    }

    double Dot(const Double3& a, const math::Float3& b) noexcept
    {
        return a.X * static_cast<double>(b.x) + a.Y * static_cast<double>(b.y) + a.Z * static_cast<double>(b.z);
    }

    double Length(const Double3& value) noexcept
    {
        return std::sqrt(value.X * value.X + value.Y * value.Y + value.Z * value.Z);
    }

    double Distance(const Double3& a, const math::Float3& b) noexcept
    {
        const double deltaX = a.X - static_cast<double>(b.x);
        const double deltaY = a.Y - static_cast<double>(b.y);
        const double deltaZ = a.Z - static_cast<double>(b.z);
        return std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
    }

    double ChordKilometers(const Double3& a, const Double3& b) noexcept
    {
        const double deltaX = a.X - b.X;
        const double deltaY = a.Y - b.Y;
        const double deltaZ = a.Z - b.Z;
        const double chord = std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
        return 2.0 * std::asin(std::clamp(0.5 * chord, 0.0, 1.0)) * k_PlanetRadiusKm;
    }

    Double3 RotateRowMajorColumnVector(const math::Float3x3& rotation, const Double3& vector) noexcept
    {
        // PlanetLab matrices multiply column vectors (M * v), whereas Float3x3::operator* uses row vectors (v * M)
        return Double3
        {
            Dot(vector, rotation.Row(0u)),
            Dot(vector, rotation.Row(1u)),
            Dot(vector, rotation.Row(2u)),
        };
    }

    Double3 CellInWorld(std::span<const math::Float3x3> rotations,
                        int32_t plate,
                        int32_t cell,
                        int32_t point_count) noexcept
    {
        const Double3 frame = LatticePointDouble(cell, point_count);
        return RotateRowMajorColumnVector(rotations[static_cast<size_t>(plate)], frame);
    }

    bool IsClose(float actual, float expected, float relative_tolerance, float absolute_tolerance) noexcept
    {
        if (actual == expected)
        {
            return true;
        }

        return std::fabs(actual - expected) <= absolute_tolerance + relative_tolerance * std::fabs(expected);
    }

    uint32_t DivRoundUp(uint32_t count, uint32_t group_size) noexcept
    {
        return (count + group_size - 1u) / group_size;
    }
}
