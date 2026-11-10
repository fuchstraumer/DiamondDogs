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
#include <vector>

namespace
{
    using capture::CaptureEncoding;

    enum class TexelLayout : uint8_t
    {
        Invalid = 0,
        Rgba8,
        Bgra8,
        A2B10G10R10,
        A2R10G10B10,
        Rgba16Half,
    };

    struct Rgb
    {
        float Red;
        float Green;
        float Blue;
    };

    constexpr TexelLayout ClassifyLayout(ImageFormat format) noexcept
    {
        switch (format.ComponentFormat)
        {
        case ImageComponentFormats::RGBA8: return TexelLayout::Rgba8;
        case ImageComponentFormats::BGRA8: return TexelLayout::Bgra8;
        case ImageComponentFormats::A2B10G10R10: return TexelLayout::A2B10G10R10;
        case ImageComponentFormats::A2R10G10B10: return TexelLayout::A2R10G10B10;
        case ImageComponentFormats::RGBA16: return TexelLayout::Rgba16Half;
        default: return TexelLayout::Invalid;
        }
    }

    [[nodiscard]] float ClampUnit(float value) noexcept;
    [[nodiscard]] uint8_t QuantizeEncodedValue(float encoded) noexcept;

    template<TexelLayout Layout>
    [[nodiscard]] constexpr size_t BytesPerTexel() noexcept
    {
        return Layout == TexelLayout::Rgba16Half ? 8u : 4u;
    }

    template<TexelLayout Layout>
    [[nodiscard]] std::array<uint8_t, 3> LoadCodes8(std::span<const std::byte> pixels, size_t index) noexcept
    {
        const std::span<const std::byte, 4> texel = pixels.subspan(index * 4u).first<4>();
        const uint8_t first = std::to_integer<uint8_t>(texel[0]);
        const uint8_t second = std::to_integer<uint8_t>(texel[1]);
        const uint8_t third = std::to_integer<uint8_t>(texel[2]);

        if constexpr (Layout == TexelLayout::Bgra8)
        {
            return { third, second, first };
        }
        else
        {
            return { first, second, third };
        }
    }

    // each layout is decoded to non-linear (or already linear) channel values in [0, 1], except half floats
    template<TexelLayout Layout>
    [[nodiscard]] Rgb LoadTexel(std::span<const std::byte> pixels, size_t index) noexcept
    {
        if constexpr (Layout == TexelLayout::Rgba8 || Layout == TexelLayout::Bgra8)
        {
            const std::array<uint8_t, 3> codes = LoadCodes8<Layout>(pixels, index);

            return Rgb{ codes[0] / 255.0f, codes[1] / 255.0f, codes[2] / 255.0f };
        }
        else if constexpr (Layout == TexelLayout::A2B10G10R10 || Layout == TexelLayout::A2R10G10B10)
        {
            const std::span<const std::byte, 4> texel = pixels.subspan(index * 4u).first<4>();
            uint32_t word = 0u;

            std::memcpy(&word, texel.data(), sizeof(word));

            const float low = static_cast<float>(word & 0x3FFu) / 1023.0f;
            const float mid = static_cast<float>((word >> 10u) & 0x3FFu) / 1023.0f;
            const float high = static_cast<float>((word >> 20u) & 0x3FFu) / 1023.0f;

            // A2B10G10R10 keeps red in the low bits, A2R10G10B10 keeps blue there
            if constexpr (Layout == TexelLayout::A2B10G10R10)
            {
                return Rgb{ low, mid, high };
            }
            else
            {
                return Rgb{ high, mid, low };
            }
        }
        else if constexpr (Layout == TexelLayout::Rgba16Half)
        {
            const std::span<const std::byte, 6> texel = pixels.subspan(index * 8u).first<6>();
            uint16_t halves[3]{};

            std::memcpy(halves, texel.data(), sizeof(halves));

            return Rgb{ math::HalfToFloat(halves[0]), math::HalfToFloat(halves[1]), math::HalfToFloat(halves[2]) };
        }
        else
        {
            return Rgb{ 0.0f, 0.0f, 0.0f };
        }
    }

    // an sRGB source decodes with the sRGB curve and takes no linear scale
    template<CaptureEncoding Encoding>
    [[nodiscard]] Rgb Linearize(Rgb texel, float linear_scale) noexcept
    {
        if constexpr (Encoding == CaptureEncoding::sRGB)
        {
            return Rgb{ capture::conversion::DecodeSrgb(texel.Red),
                        capture::conversion::DecodeSrgb(texel.Green),
                        capture::conversion::DecodeSrgb(texel.Blue) };
        }
        else
        {
            if constexpr (Encoding == CaptureEncoding::HDR10_PQ)
            {
                texel.Red = capture::conversion::DecodePq(texel.Red);
                texel.Green = capture::conversion::DecodePq(texel.Green);
                texel.Blue = capture::conversion::DecodePq(texel.Blue);
                capture::conversion::Bt2020ToBt709(texel.Red, texel.Green, texel.Blue);
            }

            return Rgb{ texel.Red * linear_scale, texel.Green * linear_scale, texel.Blue * linear_scale };
        }
    }

    template<typename Callable>
    auto DispatchTexelLayout(TexelLayout layout, Callable&& callable)
    {
        switch (layout)
        {
        case TexelLayout::Rgba8:
            return callable.template operator()<TexelLayout::Rgba8>();
        case TexelLayout::Bgra8:
            return callable.template operator()<TexelLayout::Bgra8>();
        case TexelLayout::A2B10G10R10:
            return callable.template operator()<TexelLayout::A2B10G10R10>();
        case TexelLayout::A2R10G10B10:
            return callable.template operator()<TexelLayout::A2R10G10B10>();
        case TexelLayout::Rgba16Half:
            return callable.template operator()<TexelLayout::Rgba16Half>();
        default:
            return callable.template operator()<TexelLayout::Invalid>();
        }
    }

