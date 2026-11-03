#include "DisplayTarget.hpp"
#include "ImageDataFormats.hpp"
#include "PlatformTypes.hpp"
#include <gtest/gtest.h>
#include <array>
#include <span>

namespace
{
    constexpr ImageFormat k_Bgra8Unorm{ ImageComponentFormats::BGRA8, ImageDataType::UNorm };
    constexpr ImageFormat k_Bgra8Srgb{ ImageComponentFormats::BGRA8, ImageDataType::sRGB };
    constexpr ImageFormat k_Rgba8Unorm{ ImageComponentFormats::RGBA8, ImageDataType::UNorm };
    constexpr ImageFormat k_Rgba8Srgb{ ImageComponentFormats::RGBA8, ImageDataType::sRGB };
    constexpr ImageFormat k_Rgba16Float{ ImageComponentFormats::RGBA16, ImageDataType::Float };
    constexpr ImageFormat k_A2b10g10r10Unorm{ ImageComponentFormats::A2B10G10R10, ImageDataType::UNorm };

    // pairs of the LG ULTRAGEAR+ on the RTX 4080 with Windows HDR on, in driver order. It has no FP16 and PQ pair
    const std::array<SurfaceFormatPair, 7> k_LgPairs
    {
        SurfaceFormatPair{ k_Bgra8Unorm, ColorSpace::sRGB_Nonlinear },
        SurfaceFormatPair{ k_Bgra8Srgb, ColorSpace::sRGB_Nonlinear },
        SurfaceFormatPair{ k_Rgba8Unorm, ColorSpace::sRGB_Nonlinear },
        SurfaceFormatPair{ k_Rgba8Srgb, ColorSpace::sRGB_Nonlinear },
        SurfaceFormatPair{ k_Rgba16Float, ColorSpace::Extended_sRGB_Linear },
        SurfaceFormatPair{ k_A2b10g10r10Unorm, ColorSpace::HDR10_ST2084 },
        SurfaceFormatPair{ k_A2b10g10r10Unorm, ColorSpace::sRGB_Nonlinear },
    };

    // The AMD iGPU of the same machine: SDR pairs only
    const std::array<SurfaceFormatPair, 4> k_SdrOnlyPairs
    {
        SurfaceFormatPair{ k_Rgba8Unorm, ColorSpace::sRGB_Nonlinear },
        SurfaceFormatPair{ k_Bgra8Unorm, ColorSpace::sRGB_Nonlinear },
        SurfaceFormatPair{ k_Rgba8Srgb, ColorSpace::sRGB_Nonlinear },
        SurfaceFormatPair{ k_Bgra8Srgb, ColorSpace::sRGB_Nonlinear },
    };

    // DXGI GetDesc1 and DisplayConfig of the LG: 0.035 to 600 nits, and an SDR white level of 348 nits
    DisplayColorCapabilities LgCapabilities()
    {
        DisplayColorCapabilities capabilities{};
        capabilities.hdrSupported = true;
        capabilities.hdrEnabled = true;
        capabilities.sdrWhiteLevel = 348.0f;
        capabilities.MaxLuminance = 600.0f;
        capabilities.MinLuminance = 0.035f;

        return capabilities;
    }

    color::ColorPipePreferences Preferences(color::DisplayOutput output, color::DisplayEncoding encoding)
    {
        return color::ColorPipePreferences{ output, encoding, color::TonemapperKind::Passthrough };
    }

    void ExpectChoice(const color::SurfaceChoice& choice, ImageFormat format, ColorSpace space, color::DisplayEncoding encoding)
    {
        EXPECT_EQ(choice.Format, format);
        EXPECT_EQ(choice.Space, space);
        EXPECT_EQ(choice.Encoding, encoding);
    }
}

TEST(ChooseSurfaceTest, LgPicksHdr10PqWhenPreferred)
{
    const color::SurfaceChoice choice = color::ChooseSurface(Preferences(color::DisplayOutput::Hdr, color::DisplayEncoding::Hdr10Pq), k_LgPairs);

    ExpectChoice(choice, k_A2b10g10r10Unorm, ColorSpace::HDR10_ST2084, color::DisplayEncoding::Hdr10Pq);
}

TEST(ChooseSurfaceTest, LgPicksScRgbWhenPreferred)
{
    const color::SurfaceChoice choice = color::ChooseSurface(Preferences(color::DisplayOutput::Hdr, color::DisplayEncoding::ScRgbLinear), k_LgPairs);

    ExpectChoice(choice, k_Rgba16Float, ColorSpace::Extended_sRGB_Linear, color::DisplayEncoding::ScRgbLinear);
}

