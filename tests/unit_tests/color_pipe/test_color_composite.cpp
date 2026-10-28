#include "ColorComposite.hpp"
#include "ColorSpaces.hpp"
#include "DisplayTarget.hpp"
#include "TransferFunctions.hpp"
#include <gtest/gtest.h>
#include <cmath>
#include <cstdint>

namespace
{
    // the LG: paper white 348, peak 600, black 0.035
    color::DisplayTarget LgTarget(color::DisplayEncoding encoding)
    {
        return color::DisplayTarget{ 600.0f, 0.035f, 348.0f, color::PrimariesOf(encoding), encoding };
    }

    color::DisplayTarget SdrTarget()
    {
        return color::DisplayTarget{ 348.0f, 0.035f, 348.0f, color::Rec709, color::DisplayEncoding::SrgbNonlinear };
    }

    constexpr color::UiTexel k_Transparent{ 255, 0, 0, 0 };
}

TEST(ColorCompositeTest, ExposureIsAPowerOfTwo)
{
    const color::Vec3d exposed = color::ApplyExposure(color::Vec3d{ 0.18, 0.36, 0.5 }, 3.0);

    EXPECT_DOUBLE_EQ(exposed.X, 0.18 * 8.0);
    EXPECT_DOUBLE_EQ(exposed.Y, 0.36 * 8.0);
    EXPECT_DOUBLE_EQ(exposed.Z, 0.5 * 8.0);
}

TEST(ColorCompositeTest, FalseColorZonesRoundToWholeStops)
{
    EXPECT_EQ(color::FalseColorZoneOf(0.0), 5);
    EXPECT_EQ(color::FalseColorZoneOf(0.49), 5);
    EXPECT_EQ(color::FalseColorZoneOf(0.51), 6);
    EXPECT_EQ(color::FalseColorZoneOf(-0.49), 5);
    EXPECT_EQ(color::FalseColorZoneOf(-0.51), 4);
    EXPECT_EQ(color::FalseColorZoneOf(-5.0), 0);
    EXPECT_EQ(color::FalseColorZoneOf(-40.0), 0);
    EXPECT_EQ(color::FalseColorZoneOf(5.0), 10);
    EXPECT_EQ(color::FalseColorZoneOf(40.0), 10);
}

TEST(ColorCompositeTest, MiddleGrayIsZeroEv)
{
    EXPECT_NEAR(color::FalseColorEvOf(color::k_MiddleGray), 0.0, 1.0e-12);
    EXPECT_NEAR(color::FalseColorEvOf(color::k_MiddleGray * 4.0), 2.0, 1.0e-12);
    EXPECT_TRUE(std::isfinite(color::FalseColorEvOf(0.0)));
    EXPECT_TRUE(std::isfinite(color::FalseColorEvOf(-1.0)));
}

TEST(ColorCompositeTest, PassthroughNeverExceedsTheHeadroom)
{
    const color::DisplayTarget target = LgTarget(color::DisplayEncoding::Hdr10Pq);
    const double headroom = color::HeadroomOf(target);
    const color::Vec3d tonemapped = color::Tonemap(color::TonemapperKind::Passthrough, color::Vec3d{ 50.0, 20.0, 80.0 }, target);

    EXPECT_LE(tonemapped.X, headroom);
    EXPECT_LE(tonemapped.Y, headroom);
    EXPECT_LE(tonemapped.Z, headroom);
    EXPECT_GE(tonemapped.X, 0.0);
}

TEST(ColorCompositeTest, UnwrittenTonemappersGiveNaN)
{
    const color::Vec3d tonemapped = color::Tonemap(color::TonemapperKind::Invalid, color::Vec3d{ 0.18, 0.18, 0.18 }, SdrTarget());

    EXPECT_TRUE(std::isnan(tonemapped.X));
}

TEST(ColorCompositeTest, KhronosNeutralPreservesGrayAndRespectsHeadroom)
{
    const color::DisplayTarget target = SdrTarget();
    const double headroom = color::HeadroomOf(target);
    const color::Vec3d gray = color::Tonemap(color::TonemapperKind::KhronosNeutral, color::Vec3d{ 0.18, 0.18, 0.18 }, target);

    EXPECT_TRUE(std::isfinite(gray.X));
    EXPECT_NEAR(gray.X, gray.Y, 1.0e-9);
    EXPECT_NEAR(gray.Z, gray.Y, 1.0e-9);
    EXPECT_GT(gray.Y, 0.0);
    EXPECT_LE(gray.Y, headroom);

    const color::Vec3d highlight = color::Tonemap(color::TonemapperKind::KhronosNeutral, color::Vec3d{ 100.0, 50.0, 200.0 }, target);
    EXPECT_TRUE(std::isfinite(highlight.X));
    EXPECT_TRUE(std::isfinite(highlight.Y));
    EXPECT_TRUE(std::isfinite(highlight.Z));
    EXPECT_LE(highlight.X, headroom);
    EXPECT_LE(highlight.Y, headroom);
    EXPECT_LE(highlight.Z, headroom);
}