    template<typename Callable>
    auto DispatchEncoding(CaptureEncoding encoding, Callable&& callable)
    {
        switch (encoding)
        {
        case CaptureEncoding::sRGB:
            return callable.template operator()<CaptureEncoding::sRGB>();
        case CaptureEncoding::HDR10_PQ:
            return callable.template operator()<CaptureEncoding::HDR10_PQ>();
        default:
            return callable.template operator()<CaptureEncoding::LinearExtendedSRGB>();
        }
    }

    template<TexelLayout Layout, CaptureEncoding Encoding>
    void ConvertTexels(std::span<const std::byte> pixels, std::span<uint8_t> rgb, float linear_scale) noexcept
    {
        const size_t count = rgb.size() / 3u;

        for (size_t i = 0; i < count; ++i)
        {
            const std::span<uint8_t, 3> out = rgb.subspan(i * 3u).first<3>();

            if constexpr (Encoding == CaptureEncoding::sRGB && (Layout == TexelLayout::Rgba8 || Layout == TexelLayout::Bgra8))
            {
                const std::array<uint8_t, 3> codes = LoadCodes8<Layout>(pixels, i);

                out[0] = codes[0];
                out[1] = codes[1];
                out[2] = codes[2];
            }
            else if constexpr (Encoding == CaptureEncoding::sRGB)
            {
                // packed 10-bit and half float values that are already sRGB encoded
                const Rgb texel = LoadTexel<Layout>(pixels, i);

                out[0] = QuantizeEncodedValue(texel.Red);
                out[1] = QuantizeEncodedValue(texel.Green);
                out[2] = QuantizeEncodedValue(texel.Blue);
            }
            else
            {
                const Rgb linear = Linearize<Encoding>(LoadTexel<Layout>(pixels, i), linear_scale);

                out[0] = capture::conversion::EncodeSrgb(linear.Red);
                out[1] = capture::conversion::EncodeSrgb(linear.Green);
                out[2] = capture::conversion::EncodeSrgb(linear.Blue);
            }
        }
    }

    template<TexelLayout Layout, CaptureEncoding Encoding>
    void AverageBlocks(std::span<const std::byte> pixels, uint32_t width, uint32_t height, uint32_t factor,
                       uint32_t out_width, uint32_t out_height, float linear_scale, std::span<uint8_t> rgb) noexcept
    {
        for (uint32_t blockY = 0u; blockY < out_height; ++blockY)
        {
            const uint32_t startY = blockY * factor;
            const uint32_t endY = std::min(startY + factor, height);

            for (uint32_t blockX = 0u; blockX < out_width; ++blockX)
            {
                const uint32_t startX = blockX * factor;
                const uint32_t endX = std::min(startX + factor, width);
                std::array<double, 3> sum{};

                for (uint32_t y = startY; y < endY; ++y)
                {
                    const std::span<const std::byte> row = pixels.subspan(size_t{ y } * width * BytesPerTexel<Layout>());

                    for (uint32_t x = startX; x < endX; ++x)
                    {
                        const Rgb linear = Linearize<Encoding>(LoadTexel<Layout>(row, x), linear_scale);

                        // NaN and negative values count as black, as the encode would write them
                        sum[0] += ClampUnit(linear.Red);
                        sum[1] += ClampUnit(linear.Green);
                        sum[2] += ClampUnit(linear.Blue);
                    }
                }

                const double count = static_cast<double>(endX - startX) * (endY - startY);
                const std::span<uint8_t, 3> out = rgb.subspan(((size_t{ blockY } * out_width) + blockX) * 3u).first<3>();

                out[0] = capture::conversion::EncodeSrgb(static_cast<float>(sum[0] / count));
                out[1] = capture::conversion::EncodeSrgb(static_cast<float>(sum[1] / count));
                out[2] = capture::conversion::EncodeSrgb(static_cast<float>(sum[2] / count));
            }
        }
    }
}

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
        return DispatchTexelLayout(ClassifyLayout(format), [&]<TexelLayout Layout>()
        {
            return DispatchEncoding(encoding, [&]<CaptureEncoding Encoding>()
            {
                const Rgb linear = Linearize<Encoding>(LoadTexel<Layout>(pixels, index), linear_scale);

                return math::Float3{ linear.Red, linear.Green, linear.Blue };
            });
        });
    }

    std::vector<uint8_t> ConvertToSrgb8(std::span<const std::byte> pixels, uint32_t width, uint32_t height, ImageFormat format, CaptureEncoding encoding, float linear_scale)
    {
        std::vector<uint8_t> rgb(size_t{ width } * height * 3u);

        DispatchTexelLayout(ClassifyLayout(format), [&]<TexelLayout Layout>()
        {
            DispatchEncoding(encoding, [&]<CaptureEncoding Encoding>()
            {
                ConvertTexels<Layout, Encoding>(pixels, rgb, linear_scale);
            });
        });

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

        DispatchTexelLayout(ClassifyLayout(format), [&]<TexelLayout Layout>()
        {
            DispatchEncoding(encoding, [&]<CaptureEncoding Encoding>()
            {
                AverageBlocks<Layout, Encoding>(pixels, width, height, factor, out_width, out_height, linear_scale, rgb);
            });
        });

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

namespace
{

    float ClampUnit(float value) noexcept
    {
        return value > 0.0f ? std::min(value, 1.0f) : 0.0f;
    }

    uint8_t QuantizeEncodedValue(float encoded) noexcept
    {
        return static_cast<uint8_t>(std::lroundf(ClampUnit(encoded) * 255.0f));
    }
}