TEST(ChooseSurfaceTest, LgPicksSdrWhenSdrIsPreferred)
{
    // the pair is UNORM and not _SRGB, even with an HDR preference left over in the encoding field
    const color::SurfaceChoice choice = color::ChooseSurface(Preferences(color::DisplayOutput::Sdr, color::DisplayEncoding::Hdr10Pq), k_LgPairs);

    ExpectChoice(choice, k_Bgra8Unorm, ColorSpace::sRGB_Nonlinear, color::DisplayEncoding::SrgbNonlinear);
}

TEST(ChooseSurfaceTest, HdrFallsBackToSdrWhenTheSurfaceOffersNoHdrPair)
{
    const color::SurfaceChoice pq = color::ChooseSurface(Preferences(color::DisplayOutput::Hdr, color::DisplayEncoding::Hdr10Pq), k_SdrOnlyPairs);
    const color::SurfaceChoice scRgb = color::ChooseSurface(Preferences(color::DisplayOutput::Hdr, color::DisplayEncoding::ScRgbLinear), k_SdrOnlyPairs);

    ExpectChoice(pq, k_Bgra8Unorm, ColorSpace::sRGB_Nonlinear, color::DisplayEncoding::SrgbNonlinear);
    ExpectChoice(scRgb, k_Bgra8Unorm, ColorSpace::sRGB_Nonlinear, color::DisplayEncoding::SrgbNonlinear);
}

TEST(ChooseSurfaceTest, HdrFallsBackToTheOtherHdrPairBeforeSdr)
{
    const std::array<SurfaceFormatPair, 2> onlyPq
    {
        SurfaceFormatPair{ k_Bgra8Unorm, ColorSpace::sRGB_Nonlinear },
        SurfaceFormatPair{ k_A2b10g10r10Unorm, ColorSpace::HDR10_ST2084 },
    };
    const std::array<SurfaceFormatPair, 2> onlyScRgb
    {
        SurfaceFormatPair{ k_Bgra8Unorm, ColorSpace::sRGB_Nonlinear },
        SurfaceFormatPair{ k_Rgba16Float, ColorSpace::Extended_sRGB_Linear },
    };

    ExpectChoice(color::ChooseSurface(Preferences(color::DisplayOutput::Hdr, color::DisplayEncoding::ScRgbLinear), onlyPq),
        k_A2b10g10r10Unorm, ColorSpace::HDR10_ST2084, color::DisplayEncoding::Hdr10Pq);
    ExpectChoice(color::ChooseSurface(Preferences(color::DisplayOutput::Hdr, color::DisplayEncoding::Hdr10Pq), onlyScRgb),
        k_Rgba16Float, ColorSpace::Extended_sRGB_Linear, color::DisplayEncoding::ScRgbLinear);
}

TEST(ChooseSurfaceTest, HdrWithoutAnHdrEncodingPreferenceStartsWithPq)
{
    const color::SurfaceChoice choice = color::ChooseSurface(Preferences(color::DisplayOutput::Hdr, color::DisplayEncoding::SrgbNonlinear), k_LgPairs);

    ExpectChoice(choice, k_A2b10g10r10Unorm, ColorSpace::HDR10_ST2084, color::DisplayEncoding::Hdr10Pq);
}

TEST(ChooseSurfaceTest, NoUsablePairGivesAnInvalidEncoding)
{
    const std::array<SurfaceFormatPair, 2> onlySrgbFormats
    {
        SurfaceFormatPair{ k_Bgra8Srgb, ColorSpace::sRGB_Nonlinear },
        SurfaceFormatPair{ k_Rgba8Srgb, ColorSpace::sRGB_Nonlinear },
    };

    EXPECT_EQ(color::ChooseSurface(Preferences(color::DisplayOutput::Hdr, color::DisplayEncoding::Hdr10Pq), {}).Encoding, color::DisplayEncoding::Invalid);
    EXPECT_EQ(color::ChooseSurface(Preferences(color::DisplayOutput::Sdr, color::DisplayEncoding::SrgbNonlinear), onlySrgbFormats).Encoding, color::DisplayEncoding::Invalid);
    EXPECT_EQ(color::ChooseSurface(Preferences(color::DisplayOutput::Invalid, color::DisplayEncoding::Hdr10Pq), k_LgPairs).Encoding, color::DisplayEncoding::Invalid);
}

TEST(ResolveDisplayTargetTest, LgWithPq)
{
    const color::SurfaceChoice surface{ k_A2b10g10r10Unorm, ColorSpace::HDR10_ST2084, color::DisplayEncoding::Hdr10Pq };
    const color::DisplayTarget target = color::ResolveDisplayTarget(LgCapabilities(), surface, color::DisplayOverrides{});

    EXPECT_FLOAT_EQ(target.PaperWhiteNits, 348.0f);
    EXPECT_FLOAT_EQ(target.PeakNits, 600.0f);
    EXPECT_FLOAT_EQ(target.BlackNits, 0.035f);
    EXPECT_EQ(target.Encoding, color::DisplayEncoding::Hdr10Pq);
    EXPECT_EQ(target.Primaries.Red.X, color::Rec2020.Red.X);
    EXPECT_NEAR(color::HeadroomOf(target), 600.0 / 348.0, 1.0e-6);
}

