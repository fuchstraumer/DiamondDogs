#include "TransferFunctions.hpp"
#include <algorithm>
#include <cmath>

namespace
{
    // the two sRGB segments meet here, in encoded and in linear terms
    constexpr double k_SrgbDecodeBreak = 0.0404482362771082;
    constexpr double k_SrgbEncodeBreak = 0.00313066844250063;

    [[nodiscard]] double SrgbEncodeMagnitude(double linear) noexcept;
    [[nodiscard]] double SrgbDecodeMagnitude(double encoded) noexcept;
}

namespace color
{
    double SrgbEncode(double linear) noexcept
    {
        return std::copysign(SrgbEncodeMagnitude(std::fabs(linear)), linear);
    }

    double SrgbDecode(double encoded) noexcept
    {
        return std::copysign(SrgbDecodeMagnitude(std::fabs(encoded)), encoded);
    }

    double Gamma22Encode(double linear) noexcept
    {
        return std::copysign(std::pow(std::fabs(linear), 1.0 / 2.2), linear);
    }

    double Gamma22Decode(double encoded) noexcept
    {
        return std::copysign(std::pow(std::fabs(encoded), 2.2), encoded);
    }

    double PqEncode(double nits) noexcept
    {
        const double normalized = std::clamp(nits / PqPeakNits, 0.0, 1.0);
        const double power = std::pow(normalized, PqM1);
        const double ratio = (PqC1 + PqC2 * power) / (1.0 + PqC3 * power);

        return std::pow(ratio, PqM2);
    }

    double PqDecode(double signal) noexcept
    {
        const double clamped = std::clamp(signal, 0.0, 1.0);
        const double power = std::pow(clamped, 1.0 / PqM2);
        const double numerator = std::max(power - PqC1, 0.0);
        const double denominator = PqC2 - PqC3 * power;

        return PqPeakNits * std::pow(numerator / denominator, 1.0 / PqM1);
    }

    double ScRgbEncode(double nits) noexcept
    {
        return nits / ScRgbReferenceNits;
    }

    double ScRgbDecode(double scrgb) noexcept
    {
        return scrgb * ScRgbReferenceNits;
    }
}

namespace
{
    double SrgbEncodeMagnitude(double linear) noexcept
    {
        if (linear <= k_SrgbEncodeBreak)
        {
            return 12.92 * linear;
        }

        return 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
    }

    double SrgbDecodeMagnitude(double encoded) noexcept
    {
        if (encoded <= k_SrgbDecodeBreak)
        {
            return encoded / 12.92;
        }

        return std::pow((encoded + 0.055) / 1.055, 2.4);
    }
}
