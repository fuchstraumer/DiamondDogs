#include "PlanetMath.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
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

    int32_t FibonacciNearest(const Double3& direction, int32_t count) noexcept
    {
        if (count <= 1)
        {
            return 0;
        }

        const double goldenRatio = 0.5 * (1.0 + std::sqrt(5.0));
        const double goldenAngle = std::numbers::pi * (3.0 - std::sqrt(5.0));
        const double twoPi = 2.0 * std::numbers::pi;
        const double sqrt5 = std::sqrt(5.0);
        const double n = static_cast<double>(count);
        const double cosTheta = std::clamp(direction.Z, -1.0, 1.0);

        double phi = -(std::atan2(direction.Y, direction.X) - 0.5 * goldenAngle);
        phi -= twoPi * std::round(phi / twoPi);

        const double sinSquared = std::max(1.0 - cosTheta * cosTheta, 1.0e-30);
        const double zone = std::max(2.0, std::floor(std::log(n * std::numbers::pi * sqrt5 * sinSquared) /
                                                     std::log(goldenRatio * goldenRatio)));
        const double fibonacci = std::pow(goldenRatio, zone) / sqrt5;
        const double first = std::round(fibonacci);
        const double second = std::round(fibonacci * goldenRatio);

        const double turn = goldenRatio - 1.0;
        const auto fraction = [](double value) noexcept { return value - std::floor(value); };
        const double b00 = twoPi * (fraction((first + 1.0) * turn) - turn);
        const double b01 = twoPi * (fraction((second + 1.0) * turn) - turn);
        const double b10 = -2.0 * first / n;
        const double b11 = -2.0 * second / n;
        const double determinant = b00 * b11 - b01 * b10;
        const double top = 1.0 - 1.0 / n;
        const double height = cosTheta - top;
        const int64_t corner0 = static_cast<int64_t>(std::floor((b11 * phi - b01 * height) / determinant));
        const int64_t corner1 = static_cast<int64_t>(std::floor((-b10 * phi + b00 * height) / determinant));
        const int64_t firstInt = static_cast<int64_t>(first);
        const int64_t secondInt = static_cast<int64_t>(second);

        double bestDistance = std::numeric_limits<double>::infinity();
        int32_t bestIndex = 0;

        for (int32_t candidate = 0; candidate < 4; ++candidate)
        {
            const int64_t ox = candidate & 1;
            const int64_t oy = candidate >> 1;
            int64_t raw = firstInt * (corner0 + ox) + secondInt * (corner1 + oy);

            if (raw < 0)
            {
                raw = -raw - 1;
            }

            if (raw >= count)
            {
                raw = 2 * static_cast<int64_t>(count) - raw - 1;
            }

            const int32_t index = static_cast<int32_t>(std::clamp<int64_t>(raw, 0, count - 1));
            const Double3 point = LatticePointDouble(index, count);
            const double dx = point.X - direction.X;
            const double dy = point.Y - direction.Y;
            const double dz = point.Z - direction.Z;
            const double distSq = dx * dx + dy * dy + dz * dz;

            if (distSq < bestDistance)
            {
                bestDistance = distSq;
                bestIndex = index;
            }
        }

        return bestIndex;
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
