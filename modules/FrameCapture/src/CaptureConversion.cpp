#include "CaptureConversion.hpp"
#include "FrameCapture.hpp"
#include "ImageDataFormats.hpp"
#include "math/Math.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <span>
#include <string>
#include <utility>
#include <utility>
#include <vector>

namespace capture::conversion
{
    uint32_t BytesPerPixel(ImageFormat format) noexcept
    {
        switch (format.ComponentFormat)
        {
        case ImageComponentFormats::RGBA8:
        case ImageComponentFormats::BGRA8:
        case ImageComponentFormats::A2B10G10R10:
        case ImageComponentFormats::A2R10G10B10:
            return 4u;
        case ImageComponentFormats::RGBA16:
            return format.DataType == ImageDataType::Float ? 8u : 0u;
        default:
            return 0u;
        }
    }

    uint8_t EncodeSrgb(float linear) noexcept
    {
        // NaN goes to 0, through the clamp's comparisons
        const float value = linear > 0.0f ? std::min(linear, 1.0f) : 0.0f;
        const float encoded = value <= 0.0031308f ? 12.92f * value : (1.055f * std::pow(value, 1.0f / 2.4f)) - 0.055f;

        return static_cast<uint8_t>((encoded * 255.0f) + 0.5f);
    }

    float DecodeSrgb(float encoded) noexcept
    {
        const float value = std::clamp(encoded, 0.0f, 1.0f);

        return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
    }

    float DecodePq(float encoded) noexcept
    {
        constexpr float m1 = 0.1593017578125f;
        constexpr float m2 = 78.84375f;
        constexpr float c1 = 0.8359375f;
        constexpr float c2 = 18.8515625f;
        constexpr float c3 = 18.6875f;
        const float p = std::pow(std::clamp(encoded, 0.0f, 1.0f), 1.0f / m2);
        const float nits = 10000.0f * std::pow(std::max(p - c1, 0.0f) / (c2 - (c3 * p)), 1.0f / m1);

        return nits / 80.0f;
    }

    void Bt2020ToBt709(float& r, float& g, float& b) noexcept
    {
        // rows for a column vector. math::Float3x3 * Float3 multiplies a row vector, which applies the transpose
        constexpr float k_Bt2020ToBt709[3][3]{
            {  1.6605f, -0.5876f, -0.0728f },
            { -0.1246f,  1.1329f, -0.0083f },
            { -0.0182f, -0.1006f,  1.1187f },
        };
        const float red = r;
        const float green = g;
        const float blue = b;

        r = (k_Bt2020ToBt709[0][0] * red) + (k_Bt2020ToBt709[0][1] * green) + (k_Bt2020ToBt709[0][2] * blue);
        g = (k_Bt2020ToBt709[1][0] * red) + (k_Bt2020ToBt709[1][1] * green) + (k_Bt2020ToBt709[1][2] * blue);
        b = (k_Bt2020ToBt709[2][0] * red) + (k_Bt2020ToBt709[2][1] * green) + (k_Bt2020ToBt709[2][2] * blue);
    }

