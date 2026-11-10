#include "CaptureConversion.hpp"
#include "ColorSpaces.hpp"
#include "FrameCapture.hpp"
#include "ImageDataFormats.hpp"
#include "TransferFunctions.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace
{
    using capture::CaptureEncoding;
    namespace conversion = capture::conversion;

    constexpr ImageFormat k_Rgba8{ ImageComponentFormats::RGBA8, ImageDataType::UNorm };
    constexpr ImageFormat k_Bgra8{ ImageComponentFormats::BGRA8, ImageDataType::UNorm };
    constexpr ImageFormat k_A2B10G10R10{ ImageComponentFormats::A2B10G10R10, ImageDataType::UNorm };
    constexpr ImageFormat k_A2R10G10B10{ ImageComponentFormats::A2R10G10B10, ImageDataType::UNorm };
    constexpr ImageFormat k_Rgba16Float{ ImageComponentFormats::RGBA16, ImageDataType::Float };

    constexpr std::array<ImageFormat, 5> k_CapturedFormats{ k_Rgba8, k_Bgra8, k_A2B10G10R10, k_A2R10G10B10, k_Rgba16Float };
    constexpr std::array<CaptureEncoding, 3> k_Encodings{ CaptureEncoding::sRGB, CaptureEncoding::LinearExtendedSRGB, CaptureEncoding::HDR10_PQ };

    // float16 bit patterns, so that the test data does not depend on the converter that it tests
    constexpr uint16_t k_HalfZero = 0x0000u;
    constexpr uint16_t k_HalfQuarter = 0x3400u;
    constexpr uint16_t k_HalfHalf = 0x3800u;
    constexpr uint16_t k_HalfOne = 0x3C00u;
    constexpr uint16_t k_HalfTwo = 0x4000u;
    constexpr uint16_t k_HalfFour = 0x4400u;
    constexpr uint16_t k_HalfMinusOne = 0xBC00u;
    constexpr uint16_t k_HalfMax = 0x7BFFu;
    constexpr uint16_t k_HalfSmallestSubnormal = 0x0001u;
    constexpr uint16_t k_HalfInfinity = 0x7C00u;
    constexpr uint16_t k_HalfNaN = 0x7E00u;

    // a reference code closer than this to a rounding edge (x.5) is not a fair test of a float encode
    constexpr double k_RoundingMargin = 1.0e-3;

    [[nodiscard]] double ReferenceEncoded(double linear)
    {
        return color::SrgbEncode(std::clamp(linear, 0.0, 1.0)) * 255.0;
    }

    [[nodiscard]] int32_t ReferenceCode(double linear)
    {
        return static_cast<int32_t>(std::lround(ReferenceEncoded(linear)));
    }

    [[nodiscard]] double DistanceToRoundingEdge(double linear)
    {
        const double scaled = ReferenceEncoded(linear);

        return std::abs((scaled - std::floor(scaled)) - 0.5);
    }

    [[nodiscard]] std::vector<std::byte> Bytes(std::initializer_list<uint8_t> values)
    {
        std::vector<std::byte> bytes;

        for (const uint8_t value : values)
        {
            bytes.push_back(static_cast<std::byte>(value));
        }

        return bytes;
    }

    [[nodiscard]] std::vector<std::byte> Words(std::initializer_list<uint32_t> values)
    {
        std::vector<std::byte> bytes(values.size() * sizeof(uint32_t));

        std::memcpy(bytes.data(), values.begin(), bytes.size());
        return bytes;
    }

    [[nodiscard]] std::vector<std::byte> Halves(std::initializer_list<uint16_t> values)
    {
        std::vector<std::byte> bytes(values.size() * sizeof(uint16_t));

        std::memcpy(bytes.data(), values.begin(), bytes.size());
        return bytes;
    }

    /** Packs three 10-bit values and an opaque alpha, low bits first */
    [[nodiscard]] uint32_t Pack10(uint32_t low, uint32_t mid, uint32_t high)
    {
        return low | (mid << 10u) | (high << 20u) | (3u << 30u);
    }

    /** The float16 bits of a float that float16 holds exactly: zero, or a normal positive value with at most 11
     *  significant bits. The exponent bias moves from 127 to 15, and the mantissa drops its 13 low bits, which are 0 */
    [[nodiscard]] uint16_t ExactHalf(float value)
    {
        if (value == 0.0f)
        {
            return k_HalfZero;
        }

        constexpr uint32_t k_BiasShift = (127u - 15u) << 10u;

        return static_cast<uint16_t>((std::bit_cast<uint32_t>(value) >> 13u) - k_BiasShift);
    }

    /** Fixed pseudo-random bytes (a linear congruential generator), so that a failure repeats */
    [[nodiscard]] std::vector<std::byte> NoiseBytes(size_t count, uint32_t seed)
    {
        std::vector<std::byte> bytes(count);
        uint32_t state = seed;

        for (std::byte& value : bytes)
        {
            state = (state * 1664525u) + 1013904223u;
            value = static_cast<std::byte>(state >> 24u);
        }

        return bytes;
    }

    [[nodiscard]] std::vector<uint8_t> DecodeThenEncode(std::span<const std::byte> pixels, size_t count, ImageFormat format, CaptureEncoding encoding, float linear_scale)
    {
        std::vector<uint8_t> rgb;

        for (size_t i = 0; i < count; ++i)
        {
            const std::array<float, 3> linear = conversion::DecodeLinear(pixels, i, format, encoding, linear_scale);

            rgb.push_back(conversion::EncodeSrgb(linear[0]));
            rgb.push_back(conversion::EncodeSrgb(linear[1]));
            rgb.push_back(conversion::EncodeSrgb(linear[2]));
        }

        return rgb;
    }

    [[nodiscard]] std::string Describe(ImageFormat format, CaptureEncoding encoding)
    {
        return "format " + std::to_string(static_cast<int32_t>(format.ComponentFormat)) + ", encoding " +
               std::to_string(static_cast<int32_t>(encoding));
    }
}

