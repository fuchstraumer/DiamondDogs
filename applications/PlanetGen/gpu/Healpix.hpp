#pragma once
#ifndef DIAMOND_DOGS_PLANET_HEALPIX_HPP
#define DIAMOND_DOGS_PLANET_HEALPIX_HPP
#include <array>
#include <cstdint>

namespace planet
{
    /** HEALPix (Gorski et al. 2005) splits the sphere into 12 base faces of equal area, and each face into
     *  Nside x Nside pixels of equal area. Nside is 2^order. README.md explains the mapping. */
    inline constexpr uint32_t k_HealpixBaseFaces = 12u;
    inline constexpr uint32_t k_HealpixMaxOrder = 29u;

    /** @brief The pixel count at `order`: 12 * 4^order. */
    [[nodiscard]] uint64_t HealpixPixelCount(uint32_t order) noexcept;

    /** @brief The nested index of the pixel that holds the unit direction (x, y, z), at `order`. In nested order,
     *  index >> 2 is the parent pixel at order - 1, so each run of 4^k indices is one compact patch. */
    [[nodiscard]] uint64_t HealpixNestedIndex(double x, double y, double z, uint32_t order) noexcept;

    /** @brief The unit direction vector (x, y, z) at the center of nested pixel `pixel_index` at `order`. */
    [[nodiscard]] std::array<double, 3> HealpixPixToVec(uint32_t pixel_index, uint32_t order) noexcept;

    /** @brief The 8 nested neighbor indices (W, NW, N, NE, E, SE, S, SW) of `pixel_index` at `order`, or -1 at
     *  missing face-corner diagonals. */
    [[nodiscard]] std::array<int32_t, 8> HealpixNeighbors8(uint32_t pixel_index, uint32_t order) noexcept;

    /** @brief The lowest order with at least `pixels_per_point` pixels for each of `point_count` points. */
    [[nodiscard]] uint32_t HealpixOrderFor(uint32_t point_count, uint32_t pixels_per_point) noexcept;
}

#endif // !DIAMOND_DOGS_PLANET_HEALPIX_HPP