    math::Float3 DecodeLinear(std::span<const std::byte> pixels, size_t index, ImageFormat format, CaptureEncoding encoding, float linear_scale)
    {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;

        switch (format.ComponentFormat)
        {
        case ImageComponentFormats::RGBA8:
        case ImageComponentFormats::BGRA8:
        {
            const uint8_t* texel = reinterpret_cast<const uint8_t*>(pixels.data()) + (index * 4u);
            const bool bgra = format.ComponentFormat == ImageComponentFormats::BGRA8;

            r = texel[bgra ? 2u : 0u] / 255.0f;
            g = texel[1u] / 255.0f;
            b = texel[bgra ? 0u : 2u] / 255.0f;
            break;
        }
        case ImageComponentFormats::A2B10G10R10:
        case ImageComponentFormats::A2R10G10B10:
        {
            uint32_t word = 0u;

            std::memcpy(&word, pixels.data() + (index * 4u), 4u);

            const float low = static_cast<float>(word & 0x3FFu) / 1023.0f;
            const float mid = static_cast<float>((word >> 10u) & 0x3FFu) / 1023.0f;
            const float high = static_cast<float>((word >> 20u) & 0x3FFu) / 1023.0f;
            const bool redLow = format.ComponentFormat == ImageComponentFormats::A2B10G10R10;

            r = redLow ? low : high;
            g = mid;
            b = redLow ? high : low;
            break;
        }
        case ImageComponentFormats::RGBA16:
        {
            uint16_t halves[3]{};

            std::memcpy(halves, pixels.data() + (index * 8u), sizeof(halves));
            r = math::HalfToFloat(halves[0]);
            g = math::HalfToFloat(halves[1]);
            b = math::HalfToFloat(halves[2]);
            break;
        }
        default:
            break;
        }

        if (encoding == CaptureEncoding::sRGB)
        {
            return math::Float3{ DecodeSrgb(r), DecodeSrgb(g), DecodeSrgb(b) };
        }

        if (encoding == CaptureEncoding::HDR10_PQ)
        {
            r = DecodePq(r);
            g = DecodePq(g);
            b = DecodePq(b);
            Bt2020ToBt709(r, g, b);
        }

        return math::Float3{ r * linear_scale, g * linear_scale, b * linear_scale };
    }

    std::vector<uint8_t> ConvertToSrgb8(std::span<const std::byte> pixels, uint32_t width, uint32_t height, ImageFormat format, CaptureEncoding encoding, float linear_scale)
    {
        const size_t count = size_t{ width } * height;
        std::vector<uint8_t> rgb(count * 3u);

        for (size_t i = 0; i < count; ++i)
        {
            float r = 0.0f;
            float g = 0.0f;
            float b = 0.0f;
            bool encoded = false;

            switch (format.ComponentFormat)
            {
            case ImageComponentFormats::RGBA8:
            case ImageComponentFormats::BGRA8:
            {
                const uint8_t* texel = reinterpret_cast<const uint8_t*>(pixels.data()) + (i * 4u);
                const bool bgra = format.ComponentFormat == ImageComponentFormats::BGRA8;

                if (encoding == CaptureEncoding::sRGB)
                {
                    rgb[(i * 3u) + 0u] = texel[bgra ? 2u : 0u];
                    rgb[(i * 3u) + 1u] = texel[1u];
                    rgb[(i * 3u) + 2u] = texel[bgra ? 0u : 2u];
                    encoded = true;
                }
                else
                {
                    r = texel[bgra ? 2u : 0u] / 255.0f;
                    g = texel[1u] / 255.0f;
                    b = texel[bgra ? 0u : 2u] / 255.0f;
                }

                break;
            }
            case ImageComponentFormats::A2B10G10R10:
            case ImageComponentFormats::A2R10G10B10:
            {
                uint32_t word = 0u;

                std::memcpy(&word, pixels.data() + (i * 4u), 4u);

                const float low = static_cast<float>(word & 0x3FFu) / 1023.0f;
                const float mid = static_cast<float>((word >> 10u) & 0x3FFu) / 1023.0f;
                const float high = static_cast<float>((word >> 20u) & 0x3FFu) / 1023.0f;
                // A2B10G10R10 keeps red in the low bits, A2R10G10B10 keeps blue there
                const bool redLow = format.ComponentFormat == ImageComponentFormats::A2B10G10R10;

                r = redLow ? low : high;
                g = mid;
                b = redLow ? high : low;
                break;
            }
            case ImageComponentFormats::RGBA16:
            {
                uint16_t halves[3]{};

                std::memcpy(halves, pixels.data() + (i * 8u), sizeof(halves));
                r = math::HalfToFloat(halves[0]);
                g = math::HalfToFloat(halves[1]);
                b = math::HalfToFloat(halves[2]);
                break;
            }
            default:
                break;
            }

            if (encoded)
            {
                continue;
            }

            if (encoding == CaptureEncoding::HDR10_PQ)
            {
                r = DecodePq(r);
                g = DecodePq(g);
                b = DecodePq(b);
                Bt2020ToBt709(r, g, b);
            }
            else if (encoding == CaptureEncoding::sRGB)
            {
                // packed 10-bit values that are already sRGB encoded
                rgb[(i * 3u) + 0u] = static_cast<uint8_t>(std::lroundf(std::clamp(r, 0.0f, 1.0f) * 255.0f));
                rgb[(i * 3u) + 1u] = static_cast<uint8_t>(std::lroundf(std::clamp(g, 0.0f, 1.0f) * 255.0f));
                rgb[(i * 3u) + 2u] = static_cast<uint8_t>(std::lroundf(std::clamp(b, 0.0f, 1.0f) * 255.0f));
                continue;
            }

            r *= linear_scale;
            g *= linear_scale;
            b *= linear_scale;

            rgb[(i * 3u) + 0u] = EncodeSrgb(r);
            rgb[(i * 3u) + 1u] = EncodeSrgb(g);
            rgb[(i * 3u) + 2u] = EncodeSrgb(b);
        }

        return rgb;
    }

