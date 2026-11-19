#include "Healpix.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

// ang2pix_nest from Gorski et al. 2005, "HEALPix: a Framework for High Resolution Discretization and Fast Analysis
// of Data Distributed on the Sphere", ApJ 622:759. The algorithm is theirs; this is a transcription onto unit
// vectors, with the integer steps written out.
namespace planet
{
    namespace
    {
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