TEST(ColorCompositeTest, AgXPreservesGrayAndRespectsHeadroom)
{
    const color::DisplayTarget target = SdrTarget();
    const double headroom = color::HeadroomOf(target);
    const color::Vec3d gray = color::Tonemap(color::TonemapperKind::AgX, color::Vec3d{ 0.18, 0.18, 0.18 }, target);

    EXPECT_TRUE(std::isfinite(gray.X));
    EXPECT_NEAR(gray.X, gray.Y, 1.0e-9);
    EXPECT_NEAR(gray.Z, gray.Y, 1.0e-9);
    EXPECT_GT(gray.Y, 0.0);
    EXPECT_LE(gray.Y, headroom);

    const color::Vec3d highlight = color::Tonemap(color::TonemapperKind::AgX, color::Vec3d{ 100.0, 50.0, 200.0 }, target);
    EXPECT_TRUE(std::isfinite(highlight.X));
    EXPECT_TRUE(std::isfinite(highlight.Y));
    EXPECT_TRUE(std::isfinite(highlight.Z));
    EXPECT_LE(highlight.X, headroom);
    EXPECT_LE(highlight.Y, headroom);
    EXPECT_LE(highlight.Z, headroom);
}

TEST(ColorCompositeTest, AGrayStaysGrayThroughEveryEncoding)
{
    const color::Vec3d gray{ 0.18, 0.18, 0.18 };

    for (const color::DisplayEncoding encoding : { color::DisplayEncoding::SrgbNonlinear, color::DisplayEncoding::ScRgbLinear, color::DisplayEncoding::Hdr10Pq })
    {
        const color::CompositeSettings settings{ encoding == color::DisplayEncoding::SrgbNonlinear ? SdrTarget() : LgTarget(encoding), color::TonemapperKind::Passthrough };
        const color::Vec3d encoded = color::CompositePixel(settings, gray, k_Transparent, 0.0);

        EXPECT_NEAR(encoded.X, encoded.Y, 1.0e-6 * encoded.X) << static_cast<int>(encoding);
        EXPECT_NEAR(encoded.Z, encoded.Y, 1.0e-6 * encoded.Y) << static_cast<int>(encoding);
    }
}

TEST(ColorCompositeTest, ScRgbPutsPaperWhiteAtTheNitsOver80)
{
    // 18% gray is at 0.18 of paper white, so 0.18 * 348 / 80 in scRGB
    const color::CompositeSettings settings{ LgTarget(color::DisplayEncoding::ScRgbLinear), color::TonemapperKind::Passthrough };
    const color::Vec3d encoded = color::CompositePixel(settings, color::Vec3d{ 0.18, 0.18, 0.18 }, k_Transparent, 0.0);

    EXPECT_NEAR(encoded.Y, 0.18 * 348.0 / 80.0, 1.0e-6);
}

TEST(ColorCompositeTest, AnOpaqueUiTexelReplacesTheScene)
{
    // white UI at paper white, over a scene that is far brighter
    const color::CompositeSettings settings{ LgTarget(color::DisplayEncoding::Hdr10Pq), color::TonemapperKind::Passthrough };
    const color::Vec3d encoded = color::CompositePixel(settings, color::Vec3d{ 100.0, 100.0, 100.0 }, color::UiTexel{ 255, 255, 255, 255 }, 0.0);

    EXPECT_NEAR(encoded.X, color::PqEncode(348.0), 1.0e-6);
    EXPECT_NEAR(encoded.Y, color::PqEncode(348.0), 1.0e-6);
    EXPECT_NEAR(encoded.Z, color::PqEncode(348.0), 1.0e-6);
}

TEST(ColorCompositeTest, ATransparentUiTexelKeepsTheSceneWhateverItsColorBytesSay)
{
    const color::CompositeSettings settings{ LgTarget(color::DisplayEncoding::Hdr10Pq), color::TonemapperKind::Passthrough };
    const color::Vec3d scene{ 0.3, 0.2, 0.1 };
    const color::Vec3d withoutUi = color::CompositePixel(settings, scene, color::UiTexel{ 0, 0, 0, 0 }, 0.0);
    const color::Vec3d withNoise = color::CompositePixel(settings, scene, color::UiTexel{ 200, 100, 50, 0 }, 0.0);

    EXPECT_DOUBLE_EQ(withoutUi.X, withNoise.X);
    EXPECT_DOUBLE_EQ(withoutUi.Y, withNoise.Y);
    EXPECT_DOUBLE_EQ(withoutUi.Z, withNoise.Z);
}

TEST(ColorCompositeTest, APremultipliedHalfTransparentWhiteBlendsHalfAndHalf)
{
    // Dear ImGui stores white at alpha 0.5 as (0.5, 0.5, 0.5, 0.5). The result is half UI white and half scene
    const color::DisplayTarget target = SdrTarget();
    const color::CompositeSettings settings{ target, color::TonemapperKind::Passthrough };
    const color::UiTexel half{ 128, 128, 128, 128 };
    const color::Vec3d scene{ 0.0, 0.0, 0.0 };
    const color::Vec3d encoded = color::CompositePixel(settings, scene, half, 0.0);

    const double alpha = 128.0 / 255.0;
    EXPECT_NEAR(encoded.Y, color::SrgbEncode(alpha), 1.0e-9);
}