TEST(CaptureConversion, EncodeSrgbMatchesTheDoubleReference)
{
    constexpr int32_t k_Samples = 1'000'000;
    int32_t skipped = 0;
    int32_t mismatches = 0;
    float firstMismatch = 0.0f;

    for (int32_t i = 0; i <= k_Samples; ++i)
    {
        const float linear = static_cast<float>(i) / static_cast<float>(k_Samples);

        if (DistanceToRoundingEdge(linear) < k_RoundingMargin)
        {
            ++skipped;
            continue;
        }

        if (conversion::EncodeSrgb(linear) != ReferenceCode(linear))
        {
            firstMismatch = mismatches == 0 ? linear : firstMismatch;
            ++mismatches;
        }
    }

    EXPECT_EQ(mismatches, 0) << "first at linear " << firstMismatch;
    EXPECT_LT(skipped, k_Samples / 100);
}

TEST(CaptureConversion, EncodeSrgbClampsAndSendsNaNToBlack)
{
    constexpr float k_Infinity = std::numeric_limits<float>::infinity();

    EXPECT_EQ(conversion::EncodeSrgb(0.0f), 0u);
    EXPECT_EQ(conversion::EncodeSrgb(-0.0f), 0u);
    EXPECT_EQ(conversion::EncodeSrgb(-1.0f), 0u);
    EXPECT_EQ(conversion::EncodeSrgb(-k_Infinity), 0u);
    EXPECT_EQ(conversion::EncodeSrgb(std::numeric_limits<float>::quiet_NaN()), 0u);
    EXPECT_EQ(conversion::EncodeSrgb(1.0f), 255u);
    EXPECT_EQ(conversion::EncodeSrgb(2.0f), 255u);
    EXPECT_EQ(conversion::EncodeSrgb(k_Infinity), 255u);
}

TEST(CaptureConversion, EveryEightBitCodeSurvivesADecodeAndAnEncode)
{
    for (int32_t code = 0; code < 256; ++code)
    {
        const float encoded = static_cast<float>(code) / 255.0f;
        const float linear = conversion::DecodeSrgb(encoded);

        EXPECT_NEAR(linear, color::SrgbDecode(encoded), 1.0e-6) << "code " << code;
        EXPECT_EQ(conversion::EncodeSrgb(linear), code) << "code " << code;
    }
}

