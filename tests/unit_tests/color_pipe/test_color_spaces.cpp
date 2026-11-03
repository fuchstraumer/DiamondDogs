#include "ColorSpaces.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace
{
    struct NamedGamut
    {
        std::string_view Name;
        const color::Gamut* Gamut;
    };

    const std::array<NamedGamut, 4> k_AllGamuts
    {
        NamedGamut{ "Rec709", &color::Rec709 },
        NamedGamut{ "Rec2020", &color::Rec2020 },
        NamedGamut{ "AP1", &color::AP1 },
        NamedGamut{ "DisplayP3", &color::DisplayP3 },
    };

    const std::array<color::ChromaticAdaptation, 3> k_Adaptations
    {
        color::ChromaticAdaptation::None,
        color::ChromaticAdaptation::Bradford,
        color::ChromaticAdaptation::Cat02,
    };

    double MaxDeviationFromIdentity(const color::Mat3d& matrix)
    {
        double worst = 0.0;

        for (int32_t row = 0; row < 3; ++row)
        {
            for (int32_t column = 0; column < 3; ++column)
            {
                const double expected = row == column ? 1.0 : 0.0;
                worst = std::max(worst, std::fabs(matrix.Rows[row][column] - expected));
            }
        }

        return worst;
    }

    void ExpectWhiteMapsToWhite(color::ChromaticAdaptation adaptation)
    {
        const color::Mat3d toAp1 = color::RgbToRgb(color::Rec709, color::AP1, adaptation);
        const color::Vec3d white = color::Multiply(toAp1, color::Vec3d{ 1.0, 1.0, 1.0 });

        EXPECT_NEAR(white.X, 1.0, 1.0e-9);
        EXPECT_NEAR(white.Y, 1.0, 1.0e-9);
        EXPECT_NEAR(white.Z, 1.0, 1.0e-9);
    }
}

TEST(ColorSpacesTest, RgbToRgbRoundTripIsIdentity)
{
    for (const NamedGamut& source : k_AllGamuts)
    {
        for (const NamedGamut& destination : k_AllGamuts)
        {
            for (const color::ChromaticAdaptation adaptation : k_Adaptations)
            {
                const color::Mat3d forward = color::RgbToRgb(*source.Gamut, *destination.Gamut, adaptation);
                const color::Mat3d back = color::RgbToRgb(*destination.Gamut, *source.Gamut, adaptation);
                const double deviation = MaxDeviationFromIdentity(color::Multiply(back, forward));

                EXPECT_LT(deviation, 1.0e-12) << source.Name << " to " << destination.Name << ", adaptation " << static_cast<int32_t>(adaptation);
            }
        }
    }
}

TEST(ColorSpacesTest, RgbToXyzMapsWhiteToTheWhitePointAtUnitLuminance)
{
    for (const NamedGamut& named : k_AllGamuts)
    {
        const color::Vec3d xyz = color::Multiply(color::RgbToXyz(*named.Gamut), color::Vec3d{ 1.0, 1.0, 1.0 });
        const color::Vec3d expected = color::XyToXyz(named.Gamut->White);

        EXPECT_NEAR(xyz.X, expected.X, 1.0e-12) << named.Name;
        EXPECT_NEAR(xyz.Y, 1.0, 1.0e-12) << named.Name;
        EXPECT_NEAR(xyz.Z, expected.Z, 1.0e-12) << named.Name;
    }
}

TEST(ColorSpacesTest, XyzToRgbInvertsRgbToXyz)
{
    for (const NamedGamut& named : k_AllGamuts)
    {
        const color::Mat3d product = color::Multiply(color::XyzToRgb(*named.Gamut), color::RgbToXyz(*named.Gamut));

        EXPECT_LT(MaxDeviationFromIdentity(product), 1.0e-12) << named.Name;
    }
}

