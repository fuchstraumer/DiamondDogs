#include "Healpix.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <utility>

// ang2pix_nest, pix2vec_nest, and neighbours from Gorski et al. 2005, "HEALPix: a Framework for High Resolution
// Discretization and Fast Analysis of Data Distributed on the Sphere", ApJ 622:759. The algorithm is theirs; this is a
// transcription onto unit vectors, with the integer steps written out.
namespace planet
{
    namespace
    {
        constexpr std::array<int32_t, 12> k_FaceSouthRingMultiplier{ 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4 };
        constexpr std::array<int32_t, 12> k_FaceAzimuthOffset{ 1, 3, 5, 7, 0, 2, 4, 6, 1, 3, 5, 7 };
        constexpr std::array<int32_t, 8> k_NeighborDeltaX{ -1, -1, 0, 1, 1, 1, 0, -1 };
        constexpr std::array<int32_t, 8> k_NeighborDeltaY{ 0, 1, 1, 1, 0, -1, -1, -1 };
        constexpr std::array<std::array<int32_t, 12>, 9> k_AdjacentBaseFace
        {{
            { 8, 9, 10, 11, -1, -1, -1, -1, 10, 11, 8, 9 },
            { 5, 6, 7, 4, 8, 9, 10, 11, 9, 10, 11, 8 },
            { -1, -1, -1, -1, 5, 6, 7, 4, -1, -1, -1, -1 },
            { 4, 5, 6, 7, 11, 8, 9, 10, 11, 8, 9, 10 },
            { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 },
            { 1, 2, 3, 0, 0, 1, 2, 3, 5, 6, 7, 4 },
            { -1, -1, -1, -1, 7, 4, 5, 6, -1, -1, -1, -1 },
            { 3, 0, 1, 2, 3, 0, 1, 2, 4, 5, 6, 7 },
            { 2, 3, 0, 1, -1, -1, -1, -1, 0, 1, 2, 3 },
        }};
        // bit 0 = flip X, bit 1 = flip Y, bit 2 = swap XY (applied in that order)
        constexpr std::array<std::array<int32_t, 3>, 9> k_SeamTransformMask
        {{
            { 0, 0, 3 },
            { 0, 0, 6 },
            { 0, 0, 0 },
            { 0, 0, 5 },
            { 0, 0, 0 },
            { 5, 0, 0 },
            { 0, 0, 0 },
            { 6, 0, 0 },
            { 3, 0, 0 },
        }};

        // puts the low 32 bits of value into the even bits of the result
        [[nodiscard]] uint64_t SpreadBits(uint64_t value) noexcept
        {
            value &= 0x00000000FFFFFFFFull;
            value = (value | (value << 16u)) & 0x0000FFFF0000FFFFull;
            value = (value | (value << 8u)) & 0x00FF00FF00FF00FFull;
            value = (value | (value << 4u)) & 0x0F0F0F0F0F0F0F0Full;
            value = (value | (value << 2u)) & 0x3333333333333333ull;
            value = (value | (value << 1u)) & 0x5555555555555555ull;
            return value;
        }

        // collects the even bits of value into the low 32 bits of the result
        [[nodiscard]] uint32_t CompactBits(uint64_t value) noexcept
        {
            value &= 0x5555555555555555ull;
            value = (value | (value >> 1u)) & 0x3333333333333333ull;
            value = (value | (value >> 2u)) & 0x0F0F0F0F0F0F0F0Full;
            value = (value | (value >> 4u)) & 0x00FF00FF00FF00FFull;
            value = (value | (value >> 8u)) & 0x0000FFFF0000FFFFull;
            value = (value | (value >> 16u)) & 0x00000000FFFFFFFFull;
            return static_cast<uint32_t>(value);
        }
    }

    uint64_t HealpixPixelCount(uint32_t order) noexcept
    {
        return uint64_t{ k_HealpixBaseFaces } << (2u * order);
    }

    uint64_t HealpixNestedIndex(double x, double y, double z, uint32_t order) noexcept
    {
        const int64_t nside = int64_t{ 1 } << order;
        const int64_t lastInFace = nside - 1;
        const double absZ = std::abs(z);

        // tt is the longitude in quarter turns, in [0, 4)
        double phi = std::atan2(y, x);

        if (phi < 0.0)
        {
            phi += 2.0 * std::numbers::pi;
        }

        const double tt = std::clamp(phi / (0.5 * std::numbers::pi), 0.0, std::nextafter(4.0, 0.0));
        int64_t face = 0;
        int64_t ix = 0;
        int64_t iy = 0;

        if (absZ <= 2.0 / 3.0)
        {
            // the equatorial belt: the two diagonal coordinates jp and jm run along the edges of the faces
            const double along = static_cast<double>(nside) * (0.5 + tt);
            const double across = static_cast<double>(nside) * z * 0.75;
            const int64_t jp = static_cast<int64_t>(along - across);
            const int64_t jm = static_cast<int64_t>(along + across);
            const int64_t faceP = jp >> order;
            const int64_t faceM = jm >> order;

            if (faceP == faceM)
            {
                face = faceP | 4;
            }
            else if (faceP < faceM)
            {
                face = faceP;
            }
            else
            {
                face = faceM + 8;
            }

            ix = jm & lastInFace;
            iy = nside - (jp & lastInFace) - 1;
        }
        else
        {
            // a polar cap: the distance from the pole scales as sqrt(1 - |z|), which keeps the pixel areas equal
            const int64_t quarter = std::min(static_cast<int64_t>(tt), int64_t{ 3 });
            const double inQuarter = tt - static_cast<double>(quarter);
            const double fromPole = static_cast<double>(nside) * std::sqrt(3.0 * (1.0 - absZ));
            const int64_t jp = std::min(static_cast<int64_t>(inQuarter * fromPole), lastInFace);
            const int64_t jm = std::min(static_cast<int64_t>((1.0 - inQuarter) * fromPole), lastInFace);

            if (z >= 0.0)
            {
                face = quarter;
                ix = nside - jm - 1;
                iy = nside - jp - 1;
            }
            else
            {
                face = quarter + 8;
                ix = jp;
                iy = jm;
            }
        }

        const uint64_t inFace = SpreadBits(static_cast<uint64_t>(ix)) | (SpreadBits(static_cast<uint64_t>(iy)) << 1u);
        return (static_cast<uint64_t>(face) << (2u * order)) + inFace;
    }