TEST(CaptureConversion, DecodePqMatchesTheDoubleReference)
{
    double worst = 0.0;

    for (int32_t code = 0; code <= 1023; ++code)
    {
        const double signal = static_cast<double>(code) / 1023.0;
        const double expected = color::PqDecode(signal) / color::ScRgbReferenceNits;
        const double actual = conversion::DecodePq(static_cast<float>(signal));
        const double error = std::abs(actual - expected) / std::max(expected, 1.0e-3);

        worst = std::max(worst, error);
    }

    // 1e-4 of a value is below 0.03 of an 8-bit code everywhere on the curve
    EXPECT_LT(worst, 1.0e-4);
    EXPECT_EQ(conversion::DecodePq(0.0f), 0.0f);
    EXPECT_NEAR(conversion::DecodePq(1.0f), 125.0f, 1.0e-3f);
    EXPECT_EQ(conversion::DecodePq(-0.5f), 0.0f);
    EXPECT_EQ(conversion::DecodePq(1.5f), conversion::DecodePq(1.0f));
}

TEST(CaptureConversion, Bt2020ToBt709MatchesTheMatrixDerivedFromThePrimaries)
{
    const color::Mat3d reference = color::RgbToRgb(color::Rec2020, color::Rec709, color::ChromaticAdaptation::None);

    // the code writes the matrix with 4 decimals, so each entry can be 5e-5 away
    for (int32_t column = 0; column < 3; ++column)
    {
        std::array<float, 3> rgb{};

        rgb[column] = 1.0f;
        conversion::Bt2020ToBt709(rgb[0], rgb[1], rgb[2]);

        for (int32_t row = 0; row < 3; ++row)
        {
            EXPECT_NEAR(rgb[row], reference.Rows[row][column], 6.0e-5) << "row " << row << ", column " << column;
        }
    }

    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;

    conversion::Bt2020ToBt709(r, g, b);
    EXPECT_NEAR(r, 1.0f, 1.1e-4f);
    EXPECT_NEAR(g, 1.0f, 1.1e-4f);
    EXPECT_NEAR(b, 1.0f, 1.1e-4f);
}

TEST(CaptureConversion, DecodeLinearReadsEachFormatInItsChannelOrder)
{
    const std::array<float, 3> rgba8 = conversion::DecodeLinear(Bytes({ 10u, 20u, 30u, 255u }), 0u, k_Rgba8, CaptureEncoding::LinearExtendedSRGB, 2.0f);
    const std::array<float, 3> bgra8 = conversion::DecodeLinear(Bytes({ 30u, 20u, 10u, 255u }), 0u, k_Bgra8, CaptureEncoding::LinearExtendedSRGB, 2.0f);

    EXPECT_FLOAT_EQ(rgba8[0], 20.0f / 255.0f);
    EXPECT_FLOAT_EQ(rgba8[1], 40.0f / 255.0f);
    EXPECT_FLOAT_EQ(rgba8[2], 60.0f / 255.0f);
    EXPECT_EQ(bgra8, rgba8);

    // A2B10G10R10 keeps red in the low bits, A2R10G10B10 keeps blue there
    const std::array<float, 3> redLow = conversion::DecodeLinear(Words({ Pack10(1023u, 512u, 0u) }), 0u, k_A2B10G10R10, CaptureEncoding::LinearExtendedSRGB, 1.0f);
    const std::array<float, 3> blueLow = conversion::DecodeLinear(Words({ Pack10(0u, 512u, 1023u) }), 0u, k_A2R10G10B10, CaptureEncoding::LinearExtendedSRGB, 1.0f);

    EXPECT_FLOAT_EQ(redLow[0], 1.0f);
    EXPECT_FLOAT_EQ(redLow[1], 512.0f / 1023.0f);
    EXPECT_FLOAT_EQ(redLow[2], 0.0f);
    EXPECT_EQ(blueLow, redLow);

    const std::array<float, 3> halves = conversion::DecodeLinear(Halves({ k_HalfHalf, k_HalfOne, k_HalfTwo, k_HalfOne }), 0u, k_Rgba16Float, CaptureEncoding::LinearExtendedSRGB, 0.5f);

    EXPECT_EQ(halves, (std::array<float, 3>{ 0.25f, 0.5f, 1.0f }));
}

