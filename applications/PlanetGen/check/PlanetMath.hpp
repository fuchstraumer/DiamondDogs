#pragma once
#ifndef DIAMOND_DOGS_PLANET_MATH_HPP
#define DIAMOND_DOGS_PLANET_MATH_HPP
#include "math/MathStorage.hpp"
#include <cstdint>
#include <span>

namespace planet
{
    inline constexpr double k_PlanetRadiusKm = 6370.0;

    struct Double3
    {
        double X{ 0.0 };
        double Y{ 0.0 };
        double Z{ 0.0 };
    };

    [[nodiscard]] Double3 LatticePointDouble(int32_t index, int32_t count) noexcept;
    [[nodiscard]] double PointSpacingRadians(int32_t point_count) noexcept;
    [[nodiscard]] double Dot(const Double3& a, const math::Float3& b) noexcept;
    [[nodiscard]] double Length(const Double3& value) noexcept;
    [[nodiscard]] double Distance(const Double3& a, const math::Float3& b) noexcept;
    [[nodiscard]] double ChordKilometers(const Double3& a, const Double3& b) noexcept;
    [[nodiscard]] Double3 RotateRowMajorColumnVector(const math::Float3x3& rotation, const Double3& vector) noexcept;
    [[nodiscard]] Double3 CellInWorld(std::span<const math::Float3x3> rotations,
                                      int32_t plate,
                                      int32_t cell,
                                      int32_t point_count) noexcept;
    [[nodiscard]] bool IsClose(float actual, float expected, float relative_tolerance, float absolute_tolerance) noexcept;
    [[nodiscard]] uint32_t DivRoundUp(uint32_t count, uint32_t group_size) noexcept;
}

#endif // !DIAMOND_DOGS_PLANET_MATH_HPP
