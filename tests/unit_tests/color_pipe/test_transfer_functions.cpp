#include "TransferFunctions.hpp"
#include <gtest/gtest.h>
#include <cmath>
#include <cstdint>

namespace
{
    constexpr int32_t k_SampleCount = 1000;

    using Function = double (*)(double) noexcept;

    // the identity bound scales with the domain, so that a 10000 nit peak has an absolute tolerance of 1e-8
    void ExpectRoundTripAndMonotonic(Function encode, Function decode, double domain_max)
    {
        double previousEncoded = -1.0;

        for (int32_t sampleIndex = 0; sampleIndex < k_SampleCount; ++sampleIndex)
        {
            const double value = domain_max * static_cast<double>(sampleIndex) / static_cast<double>(k_SampleCount - 1);
            const double encoded = encode(value);
            const double decoded = decode(encoded);

            EXPECT_NEAR(decoded, value, 1.0e-12 * domain_max) << "sample " << sampleIndex;
            EXPECT_GT(encoded, previousEncoded) << "encode is not monotonic at sample " << sampleIndex;
            previousEncoded = encoded;
        }
    }

    void ExpectDecodeMonotonic(Function decode, double signal_max)
    {
        double previousDecoded = -1.0;

        for (int32_t sampleIndex = 0; sampleIndex < k_SampleCount; ++sampleIndex)
        {
            const double signal = signal_max * static_cast<double>(sampleIndex) / static_cast<double>(k_SampleCount - 1);
            const double decoded = decode(signal);

            EXPECT_GT(decoded, previousDecoded) << "decode is not monotonic at sample " << sampleIndex;
            previousDecoded = decoded;
        }
    }
}

TEST(TransferFunctionsTest, SrgbRoundTripAndMonotonic)
{
    ExpectRoundTripAndMonotonic(color::SrgbEncode, color::SrgbDecode, 1.0);
    ExpectDecodeMonotonic(color::SrgbDecode, 1.0);
}

TEST(TransferFunctionsTest, SrgbMatchesTheStandardAtKnownPoints)
{
    EXPECT_NEAR(color::SrgbEncode(0.0), 0.0, 1.0e-15);
    EXPECT_NEAR(color::SrgbEncode(1.0), 1.0, 1.0e-12);
    EXPECT_NEAR(color::SrgbEncode(0.18), 0.4613561, 1.0e-6);
    EXPECT_NEAR(color::SrgbDecode(0.5), 0.2140411, 1.0e-6);
}

TEST(TransferFunctionsTest, SrgbMirrorsNegativeValues)
{
    EXPECT_DOUBLE_EQ(color::SrgbEncode(-0.25), -color::SrgbEncode(0.25));
    EXPECT_DOUBLE_EQ(color::SrgbDecode(-0.25), -color::SrgbDecode(0.25));
}

TEST(TransferFunctionsTest, Gamma22RoundTripAndMonotonic)
{
    ExpectRoundTripAndMonotonic(color::Gamma22Encode, color::Gamma22Decode, 1.0);
    ExpectDecodeMonotonic(color::Gamma22Decode, 1.0);
}

TEST(TransferFunctionsTest, PqRoundTripAndMonotonic)
{
    ExpectRoundTripAndMonotonic(color::PqEncode, color::PqDecode, color::PqPeakNits);
    ExpectDecodeMonotonic(color::PqDecode, 1.0);
}

TEST(TransferFunctionsTest, PqKnownPoints)
{
    EXPECT_NEAR(color::PqEncode(10000.0), 1.0, 1.0e-12);
    // ST 2084 does not map 0 nits to 0: the signal is c1^m2, which is 7.3e-7. PqDecode of it returns 0 nits
    EXPECT_NEAR(color::PqEncode(0.0), 0.0, 1.0e-6);
    EXPECT_NEAR(color::PqEncode(0.0), std::pow(color::PqC1, color::PqM2), 1.0e-15);
    EXPECT_NEAR(color::PqDecode(color::PqEncode(0.0)), 0.0, 1.0e-12);
    EXPECT_EQ(color::PqDecode(0.0), 0.0);
    EXPECT_NEAR(color::PqEncode(100.0), 0.5080784, 1.0e-6);
}

TEST(TransferFunctionsTest, ScRgbRoundTripAndMonotonic)
{
    ExpectRoundTripAndMonotonic(color::ScRgbEncode, color::ScRgbDecode, color::PqPeakNits);
    ExpectDecodeMonotonic(color::ScRgbDecode, 125.0);
}

TEST(TransferFunctionsTest, ScRgbOneIsEightyNits)
{
    EXPECT_DOUBLE_EQ(color::ScRgbEncode(80.0), 1.0);
    EXPECT_DOUBLE_EQ(color::ScRgbDecode(1.0), 80.0);
}