TEST(CaptureConversion, DecodeLinearReadsTheIndexedTexel)
{
    const std::vector<std::byte> pixels = Halves({ k_HalfZero, k_HalfZero, k_HalfZero, k_HalfOne, k_HalfQuarter, k_HalfHalf, k_HalfOne, k_HalfOne });
    const std::array<float, 3> second = conversion::DecodeLinear(pixels, 1u, k_Rgba16Float, CaptureEncoding::LinearExtendedSRGB, 1.0f);

    EXPECT_EQ(second, (std::array<float, 3>{ 0.25f, 0.5f, 1.0f }));
}

TEST(CaptureConversion, DecodeLinearOfAnSrgbSourceUsesTheCurveAndIgnoresTheScale)
{
    const std::array<float, 3> linear = conversion::DecodeLinear(Bytes({ 0u, 128u, 255u, 0u }), 0u, k_Rgba8, CaptureEncoding::sRGB, 3.0f);

    EXPECT_EQ(linear[0], 0.0f);
    EXPECT_NEAR(linear[1], color::SrgbDecode(128.0 / 255.0), 1.0e-6);
    EXPECT_NEAR(linear[2], 1.0f, 1.0e-6f);
}

TEST(CaptureConversion, DecodeLinearOfPqDecodesThenConvertsTheGamut)
{
    const color::Mat3d gamut = color::RgbToRgb(color::Rec2020, color::Rec709, color::ChromaticAdaptation::None);
    const uint32_t code = static_cast<uint32_t>(std::lround(color::PqEncode(203.0) * 1023.0));
    const double value = color::PqDecode(static_cast<double>(code) / 1023.0) / color::ScRgbReferenceNits;

    // gray stays gray: each row of the gamut matrix sums to 1
    const std::array<float, 3> gray = conversion::DecodeLinear(Words({ Pack10(code, code, code) }), 0u, k_A2B10G10R10, CaptureEncoding::HDR10_PQ, 1.0f);

    for (int32_t channel = 0; channel < 3; ++channel)
    {
        EXPECT_NEAR(gray[channel], value, value * 2.0e-4) << "channel " << channel;
    }

    // BT.2020 red alone becomes the first column of the matrix, so the matrix runs after the decode
    const std::array<float, 3> red = conversion::DecodeLinear(Words({ Pack10(code, 0u, 0u) }), 0u, k_A2B10G10R10, CaptureEncoding::HDR10_PQ, 2.0f);

    for (int32_t row = 0; row < 3; ++row)
    {
        const double expected = 2.0 * value * gamut.Rows[row][0];

        EXPECT_NEAR(red[row], expected, (2.0 * value * 1.0e-4) + 1.0e-6) << "row " << row;
    }
}

TEST(CaptureConversion, OnlyTheExpectedPairsAreSupported)
{
    struct SupportRow
    {
        ImageFormat Format;
        std::array<bool, 3> ByEncoding;
    };

    const std::array<SupportRow, 7> table
    {
        SupportRow{ k_Rgba8, { true, true, false } },
        SupportRow{ k_Bgra8, { true, true, false } },
        SupportRow{ k_A2B10G10R10, { true, true, true } },
        SupportRow{ k_A2R10G10B10, { true, true, true } },
        SupportRow{ k_Rgba16Float, { false, true, false } },
        SupportRow{ ImageFormat{ ImageComponentFormats::RGBA16, ImageDataType::UNorm }, { false, false, false } },
        SupportRow{ ImageFormat{ ImageComponentFormats::R32, ImageDataType::Float }, { false, false, false } },
    };

    for (const SupportRow& row : table)
    {
        for (size_t encoding = 0; encoding < k_Encodings.size(); ++encoding)
        {
            EXPECT_EQ(capture::FrameCapture::IsSupported(row.Format, k_Encodings[encoding]), row.ByEncoding[encoding])
                << Describe(row.Format, k_Encodings[encoding]);
        }
    }

    EXPECT_EQ(capture::FrameCapture::RawTexelBytes(ImageFormat{ ImageComponentFormats::R8, ImageDataType::UInt }), 1u);
    EXPECT_EQ(capture::FrameCapture::RawTexelBytes(ImageFormat{ ImageComponentFormats::RG16, ImageDataType::Float }), 4u);
    EXPECT_EQ(capture::FrameCapture::RawTexelBytes(ImageFormat{ ImageComponentFormats::D32, ImageDataType::Float }), 4u);
    EXPECT_EQ(capture::FrameCapture::RawTexelBytes(k_Rgba16Float), 8u);
    EXPECT_EQ(capture::FrameCapture::RawTexelBytes(ImageFormat{ ImageComponentFormats::RGBA32, ImageDataType::Float }), 16u);
    EXPECT_EQ(capture::FrameCapture::RawTexelBytes(ImageFormat{ ImageComponentFormats::RGB8, ImageDataType::UNorm }), 0u);
}