    std::array<double, 3> HealpixPixToVec(uint32_t pixel_index, uint32_t order) noexcept
    {
        const int64_t nside = int64_t{ 1 } << order;
        const uint64_t faceBits = uint64_t{ 1 } << (2u * order);
        const uint64_t pixel = static_cast<uint64_t>(pixel_index);
        const int64_t face = static_cast<int64_t>(pixel >> (2u * order));
        const uint64_t inFace = pixel & (faceBits - 1ull);
        const int64_t ix = static_cast<int64_t>(CompactBits(inFace));
        const int64_t iy = static_cast<int64_t>(CompactBits(inFace >> 1u));

        const int64_t jr = static_cast<int64_t>(k_FaceSouthRingMultiplier[static_cast<size_t>(face)]) * nside - ix - iy - 1;
        int64_t nr = nside;
        int64_t kshift = 0;
        double z = 0.0;

        if (jr < nside)
        {
            nr = jr;
            z = 1.0 - static_cast<double>(nr * nr) / (3.0 * static_cast<double>(nside * nside));
            kshift = 0;
        }
        else if (jr > 3 * nside)
        {
            nr = 4 * nside - jr;
            z = -1.0 + static_cast<double>(nr * nr) / (3.0 * static_cast<double>(nside * nside));
            kshift = 0;
        }
        else
        {
            nr = nside;
            z = static_cast<double>(2 * nside - jr) * 2.0 / (3.0 * static_cast<double>(nside));
            kshift = (jr - nside) & 1;
        }

        int64_t jp = (static_cast<int64_t>(k_FaceAzimuthOffset[static_cast<size_t>(face)]) * nr + ix - iy + 1 + kshift) / 2;
        const int64_t nl4 = 4 * nr;

        if (jp > nl4)
        {
            jp -= nl4;
        }

        if (jp < 1)
        {
            jp += nl4;
        }

        const double phi = (static_cast<double>(jp) - 0.5 * static_cast<double>(kshift + 1)) *
                           (0.5 * std::numbers::pi / static_cast<double>(nr));
        const double sinTheta = std::sqrt(std::max(0.0, (1.0 - z) * (1.0 + z)));
        return { sinTheta * std::cos(phi), sinTheta * std::sin(phi), z };
    }

    std::array<int32_t, 8> HealpixNeighbors8(uint32_t pixel_index, uint32_t order) noexcept
    {
        const int32_t nside = int32_t{ 1 } << order;
        const int32_t lastInFace = nside - 1;
        const uint64_t faceBits = uint64_t{ 1 } << (2u * order);
        const uint64_t pixel = static_cast<uint64_t>(pixel_index);
        const int32_t face = static_cast<int32_t>(pixel >> (2u * order));
        const uint64_t inFace = pixel & (faceBits - 1ull);
        const int32_t ix = static_cast<int32_t>(CompactBits(inFace));
        const int32_t iy = static_cast<int32_t>(CompactBits(inFace >> 1u));
        const size_t zone = static_cast<size_t>(face / 4);

        std::array<int32_t, 8> neighbors{};

        for (size_t dir = 0u; dir < 8u; ++dir)
        {
            int32_t nx = ix + k_NeighborDeltaX[dir];
            int32_t ny = iy + k_NeighborDeltaY[dir];
            int32_t xOffset = 0;
            int32_t yOffset = 0;

            if (nx < 0)
            {
                nx += nside;
                xOffset = -1;
            }
            else if (nx >= nside)
            {
                nx -= nside;
                xOffset = 1;
            }

            if (ny < 0)
            {
                ny += nside;
                yOffset = -1;
            }
            else if (ny >= nside)
            {
                ny -= nside;
                yOffset = 1;
            }

            const size_t nbnum = static_cast<size_t>((xOffset + 1) + 3 * (yOffset + 1));
            const int32_t neighborFace = k_AdjacentBaseFace[nbnum][static_cast<size_t>(face)];

            if (neighborFace < 0)
            {
                neighbors[dir] = -1;
                continue;
            }

            const int32_t mask = k_SeamTransformMask[nbnum][zone];

            if ((mask & 1) != 0)
            {
                nx = lastInFace - nx;
            }

            if ((mask & 2) != 0)
            {
                ny = lastInFace - ny;
            }

            if ((mask & 4) != 0)
            {
                std::swap(nx, ny);
            }

            const uint64_t neighborInFace =
                SpreadBits(static_cast<uint64_t>(nx)) | (SpreadBits(static_cast<uint64_t>(ny)) << 1u);
            neighbors[dir] = static_cast<int32_t>((static_cast<uint64_t>(neighborFace) << (2u * order)) + neighborInFace);
        }

        return neighbors;
    }

    uint32_t HealpixOrderFor(uint32_t point_count, uint32_t pixels_per_point) noexcept
    {
        const uint64_t wanted = uint64_t{ point_count } * uint64_t{ pixels_per_point };
        uint32_t order = 0u;

        while (order < k_HealpixMaxOrder && HealpixPixelCount(order) < wanted)
        {
            ++order;
        }

        return order;
    }
}