// each tolerance is the rounding of the last published digit
TEST(ColorSpacesTest, LuminanceWeightsMatchPublishedValues)
{
    const color::Vec3d red{ 1.0, 0.0, 0.0 };
    const color::Vec3d green{ 0.0, 1.0, 0.0 };
    const color::Vec3d blue{ 0.0, 0.0, 1.0 };

    EXPECT_NEAR(color::Luminance(color::Rec709, red), 0.2126, 1.0e-4);
    EXPECT_NEAR(color::Luminance(color::Rec709, green), 0.7152, 1.0e-4);
    EXPECT_NEAR(color::Luminance(color::Rec709, blue), 0.0722, 1.0e-4);

    EXPECT_NEAR(color::Luminance(color::Rec2020, red), 0.2627, 1.0e-5);
    EXPECT_NEAR(color::Luminance(color::Rec2020, green), 0.6780, 1.0e-5);
    EXPECT_NEAR(color::Luminance(color::Rec2020, blue), 0.0593, 1.0e-5);

    EXPECT_NEAR(color::Luminance(color::AP1, red), 0.2722287, 1.0e-6);
    EXPECT_NEAR(color::Luminance(color::AP1, green), 0.6740818, 1.0e-6);
    EXPECT_NEAR(color::Luminance(color::AP1, blue), 0.0536895, 1.0e-6);
}

TEST(ColorSpacesTest, Ap1WeightsConstantMatchesTheDerivedRow)
{
    EXPECT_NEAR(color::Luminance(color::AP1, color::Vec3d{ 1.0, 0.0, 0.0 }), color::Ap1LuminanceWeights.X, 1.0e-6);
    EXPECT_NEAR(color::Luminance(color::AP1, color::Vec3d{ 0.0, 1.0, 0.0 }), color::Ap1LuminanceWeights.Y, 1.0e-6);
    EXPECT_NEAR(color::Luminance(color::AP1, color::Vec3d{ 0.0, 0.0, 1.0 }), color::Ap1LuminanceWeights.Z, 1.0e-6);
}

TEST(ColorSpacesTest, BradfordMapsSrgbWhiteToAp1White)
{
    ExpectWhiteMapsToWhite(color::ChromaticAdaptation::Bradford);
}

TEST(ColorSpacesTest, Cat02MapsSrgbWhiteToAp1White)
{
    ExpectWhiteMapsToWhite(color::ChromaticAdaptation::Cat02);
}

TEST(ColorSpacesTest, NoAdaptationLeavesSrgbWhiteOffAp1White)
{
    const color::Mat3d toAp1 = color::RgbToRgb(color::Rec709, color::AP1, color::ChromaticAdaptation::None);
    const color::Vec3d white = color::Multiply(toAp1, color::Vec3d{ 1.0, 1.0, 1.0 });

    EXPECT_GT(std::fabs(white.X - white.Z), 1.0e-3);
}

TEST(ColorSpacesTest, InvalidAdaptationIsVisible)
{
    const color::Mat3d matrix = color::RgbToRgb(color::Rec709, color::AP1, color::ChromaticAdaptation::Invalid);

    EXPECT_TRUE(std::isnan(matrix.Rows[0][0]));
}

TEST(ColorSpacesTest, KelvinAtD65IsNearD65)
{
    const color::Xy xy = color::KelvinToXy(6504.0);

    EXPECT_NEAR(xy.X, color::WhiteD65.X, 0.003);
    // D65 sits about 0.003 above the Planckian locus in CIE 1960 uv, which is about 0.005 in y
    EXPECT_NEAR(xy.Y, color::WhiteD65.Y, 0.006);
}

TEST(ColorSpacesTest, KelvinXMovesMonotonically)
{
    double previousX = color::KelvinToXy(2000.0).X;

    for (double kelvin = 2100.0; kelvin <= 12000.0; kelvin += 100.0)
    {
        const double x = color::KelvinToXy(kelvin).X;

        EXPECT_LT(x, previousX) << kelvin << " K";
        previousX = x;
    }
}

TEST(ColorSpacesTest, KelvinToRgbHasUnitLuminance)
{
    for (const NamedGamut& named : k_AllGamuts)
    {
        for (const double kelvin : { 1500.0, 3200.0, 6504.0, 12000.0 })
        {
            const color::Vec3d rgb = color::KelvinToRgb(*named.Gamut, kelvin);

            EXPECT_NEAR(color::Luminance(*named.Gamut, rgb), 1.0, 1.0e-12) << named.Name << " " << kelvin << " K";
        }
    }
}