TEST(CaptureConversion, ConvertAgreesWithDecodeThenEncodeOnEverySupportedPair)
{
    // capture_pixel reports both: Srgb8 from ConvertToSrgb8, and LinearRgb from DecodeLinear. They must agree
    constexpr size_t k_Texels = 4096u;
    constexpr float k_Scale = 1.25f;

    for (const ImageFormat format : k_CapturedFormats)
    {
        for (const CaptureEncoding encoding : k_Encodings)
        {
            if (!capture::FrameCapture::IsSupported(format, encoding))
            {
                continue;
            }

            const std::vector<std::byte> pixels = NoiseBytes(k_Texels * conversion::BytesPerPixel(format), 7u);
            const std::vector<uint8_t> converted = conversion::ConvertToSrgb8(pixels, k_Texels, 1u, format, encoding, k_Scale);
            const std::vector<uint8_t> decoded = DecodeThenEncode(pixels, k_Texels, format, encoding, k_Scale);
            size_t mismatches = 0u;

            for (size_t i = 0; i < converted.size(); ++i)
            {
                mismatches += converted[i] != decoded[i] ? 1u : 0u;
            }

            EXPECT_EQ(mismatches, 0u) << Describe(format, encoding);
        }
    }
}

TEST(CaptureConversion, ConvertDropsAlphaAndKeepsTexelOrder)
{
    const std::vector<std::byte> rgba = Bytes({ 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u, 16u });
    const std::vector<std::byte> bgra = Bytes({ 3u, 2u, 1u, 4u, 7u, 6u, 5u, 8u, 11u, 10u, 9u, 12u, 15u, 14u, 13u, 16u });
    const std::vector<uint8_t> expected{ 1u, 2u, 3u, 5u, 6u, 7u, 9u, 10u, 11u, 13u, 14u, 15u };

    EXPECT_EQ(conversion::ConvertToSrgb8(rgba, 2u, 2u, k_Rgba8, CaptureEncoding::sRGB, 1.0f), expected);
    EXPECT_EQ(conversion::ConvertToSrgb8(bgra, 2u, 2u, k_Bgra8, CaptureEncoding::sRGB, 1.0f), expected);
}

TEST(CaptureConversion, ConvertHandlesTheSpecialHalfValues)
{
    const std::vector<std::byte> pixels = Halves({
        k_HalfInfinity, k_HalfNaN, k_HalfMinusOne, k_HalfOne,
        k_HalfMax, k_HalfSmallestSubnormal, k_HalfZero, k_HalfOne,
        k_HalfHalf, k_HalfFour, k_HalfQuarter, k_HalfOne,
    });
    const std::vector<uint8_t> rgb = conversion::ConvertToSrgb8(pixels, 3u, 1u, k_Rgba16Float, CaptureEncoding::LinearExtendedSRGB, 1.0f);
    const std::vector<uint8_t> expected{
        255u, 0u, 0u,
        255u, 0u, 0u,
        static_cast<uint8_t>(ReferenceCode(0.5)), 255u, static_cast<uint8_t>(ReferenceCode(0.25)),
    };

    EXPECT_EQ(rgb, expected);
}