TEST(ResolveDisplayTargetTest, LgWithScRgbUsesRec709Primaries)
{
    const color::SurfaceChoice surface{ k_Rgba16Float, ColorSpace::Extended_sRGB_Linear, color::DisplayEncoding::ScRgbLinear };
    const color::DisplayTarget target = color::ResolveDisplayTarget(LgCapabilities(), surface, color::DisplayOverrides{});

    EXPECT_FLOAT_EQ(target.PeakNits, 600.0f);
    EXPECT_EQ(target.Primaries.Red.X, color::Rec709.Red.X);
}

TEST(ResolveDisplayTargetTest, SdrPeakEqualsPaperWhite)
{
    const color::SurfaceChoice surface{ k_Bgra8Unorm, ColorSpace::sRGB_Nonlinear, color::DisplayEncoding::SrgbNonlinear };
    const color::DisplayTarget target = color::ResolveDisplayTarget(LgCapabilities(), surface, color::DisplayOverrides{});

    EXPECT_FLOAT_EQ(target.PaperWhiteNits, 348.0f);
    EXPECT_FLOAT_EQ(target.PeakNits, 348.0f);
    EXPECT_DOUBLE_EQ(color::HeadroomOf(target), 1.0);
    EXPECT_EQ(target.Primaries.Red.X, color::Rec709.Red.X);
}

TEST(ResolveDisplayTargetTest, OverridesReplaceWhatThePlatformReports)
{
    const color::SurfaceChoice surface{ k_A2b10g10r10Unorm, ColorSpace::HDR10_ST2084, color::DisplayEncoding::Hdr10Pq };
    const color::DisplayOverrides overrides{ 800.0f, 0.0f, 200.0f };
    const color::DisplayTarget target = color::ResolveDisplayTarget(LgCapabilities(), surface, overrides);

    EXPECT_FLOAT_EQ(target.PeakNits, 800.0f);
    EXPECT_FLOAT_EQ(target.BlackNits, 0.0f);
    EXPECT_FLOAT_EQ(target.PaperWhiteNits, 200.0f);
}

TEST(ResolveDisplayTargetTest, ImpossibleValuesAreClamped)
{
    const color::SurfaceChoice surface{ k_A2b10g10r10Unorm, ColorSpace::HDR10_ST2084, color::DisplayEncoding::Hdr10Pq };

    // a peak below paper white, and a black above it
    const color::DisplayTarget target = color::ResolveDisplayTarget(LgCapabilities(), surface, color::DisplayOverrides{ 100.0f, 500.0f, std::nullopt });
    EXPECT_FLOAT_EQ(target.PeakNits, 348.0f);
    EXPECT_FLOAT_EQ(target.BlackNits, 348.0f);

    // a paper white of 0 or less
    const color::DisplayTarget zeroWhite = color::ResolveDisplayTarget(LgCapabilities(), surface, color::DisplayOverrides{ std::nullopt, std::nullopt, 0.0f });
    EXPECT_FLOAT_EQ(zeroWhite.PaperWhiteNits, color::k_MinimumPaperWhiteNits);
    EXPECT_GE(zeroWhite.PeakNits, zeroWhite.PaperWhiteNits);
}

TEST(ResolveDisplayTargetTest, ADisplayWithNoMaximumGetsAHeadroomOfOne)
{
    DisplayColorCapabilities capabilities{};
    capabilities.sdrWhiteLevel = 200.0f;
    const color::SurfaceChoice surface{ k_Rgba16Float, ColorSpace::Extended_sRGB_Linear, color::DisplayEncoding::ScRgbLinear };
    const color::DisplayTarget target = color::ResolveDisplayTarget(capabilities, surface, color::DisplayOverrides{});

    EXPECT_FLOAT_EQ(target.PeakNits, 200.0f);
    EXPECT_DOUBLE_EQ(color::HeadroomOf(target), 1.0);
}

TEST(ResolveDisplayTargetTest, ADisplayWithNoSdrWhiteLevelGets80Nits)
{
    const DisplayColorCapabilities capabilities{ .sdrWhiteLevel = 0.0f };
    const color::SurfaceChoice surface{ k_Bgra8Unorm, ColorSpace::sRGB_Nonlinear, color::DisplayEncoding::SrgbNonlinear };

    EXPECT_FLOAT_EQ(color::ResolveDisplayTarget(capabilities, surface, color::DisplayOverrides{}).PaperWhiteNits, 80.0f);
}
