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
#include <ranges>
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
    [[nodiscard]] size_t RoundUpToLanes(size_t count) noexcept;
    [[nodiscard]] math::Vector LoadLanes(const float* lanes) noexcept;
    void StoreLanes(float* lanes, math::Vector value) noexcept;

    constexpr float k_PqM1 = 0.1593017578125f;
    constexpr float k_PqM2 = 78.84375f;
    constexpr float k_PqC1 = 0.8359375f;
    constexpr float k_PqC2 = 18.8515625f;
    constexpr float k_PqC3 = 18.6875f;

    // Each stage runs over a whole chunk so that the std::pow calls in a stage do not depend on each other. The
    // CPU overlaps independent calls, and one texel at a time it cannot: a PQ convert was 24% slower that way.
    constexpr size_t k_ChunkTexels = 64u;

    struct TexelChunk
    {
        std::array<float, k_ChunkTexels> Red;
        std::array<float, k_ChunkTexels> Green;
        std::array<float, k_ChunkTexels> Blue;
    };

    void DecodeSrgbRun(std::array<float, k_ChunkTexels>& values, size_t count) noexcept;
    void DecodePqRun(std::array<float, k_ChunkTexels>& values, size_t count) noexcept;
    void ScaleRun(std::array<float, k_ChunkTexels>& values, size_t count, float scale) noexcept;
    void ConvertBt2020ToBt709Run(TexelChunk& chunk, size_t count) noexcept;
    void EncodeChannel(const std::array<float, k_ChunkTexels>& linear, size_t count, std::span<uint8_t> out, size_t channel) noexcept;
    void EncodeChunk(const TexelChunk& chunk, size_t count, std::span<uint8_t> out) noexcept;
    void QuantizeChunk(const TexelChunk& chunk, size_t count, std::span<uint8_t> out) noexcept;
    void AccumulateClamped(const TexelChunk& chunk, size_t first, size_t count, uint32_t factor, std::span<double> sums) noexcept;

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

    template<TexelLayout Layout>
    void LoadChunk(std::span<const std::byte> pixels, size_t first, size_t count, TexelChunk& chunk) noexcept
    {
        for (size_t i = 0; i < count; ++i)
        {
            const Rgb texel = LoadTexel<Layout>(pixels, first + i);

            chunk.Red[i] = texel.Red;
            chunk.Green[i] = texel.Green;
            chunk.Blue[i] = texel.Blue;
        }
    }

    template<CaptureEncoding Encoding>
    void LinearizeChunk(TexelChunk& chunk, size_t count, float linear_scale) noexcept
    {
        if constexpr (Encoding == CaptureEncoding::sRGB)
        {
            DecodeSrgbRun(chunk.Red, count);
            DecodeSrgbRun(chunk.Green, count);
            DecodeSrgbRun(chunk.Blue, count);
        }
        else
        {
            if constexpr (Encoding == CaptureEncoding::HDR10_PQ)
            {
                DecodePqRun(chunk.Red, count);
                DecodePqRun(chunk.Green, count);
                DecodePqRun(chunk.Blue, count);
                ConvertBt2020ToBt709Run(chunk, count);
            }

            ScaleRun(chunk.Red, count, linear_scale);
            ScaleRun(chunk.Green, count, linear_scale);
            ScaleRun(chunk.Blue, count, linear_scale);
        }
    }

    template<TexelLayout Layout, CaptureEncoding Encoding>
    void ConvertTexels(std::span<const std::byte> pixels, std::span<uint8_t> rgb, float linear_scale) noexcept
    {
        const size_t count = rgb.size() / 3u;

        if constexpr (Encoding == CaptureEncoding::sRGB && (Layout == TexelLayout::Rgba8 || Layout == TexelLayout::Bgra8))
        {
            for (size_t i = 0; i < count; ++i)
            {
                const std::span<uint8_t, 3> out = rgb.subspan(i * 3u).first<3>();
                const std::array<uint8_t, 3> codes = LoadCodes8<Layout>(pixels, i);

                out[0] = codes[0];
                out[1] = codes[1];
                out[2] = codes[2];
            }
        }
        else
        {
            TexelChunk chunk{};

            for (size_t first = 0; first < count; first += k_ChunkTexels)
            {
                const size_t chunkCount = std::min(k_ChunkTexels, count - first);
                const std::span<uint8_t> out = rgb.subspan(first * 3u, chunkCount * 3u);

                LoadChunk<Layout>(pixels, first, chunkCount, chunk);

                if constexpr (Encoding == CaptureEncoding::sRGB)
                {
                    // packed 10-bit and half float values that are already sRGB encoded
                    QuantizeChunk(chunk, chunkCount, out);
                }
                else
                {
                    LinearizeChunk<Encoding>(chunk, chunkCount, linear_scale);
                    EncodeChunk(chunk, chunkCount, out);
                }
            }
        }
    }

    // Decodes each source row of a block row in chunks and adds it to one running sum per output column. Every
    // block then sums its texels in row-major order, as a per-block loop would.
    template<TexelLayout Layout, CaptureEncoding Encoding>
    void AverageBlocks(std::span<const std::byte> pixels, uint32_t width, uint32_t height, uint32_t factor,
                       uint32_t out_width, uint32_t out_height, float linear_scale, std::span<uint8_t> rgb)
    {
        std::vector<double> sums(size_t{ out_width } * 3u);
        TexelChunk chunk{};

        for (uint32_t blockY = 0u; blockY < out_height; ++blockY)
        {
            const uint32_t startY = blockY * factor;
            const uint32_t endY = std::min(startY + factor, height);
            const double blockHeight = static_cast<double>(endY - startY);

            std::ranges::fill(sums, 0.0);

            for (uint32_t y = startY; y < endY; ++y)
            {
                const std::span<const std::byte> row = pixels.subspan(size_t{ y } * width * BytesPerTexel<Layout>());

                for (size_t first = 0; first < width; first += k_ChunkTexels)
                {
                    const size_t chunkCount = std::min(k_ChunkTexels, size_t{ width } - first);

                    LoadChunk<Layout>(row, first, chunkCount, chunk);
                    LinearizeChunk<Encoding>(chunk, chunkCount, linear_scale);
                    AccumulateClamped(chunk, first, chunkCount, factor, sums);
                }
            }

            for (size_t firstBlock = 0; firstBlock < out_width; firstBlock += k_ChunkTexels)
            {
                const size_t chunkCount = std::min(k_ChunkTexels, size_t{ out_width } - firstBlock);

                for (size_t i = 0; i < chunkCount; ++i)
                {
                    const size_t blockX = firstBlock + i;
                    const uint32_t blockWidth = std::min(factor, width - static_cast<uint32_t>(blockX) * factor);
                    const double count = static_cast<double>(blockWidth) * blockHeight;

                    chunk.Red[i] = static_cast<float>(sums[(blockX * 3u) + 0u] / count);
                    chunk.Green[i] = static_cast<float>(sums[(blockX * 3u) + 1u] / count);
                    chunk.Blue[i] = static_cast<float>(sums[(blockX * 3u) + 2u] / count);
                }

                const size_t outFirst = ((size_t{ blockY } * out_width) + firstBlock) * 3u;

                EncodeChunk(chunk, chunkCount, rgb.subspan(outFirst, chunkCount * 3u));
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
        // one value goes through the chunk stage, so that it agrees bit for bit with ConvertToSrgb8
        std::array<float, k_ChunkTexels> lanes{};
        std::array<uint8_t, 3> code{};

        lanes[0] = linear;
        EncodeChannel(lanes, 1u, code, 0u);

        return code[0];
    }

    float DecodeSrgb(float encoded) noexcept
    {
        const float value = std::clamp(encoded, 0.0f, 1.0f);

        return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
    }

    float DecodePq(float encoded) noexcept
    {
        const float p = std::pow(std::clamp(encoded, 0.0f, 1.0f), 1.0f / k_PqM2);
        const float nits = 10000.0f * std::pow(std::max(p - k_PqC1, 0.0f) / (k_PqC2 - (k_PqC3 * p)), 1.0f / k_PqM1);

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
        // one texel goes through the chunk stages, so that it agrees bit for bit with ConvertToSrgb8
        TexelChunk chunk{};

        DispatchTexelLayout(ClassifyLayout(format), [&]<TexelLayout Layout>()
        {
            DispatchEncoding(encoding, [&]<CaptureEncoding Encoding>()
            {
                LoadChunk<Layout>(pixels, index, 1u, chunk);
                LinearizeChunk<Encoding>(chunk, 1u, linear_scale);
            });
        });

        return math::Float3{ chunk.Red[0], chunk.Green[0], chunk.Blue[0] };
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

    size_t RoundUpToLanes(size_t count) noexcept
    {
        return (count + 3u) & ~size_t{ 3u };
    }

    math::Vector LoadLanes(const float* lanes) noexcept
    {
        return math::Vector{ _mm_loadu_ps(lanes) };
    }

    void StoreLanes(float* lanes, math::Vector value) noexcept
    {
        _mm_storeu_ps(lanes, value.Data());
    }

    // The run functions work on whole four-lane groups, so they touch up to three lanes past count. Those lanes are
    // inside the chunk arrays and hold finite values, and nothing reads them.
    void DecodeSrgbRun(std::array<float, k_ChunkTexels>& values, size_t count) noexcept
    {
        const size_t lanes = RoundUpToLanes(count);
        const math::Vector zero = math::Vector::Replicate(0.0f);
        const math::Vector one = math::Vector::Replicate(1.0f);

        for (size_t i = 0; i < lanes; i += 4u)
        {
            const math::Vector encoded = LoadLanes(&values[i]).Clamp(zero, one);
            const math::Vector curved = ((encoded + math::Vector::Replicate(0.055f)) / 1.055f).Pow(2.4f);
            const math::Vector linearPart = encoded / 12.92f;
            const math::VectorMask isLinearPart = encoded.CompareLessOrEqual(math::Vector::Replicate(0.04045f));

            StoreLanes(&values[i], math::Select(isLinearPart, curved, linearPart));
        }
    }

    void DecodePqRun(std::array<float, k_ChunkTexels>& values, size_t count) noexcept
    {
        const size_t lanes = RoundUpToLanes(count);
        const math::Vector zero = math::Vector::Replicate(0.0f);
        const math::Vector one = math::Vector::Replicate(1.0f);

        for (size_t i = 0; i < lanes; i += 4u)
        {
            const math::Vector power = LoadLanes(&values[i]).Clamp(zero, one).Pow(1.0f / k_PqM2);
            const math::Vector numerator = (power - math::Vector::Replicate(k_PqC1)).Max(zero);
            const math::Vector denominator = math::Vector::Replicate(k_PqC2) - (power * k_PqC3);
            const math::Vector nits = (numerator / denominator).Pow(1.0f / k_PqM1) * 10000.0f;

            StoreLanes(&values[i], nits / 80.0f);
        }
    }

    void ScaleRun(std::array<float, k_ChunkTexels>& values, size_t count, float scale) noexcept
    {
        for (size_t i = 0; i < count; ++i)
        {
            values[i] *= scale;
        }
    }

    void ConvertBt2020ToBt709Run(TexelChunk& chunk, size_t count) noexcept
    {
        for (size_t i = 0; i < count; ++i)
        {
            capture::conversion::Bt2020ToBt709(chunk.Red[i], chunk.Green[i], chunk.Blue[i]);
        }
    }

    void EncodeChannel(const std::array<float, k_ChunkTexels>& linear, size_t count, std::span<uint8_t> out, size_t channel) noexcept
    {
        std::array<float, k_ChunkTexels> clamped{};
        std::array<float, k_ChunkTexels> powered{};
        const size_t lanes = RoundUpToLanes(count);

        for (size_t i = 0; i < count; ++i)
        {
            clamped[i] = ClampUnit(linear[i]);
        }

        for (size_t i = 0; i < lanes; i += 4u)
        {
            StoreLanes(&powered[i], LoadLanes(&clamped[i]).Pow(1.0f / 2.4f));
        }

        for (size_t i = 0; i < count; ++i)
        {
            const float encoded = clamped[i] <= 0.0031308f ? 12.92f * clamped[i] : (1.055f * powered[i]) - 0.055f;

            out[(i * 3u) + channel] = static_cast<uint8_t>((encoded * 255.0f) + 0.5f);
        }
    }

    void EncodeChunk(const TexelChunk& chunk, size_t count, std::span<uint8_t> out) noexcept
    {
        EncodeChannel(chunk.Red, count, out, 0u);
        EncodeChannel(chunk.Green, count, out, 1u);
        EncodeChannel(chunk.Blue, count, out, 2u);
    }

    void QuantizeChunk(const TexelChunk& chunk, size_t count, std::span<uint8_t> out) noexcept
    {
        for (size_t i = 0; i < count; ++i)
        {
            out[(i * 3u) + 0u] = QuantizeEncodedValue(chunk.Red[i]);
            out[(i * 3u) + 1u] = QuantizeEncodedValue(chunk.Green[i]);
            out[(i * 3u) + 2u] = QuantizeEncodedValue(chunk.Blue[i]);
        }
    }

    void AccumulateClamped(const TexelChunk& chunk, size_t first, size_t count, uint32_t factor, std::span<double> sums) noexcept
    {
        for (size_t i = 0; i < count; ++i)
        {
            const size_t blockX = (first + i) / factor;

            // NaN and negative values count as black, as the encode would write them
            sums[(blockX * 3u) + 0u] += ClampUnit(chunk.Red[i]);
            sums[(blockX * 3u) + 1u] += ClampUnit(chunk.Green[i]);
            sums[(blockX * 3u) + 2u] += ClampUnit(chunk.Blue[i]);
        }
    }
}