TEST(CaptureConversion, DownsampleAveragesInLinearLight)
{
    // half black and half white: 0.5 in linear light is code 188. A mean of the encoded codes would give 128
    const std::vector<std::byte> pixels = Halves({
        k_HalfZero, k_HalfZero, k_HalfZero, k_HalfOne, k_HalfOne, k_HalfOne, k_HalfOne, k_HalfOne,
        k_HalfOne, k_HalfOne, k_HalfOne, k_HalfOne, k_HalfZero, k_HalfZero, k_HalfZero, k_HalfOne,
    });
    uint32_t width = 0u;
    uint32_t height = 0u;
    const std::vector<uint8_t> rgb = conversion::DownsampleToSrgb8(pixels, 2u, 2u, 2u, k_Rgba16Float, CaptureEncoding::LinearExtendedSRGB, 1.0f, width, height);

    EXPECT_EQ(width, 1u);
    EXPECT_EQ(height, 1u);
    EXPECT_EQ(ReferenceCode(0.5), 188);
    EXPECT_EQ(rgb, (std::vector<uint8_t>{ 188u, 188u, 188u }));
}

TEST(CaptureConversion, DownsampleClampsEachTexelBeforeTheAverage)
{
    // 4.0 counts as 1.0, so the block is 0.25 and not 1.0. This matches the full-size PNG, and is not an HDR average
    const std::vector<std::byte> pixels = Halves({
        k_HalfFour, k_HalfZero, k_HalfZero, k_HalfOne, k_HalfZero, k_HalfZero, k_HalfZero, k_HalfOne,
        k_HalfZero, k_HalfZero, k_HalfZero, k_HalfOne, k_HalfZero, k_HalfZero, k_HalfZero, k_HalfOne,
    });
    uint32_t width = 0u;
    uint32_t height = 0u;
    const std::vector<uint8_t> rgb = conversion::DownsampleToSrgb8(pixels, 2u, 2u, 2u, k_Rgba16Float, CaptureEncoding::LinearExtendedSRGB, 1.0f, width, height);

    EXPECT_EQ(rgb[0], ReferenceCode(0.25));
    EXPECT_EQ(rgb[1], 0u);
}

TEST(CaptureConversion, DownsampleCountsNaNAndNegativeAsBlack)
{
    const std::vector<std::byte> pixels = Halves({
        k_HalfNaN, k_HalfMinusOne, k_HalfZero, k_HalfOne,
        k_HalfOne, k_HalfOne, k_HalfOne, k_HalfOne,
    });
    uint32_t width = 0u;
    uint32_t height = 0u;
    const std::vector<uint8_t> rgb = conversion::DownsampleToSrgb8(pixels, 2u, 1u, 2u, k_Rgba16Float, CaptureEncoding::LinearExtendedSRGB, 1.0f, width, height);

    EXPECT_EQ(rgb, (std::vector<uint8_t>{ 188u, 188u, 188u }));
}

TEST(CaptureConversion, DownsampleEdgeBlocksAverageOnlyTheTexelsTheyHave)
{
    // 5 x 3 at a factor of 2 gives 3 x 2 blocks. Red holds (x + 5y) / 16 and green half of it, both exact in float16
    constexpr uint32_t k_Width = 5u;
    constexpr uint32_t k_Height = 3u;
    constexpr uint32_t k_Factor = 2u;
    std::vector<uint16_t> halves;

    for (uint32_t y = 0u; y < k_Height; ++y)
    {
        for (uint32_t x = 0u; x < k_Width; ++x)
        {
            const float n = static_cast<float>(x + (k_Width * y));

            halves.insert(halves.end(), { ExactHalf(n / 16.0f), ExactHalf(n / 32.0f), k_HalfZero, k_HalfOne });
        }
    }

    std::vector<std::byte> pixels(halves.size() * sizeof(uint16_t));

    std::memcpy(pixels.data(), halves.data(), pixels.size());

    uint32_t width = 0u;
    uint32_t height = 0u;
    const std::vector<uint8_t> rgb = conversion::DownsampleToSrgb8(pixels, k_Width, k_Height, k_Factor, k_Rgba16Float, CaptureEncoding::LinearExtendedSRGB, 1.0f, width, height);

    ASSERT_EQ(width, 3u);
    ASSERT_EQ(height, 2u);

    for (uint32_t blockY = 0u; blockY < height; ++blockY)
    {
        for (uint32_t blockX = 0u; blockX < width; ++blockX)
        {
            double sum = 0.0;
            uint32_t count = 0u;

            for (uint32_t y = blockY * k_Factor; y < std::min((blockY + 1u) * k_Factor, k_Height); ++y)
            {
                for (uint32_t x = blockX * k_Factor; x < std::min((blockX + 1u) * k_Factor, k_Width); ++x)
                {
                    sum += static_cast<double>(x + (k_Width * y)) / 16.0;
                    ++count;
                }
            }

            const double red = std::min(sum / count, 1.0);
            const double green = std::min(sum / count / 2.0, 1.0);
            const size_t out = ((size_t{ blockY } * width) + blockX) * 3u;

            ASSERT_GT(DistanceToRoundingEdge(red), k_RoundingMargin) << "block " << blockX << ", " << blockY;
            ASSERT_GT(DistanceToRoundingEdge(green), k_RoundingMargin) << "block " << blockX << ", " << blockY;
            EXPECT_EQ(rgb[out + 0u], ReferenceCode(red)) << "block " << blockX << ", " << blockY << " (" << count << " texels)";
            EXPECT_EQ(rgb[out + 1u], ReferenceCode(green)) << "block " << blockX << ", " << blockY << " (" << count << " texels)";
            EXPECT_EQ(rgb[out + 2u], 0u);
        }
    }
}

