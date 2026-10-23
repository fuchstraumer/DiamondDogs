#pragma once
#ifndef DIAMOND_DOGS_COLOR_PIPE_TRANSFER_FUNCTIONS_HPP
#define DIAMOND_DOGS_COLOR_PIPE_TRANSFER_FUNCTIONS_HPP

namespace color
{
    inline constexpr double PqPeakNits = 10000.0;
    inline constexpr double PqM1 = 2610.0 / 16384.0;
    inline constexpr double PqM2 = 2523.0 / 4096.0 * 128.0;
    inline constexpr double PqC1 = 3424.0 / 4096.0;
    inline constexpr double PqC2 = 2413.0 / 4096.0 * 32.0;
    inline constexpr double PqC3 = 2392.0 / 4096.0 * 32.0;

    /** @brief The luminance that scRGB value 1.0 stands for. */
    inline constexpr double ScRgbReferenceNits = 80.0;

    // each encode and decode below mirrors a negative input about zero, so that extended-range values keep their sign

    /** @brief Encodes linear to sRGB (IEC 61966-2-1). The breakpoints are the exact continuity points of the two
     *  segments, and not the rounded 0.0031308 and 0.04045. */
    [[nodiscard]] double SrgbEncode(double linear) noexcept;
    [[nodiscard]] double SrgbDecode(double encoded) noexcept;

    [[nodiscard]] double Gamma22Encode(double linear) noexcept;
    [[nodiscard]] double Gamma22Decode(double encoded) noexcept;

    /** @brief Encodes absolute nits to the PQ signal (SMPTE ST 2084) in [0, 1]. Input above 10000 nits is clamped,
     *  and so is a negative input. */
    [[nodiscard]] double PqEncode(double nits) noexcept;
    /** @brief Decodes a PQ signal to absolute nits. The signal is clamped to [0, 1]. */
    [[nodiscard]] double PqDecode(double signal) noexcept;

    /** @brief Converts absolute nits to scRGB, which is linear with 1.0 at 80 nits. */
    [[nodiscard]] double ScRgbEncode(double nits) noexcept;
    [[nodiscard]] double ScRgbDecode(double scrgb) noexcept;
}

#endif // !DIAMOND_DOGS_COLOR_PIPE_TRANSFER_FUNCTIONS_HPP
