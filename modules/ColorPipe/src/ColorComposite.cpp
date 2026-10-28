#include "ColorComposite.hpp"
#include "ColorSpaces.hpp"
#include "DisplayTarget.hpp"
#include "TransferFunctions.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace color
{
    namespace
    {
        // sRGB-encoded zone colors, -5 EV to +5 EV. The table in ColorCompositeTonemappers.slang must match
        constexpr std::array<Vec3d, k_FalseColorZoneCount> k_FalseColorZonesSrgb
        {
            Vec3d{ 0.10, 0.00, 0.20 },
            Vec3d{ 0.00, 0.00, 0.55 },
            Vec3d{ 0.00, 0.35, 0.75 },
            Vec3d{ 0.00, 0.65, 0.65 },
            Vec3d{ 0.10, 0.65, 0.25 },
            Vec3d{ 0.46, 0.46, 0.46 },
            Vec3d{ 0.85, 0.85, 0.10 },
            Vec3d{ 1.00, 0.60, 0.00 },
            Vec3d{ 1.00, 0.25, 0.00 },
            Vec3d{ 0.85, 0.00, 0.35 },
            Vec3d{ 1.00, 1.00, 1.00 },
        };

        // the luminance below which the EV stops falling, so that log2 never sees 0
        constexpr double k_LuminanceFloor = 1.0e-30;
        constexpr double k_UiAlphaScale = 1.0 / 255.0;

        [[nodiscard]] const Mat3d& Ap1ToRec2020() noexcept;
        [[nodiscard]] const Mat3d& Ap1ToRec709() noexcept;
        [[nodiscard]] const Mat3d& Rec2020ToRec709() noexcept;
        [[nodiscard]] const Mat3d& Rec709ToRec2020() noexcept;
        [[nodiscard]] Vec3d Scale(const Vec3d& color, double factor) noexcept;
        [[nodiscard]] Vec3d Clamp(const Vec3d& color, double lowest, double highest) noexcept;
        [[nodiscard]] Vec3d Mix(const Vec3d& first, const Vec3d& second, double second_weight) noexcept;
        [[nodiscard]] Vec3d DecodeSrgb(const Vec3d& encoded) noexcept;
        [[nodiscard]] Vec3d EncodeSrgbRelativeToPaperWhite(const Vec3d& nits, double paper_white_nits) noexcept;
        [[nodiscard]] Vec3d TonemapPassthrough(const Vec3d& exposed_ap1, const DisplayTarget& target) noexcept;
        [[nodiscard]] Vec3d TonemapFalseColorEv(const Vec3d& exposed_ap1) noexcept;
        [[nodiscard]] Vec3d TonemapKhronosNeutral(const Vec3d& exposed_ap1, const DisplayTarget& target) noexcept;
        [[nodiscard]] Vec3d TonemapAgX(const Vec3d& exposed_ap1, const DisplayTarget& target) noexcept;
        [[nodiscard]] Vec3d NotANumber() noexcept;
    }

    Vec3d ApplyExposure(const Vec3d& scene_ap1, double exposure_ev) noexcept
    {
        return Scale(scene_ap1, std::exp2(exposure_ev));
    }

    Vec3d Tonemap(TonemapperKind kind, const Vec3d& exposed_ap1, const DisplayTarget& target) noexcept
    {
        switch (kind)
        {
        case TonemapperKind::Passthrough:
            return TonemapPassthrough(exposed_ap1, target);
        case TonemapperKind::FalseColorEv:
            return TonemapFalseColorEv(exposed_ap1);
        case TonemapperKind::KhronosNeutral:
            return TonemapKhronosNeutral(exposed_ap1, target);
        case TonemapperKind::AgX:
            return TonemapAgX(exposed_ap1, target);
        default:
            return NotANumber();
        }
    }

    double FalseColorEvOf(double luminance) noexcept
    {
        return std::log2(std::max(luminance, k_LuminanceFloor) / k_MiddleGray);
    }

    int32_t FalseColorZoneOf(double ev) noexcept
    {
        const double nearestEv = std::floor(ev + 0.5);
        const double clampedEv = std::clamp(nearestEv, static_cast<double>(k_FalseColorLowestZoneEv), static_cast<double>(k_FalseColorLowestZoneEv + k_FalseColorZoneCount - 1));

        return static_cast<int32_t>(clampedEv) - k_FalseColorLowestZoneEv;
    }

    Vec3d ToTargetPrimaries(const Vec3d& rec2020, const DisplayTarget& target) noexcept
    {
        if (target.Encoding == DisplayEncoding::Hdr10Pq)
        {
            return rec2020;
        }

        return Multiply(Rec2020ToRec709(), rec2020);
    }

    Vec3d ClipToHeadroom(const Vec3d& relative, const DisplayTarget& target) noexcept
    {
        return Clamp(relative, 0.0, HeadroomOf(target));
    }

    Vec3d CompositeUi(const Vec3d& scene_nits, const UiTexel& ui, const DisplayTarget& target) noexcept
    {
        const double alpha = ui[3] * k_UiAlphaScale;

        // a texel with no alpha covers nothing, whatever its color bytes say
        if (alpha <= 0.0)
        {
            return scene_nits;
        }

        const Vec3d premultiplied{ ui[0] * k_UiAlphaScale, ui[1] * k_UiAlphaScale, ui[2] * k_UiAlphaScale };
        const Vec3d encoded = Clamp(Scale(premultiplied, 1.0 / alpha), 0.0, 1.0);
        const Vec3d linearRec709 = DecodeSrgb(encoded);
        const Vec3d linearTarget = target.Encoding == DisplayEncoding::Hdr10Pq ? Multiply(Rec709ToRec2020(), linearRec709) : linearRec709;
        const Vec3d uiNits = Scale(linearTarget, static_cast<double>(target.PaperWhiteNits));

        return Mix(scene_nits, uiNits, alpha);
    }

    Vec3d EncodeForDisplay(const Vec3d& nits, const DisplayTarget& target) noexcept
    {
        switch (target.Encoding)
        {
        case DisplayEncoding::SrgbNonlinear:
            return EncodeSrgbRelativeToPaperWhite(nits, static_cast<double>(target.PaperWhiteNits));
        case DisplayEncoding::ScRgbLinear:
            return Vec3d{ ScRgbEncode(nits.X), ScRgbEncode(nits.Y), ScRgbEncode(nits.Z) };
        case DisplayEncoding::Hdr10Pq:
            return Vec3d{ PqEncode(nits.X), PqEncode(nits.Y), PqEncode(nits.Z) };
        default:
            return NotANumber();
        }
    }

    Vec3d CompositePixel(const CompositeSettings& settings, const Vec3d& scene_ap1, const UiTexel& ui, double exposure_ev) noexcept
    {
        const DisplayTarget& target = settings.Target;
        const Vec3d exposed = ApplyExposure(scene_ap1, exposure_ev);
        const Vec3d tonemapped = Tonemap(settings.Tonemapper, exposed, target);
        const Vec3d targetRelative = ClipToHeadroom(ToTargetPrimaries(tonemapped, target), target);
        const Vec3d sceneNits = Scale(targetRelative, static_cast<double>(target.PaperWhiteNits));
        const Vec3d composited = CompositeUi(sceneNits, ui, target);

        return EncodeForDisplay(composited, target);
    }

    namespace
    {
        const Mat3d& Ap1ToRec2020() noexcept
        {
            static const Mat3d matrix = RgbToRgb(AP1, Rec2020, ChromaticAdaptation::Bradford);
            return matrix;
        }

        const Mat3d& Ap1ToRec709() noexcept
        {
            static const Mat3d matrix = RgbToRgb(AP1, Rec709, ChromaticAdaptation::Bradford);
            return matrix;
        }

        const Mat3d& Rec2020ToRec709() noexcept
        {
            static const Mat3d matrix = RgbToRgb(Rec2020, Rec709, ChromaticAdaptation::Bradford);
            return matrix;
        }

        const Mat3d& Rec709ToRec2020() noexcept
        {
            static const Mat3d matrix = RgbToRgb(Rec709, Rec2020, ChromaticAdaptation::Bradford);
            return matrix;
        }

        Vec3d Scale(const Vec3d& color, double factor) noexcept
        {
            return Vec3d{ color.X * factor, color.Y * factor, color.Z * factor };
        }

        Vec3d Clamp(const Vec3d& color, double lowest, double highest) noexcept
        {
            return Vec3d{ std::clamp(color.X, lowest, highest), std::clamp(color.Y, lowest, highest), std::clamp(color.Z, lowest, highest) };
        }

        Vec3d Mix(const Vec3d& first, const Vec3d& second, double second_weight) noexcept
        {
            const double firstWeight = 1.0 - second_weight;

            return Vec3d
            {
                (first.X * firstWeight) + (second.X * second_weight),
                (first.Y * firstWeight) + (second.Y * second_weight),
                (first.Z * firstWeight) + (second.Z * second_weight) 
            };
        }

        Vec3d DecodeSrgb(const Vec3d& encoded) noexcept
        {
            return Vec3d{ SrgbDecode(encoded.X), SrgbDecode(encoded.Y), SrgbDecode(encoded.Z) };
        }

        Vec3d EncodeSrgbRelativeToPaperWhite(const Vec3d& nits, double paper_white_nits) noexcept
        {
            const Vec3d relative = Clamp(Scale(nits, 1.0 / paper_white_nits), 0.0, 1.0);

            return Vec3d{ SrgbEncode(relative.X), SrgbEncode(relative.Y), SrgbEncode(relative.Z) };
        }

        Vec3d TonemapPassthrough(const Vec3d& exposed_ap1, const DisplayTarget& target) noexcept
        {
            return Clamp(Multiply(Ap1ToRec2020(), exposed_ap1), 0.0, HeadroomOf(target));
        }

        Vec3d TonemapFalseColorEv(const Vec3d& exposed_ap1) noexcept
        {
            const double luminance = Luminance(AP1, exposed_ap1);
            const int32_t zone = FalseColorZoneOf(FalseColorEvOf(luminance));
            const Vec3d linearRec709 = DecodeSrgb(k_FalseColorZonesSrgb[static_cast<size_t>(zone)]);

            return Multiply(Rec709ToRec2020(), linearRec709);
        }

        constexpr Mat3d k_Ap1ToAgxInset
        { {
            std::array<double, 3>{ 1.4242167303573248, -0.44453410335192245, 0.020317372994598713 },
            std::array<double, 3>{ -0.044029197774698277, 0.96554338243400251, 0.078485815340695361 },
            std::array<double, 3>{ 0.04097325078724233, -0.050275587678294958, 1.0093023368910528 },
        } };

        constexpr Mat3d k_AgxOutsetToRec2020
        { {
            std::array<double, 3>{ 0.73125620860645901, 0.31356609159127502, -0.044822300197734338 },
            std::array<double, 3>{ 0.033372950385875823, 1.051387787463244, -0.08476073784911943 },
            std::array<double, 3>{ -0.032509362717976532, 0.011978928465452423, 1.0205304342525243 },
        } };

        constexpr double k_AgxMinEv = -12.473931188332412;
        constexpr double k_AgxMaxEv = 4.026068811667588;
        constexpr double k_AgxInvEvRange = 1.0 / (4.026068811667588 - (-12.473931188332412));

        double AgxDefaultContrastApprox(double x) noexcept
        {
            const double x2 = x * x;
            const double x4 = x2 * x2;
            return (15.5 * x4 * x2) -
                   (40.14 * x4 * x) +
                   (31.96 * x4) -
                   (6.868 * x2 * x) +
                   (0.4298 * x2) +
                   (0.1191 * x) -
                   0.00232;
        }

        Vec3d PbrNeutralCurve(Vec3d color) noexcept
        {
            constexpr double k_StartCompression = 0.8 - 0.04;
            constexpr double k_Desaturation = 0.15;

            const double x = std::min({ color.X, color.Y, color.Z });
            const double offset = x < 0.08 ? x - (6.25 * x * x) : 0.04;
            color.X -= offset;
            color.Y -= offset;
            color.Z -= offset;

            const double peak = std::max({ color.X, color.Y, color.Z });
            if (peak < k_StartCompression)
            {
                return color;
            }

            constexpr double d = 1.0 - k_StartCompression;
            const double newPeak = 1.0 - (d * d / (peak + d - k_StartCompression));
            color.X *= newPeak / peak;
            color.Y *= newPeak / peak;
            color.Z *= newPeak / peak;

            const double g = 1.0 / ((k_Desaturation * (peak - newPeak)) + 1.0);
            return Vec3d{
                (newPeak * (1.0 - g)) + (color.X * g),
                (newPeak * (1.0 - g)) + (color.Y * g),
                (newPeak * (1.0 - g)) + (color.Z * g)
            };
        }

        Vec3d TonemapKhronosNeutral(const Vec3d& exposed_ap1, const DisplayTarget& target) noexcept
        {
            const Vec3d rec709 = Clamp(Multiply(Ap1ToRec709(), exposed_ap1), 0.0, std::numeric_limits<double>::infinity());
            const Vec3d tonemappedRec709 = PbrNeutralCurve(rec709);
            const Vec3d rec2020 = Multiply(Rec709ToRec2020(), tonemappedRec709);
            return Clamp(rec2020, 0.0, HeadroomOf(target));
        }

        Vec3d TonemapAgX(const Vec3d& exposed_ap1, const DisplayTarget& target) noexcept
        {
            const Vec3d inset = Multiply(k_Ap1ToAgxInset, exposed_ap1);
            const Vec3d logColor{
                std::clamp(std::log2(std::max(inset.X, 1.0e-10)), k_AgxMinEv, k_AgxMaxEv),
                std::clamp(std::log2(std::max(inset.Y, 1.0e-10)), k_AgxMinEv, k_AgxMaxEv),
                std::clamp(std::log2(std::max(inset.Z, 1.0e-10)), k_AgxMinEv, k_AgxMaxEv)
            };
            const Vec3d normLog{
                (logColor.X - k_AgxMinEv) * k_AgxInvEvRange,
                (logColor.Y - k_AgxMinEv) * k_AgxInvEvRange,
                (logColor.Z - k_AgxMinEv) * k_AgxInvEvRange
            };
            const Vec3d curved{
                AgxDefaultContrastApprox(normLog.X),
                AgxDefaultContrastApprox(normLog.Y),
                AgxDefaultContrastApprox(normLog.Z)
            };
            const Vec3d rec2020 = Multiply(k_AgxOutsetToRec2020, curved);
            return Clamp(rec2020, 0.0, HeadroomOf(target));
        }

        Vec3d NotANumber() noexcept
        {
            const double nan = std::nan("");

            return Vec3d{ nan, nan, nan };
        }
    }
}