TEST(CaptureConversion, DownsampleByOneEqualsTheFullConversion)
{
    // FrameCapture takes ConvertToSrgb8 for a factor of 1. Both paths must write the same bytes
    constexpr uint32_t k_Width = 64u;
    constexpr uint32_t k_Height = 64u;
    constexpr float k_Scale = 1.25f;

    for (const ImageFormat format : k_CapturedFormats)
    {
        for (const CaptureEncoding encoding : k_Encodings)
        {
            if (!capture::FrameCapture::IsSupported(format, encoding))
            {
                continue;
            }

            const std::vector<std::byte> pixels = NoiseBytes(size_t{ k_Width } * k_Height * conversion::BytesPerPixel(format), 11u);
            uint32_t width = 0u;
            uint32_t height = 0u;
            const std::vector<uint8_t> downsampled = conversion::DownsampleToSrgb8(pixels, k_Width, k_Height, 1u, format, encoding, k_Scale, width, height);
            const std::vector<uint8_t> converted = conversion::ConvertToSrgb8(pixels, k_Width, k_Height, format, encoding, k_Scale);
            size_t mismatches = 0u;

            for (size_t i = 0; i < converted.size(); ++i)
            {
                mismatches += converted[i] != downsampled[i] ? 1u : 0u;
            }

            EXPECT_EQ(mismatches, 0u) << Describe(format, encoding);
        }
    }
}

TEST(CaptureConversion, ToHexWritesBytesInMemoryOrder)
{
    EXPECT_EQ(conversion::ToHex(Bytes({ 0x00u, 0xABu, 0x10u, 0xFFu })), "00AB10FF");
    EXPECT_EQ(conversion::ToHex(Halves({ k_HalfOne })), "003C");
    EXPECT_EQ(conversion::ToHex({}), "");
}

TEST(CaptureSchedule, DueOnEveryStepUntilTheCount)
{
    const capture::CaptureSchedule schedule{ .First = 5u, .Every = 3u, .Count = 4u };
    std::vector<uint64_t> due;

    for (uint64_t frame = 0u; frame < 30u; ++frame)
    {
        if (schedule.Due(frame))
        {
            due.push_back(frame);
        }
    }

    EXPECT_EQ(due, (std::vector<uint64_t>{ 5u, 8u, 11u, 14u }));
    EXPECT_EQ(schedule.LastFrame(), 14u);
}

TEST(CaptureSchedule, ZeroEveryActsAsOneAndZeroCountIsNeverDue)
{
    const capture::CaptureSchedule everyFrame{ .First = 2u, .Every = 0u, .Count = 3u };
    const capture::CaptureSchedule never{ .First = 2u, .Every = 1u, .Count = 0u };

    EXPECT_FALSE(everyFrame.Due(1u));
    EXPECT_TRUE(everyFrame.Due(2u));
    EXPECT_TRUE(everyFrame.Due(4u));
    EXPECT_FALSE(everyFrame.Due(5u));
    EXPECT_EQ(everyFrame.LastFrame(), 4u);
    EXPECT_FALSE(never.Due(2u));
    EXPECT_EQ(never.LastFrame(), 2u);
}