    std::vector<uint8_t> DownsampleToSrgb8(std::span<const std::byte> pixels,
                                           uint32_t width,
                                           uint32_t height,
                                           uint32_t factor,
                                           ImageFormat format,
                                           CaptureEncoding encoding,
                                           float linear_scale,
                                           uint32_t& out_width,
                                           uint32_t& out_height)
    {
        out_width = (width + factor - 1u) / factor;
        out_height = (height + factor - 1u) / factor;

        std::vector<uint8_t> rgb(size_t{ out_width } * out_height * 3u);

        for (int32_t blockY = 0u; std::cmp_less(blockY , out_height); ++blockY)
        {
            for (int32_t blockX = 0u; std::cmp_less(blockX , out_width); ++blockX)
            {
                const uint32_t endX = std::min((blockX + 1u) * factor, width);
                const uint32_t endY = std::min((blockY + 1u) * factor, height);
                std::array<double, 3> sum{};
                uint32_t count = 0u;

                const int32_t startY = static_cast<int32_t>(blockY * factor);
                for (int32_t y = startY; std::cmp_less(y , endY); ++y)
                {
                    const int32_t startX = static_cast<int32_t>(blockX * factor);
                    for (int32_t x = startX; std::cmp_less(x , endX); ++x)
                    {
                        math::Float3 linear = DecodeLinear(pixels, (size_t{ y } * width) + x, format, encoding, linear_scale);

                        // NaN and negative values count as black, as the encode would write them
                        sum[0] += linear[0] > 0.0f ? std::min(linear[0], 1.0f) : 0.0f;
                        sum[1] += linear[1] > 0.0f ? std::min(linear[1], 1.0f) : 0.0f;
                        sum[2] += linear[2] > 0.0f ? std::min(linear[2], 1.0f) : 0.0f;
                        ++count;
                    }
                }

                const size_t out = ((size_t{ blockY } * out_width) + blockX) * 3u;

                rgb[out + 0u] = EncodeSrgb(static_cast<float>(sum[0] / count));
                rgb[out + 1u] = EncodeSrgb(static_cast<float>(sum[1] / count));
                rgb[out + 2u] = EncodeSrgb(static_cast<float>(sum[2] / count));
            }
        }

        return rgb;
    }

    std::string ToHex(std::span<const std::byte> bytes)
    {
        std::string text;

        for (const std::byte value : bytes)
        {
            text += std::format("{:02X}", std::to_integer<uint32_t>(value));
        }

        return text;
    }
}
