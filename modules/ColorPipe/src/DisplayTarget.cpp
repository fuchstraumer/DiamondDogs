#include "DisplayTarget.hpp"
#include "ColorSpaces.hpp"
#include "ImageDataFormats.hpp"
#include "PlatformTypes.hpp"
#include <algorithm>
#include <array>
#include <optional>
#include <span>

namespace color
{
    namespace
    {
        struct SurfaceCandidate
        {
            ImageFormat Format;
            ColorSpace Space;
            DisplayEncoding Encoding;
        };

        // each table is in order of preference
        constexpr std::array<SurfaceCandidate, 2> k_Hdr10Candidates
        {
            SurfaceCandidate{ ImageFormat{ ImageComponentFormats::A2B10G10R10, ImageDataType::UNorm }, ColorSpace::HDR10_ST2084, DisplayEncoding::Hdr10Pq },
            SurfaceCandidate{ ImageFormat{ ImageComponentFormats::A2R10G10B10, ImageDataType::UNorm }, ColorSpace::HDR10_ST2084, DisplayEncoding::Hdr10Pq },
        };

        constexpr std::array<SurfaceCandidate, 1> k_ScRgbCandidates
        {
            SurfaceCandidate{ ImageFormat{ ImageComponentFormats::RGBA16, ImageDataType::Float }, ColorSpace::Extended_sRGB_Linear, DisplayEncoding::ScRgbLinear },
        };

        // UNORM and not _SRGB: the composite encodes in the shader
        constexpr std::array<SurfaceCandidate, 2> k_SdrCandidates
        {
            SurfaceCandidate{ ImageFormat{ ImageComponentFormats::BGRA8, ImageDataType::UNorm }, ColorSpace::sRGB_Nonlinear, DisplayEncoding::SrgbNonlinear },
            SurfaceCandidate{ ImageFormat{ ImageComponentFormats::RGBA8, ImageDataType::UNorm }, ColorSpace::sRGB_Nonlinear, DisplayEncoding::SrgbNonlinear },
        };

        constexpr float k_FallbackPaperWhiteNits = 80.0f;

        [[nodiscard]] SurfaceChoice FirstOffered(std::span<const SurfaceCandidate> candidates, std::span<const SurfaceFormatPair> available) noexcept;
        [[nodiscard]] SurfaceChoice ChooseSdr(std::span<const SurfaceFormatPair> available) noexcept;
        [[nodiscard]] SurfaceChoice ChooseHdrOrSdr(DisplayEncoding preferred_encoding, std::span<const SurfaceFormatPair> available) noexcept;
        [[nodiscard]] float ResolvePaperWhiteNits(const DisplayColorCapabilities& capabilities, const DisplayOverrides& overrides) noexcept;
        [[nodiscard]] float ResolvePeakNits(const DisplayColorCapabilities& capabilities, const DisplayOverrides& overrides, DisplayEncoding encoding, float paper_white_nits) noexcept;
        [[nodiscard]] float ResolveBlackNits(const DisplayColorCapabilities& capabilities, const DisplayOverrides& overrides, float paper_white_nits) noexcept;
    }

    SurfaceChoice ChooseSurface(const ColorPipePreferences& preferences, std::span<const SurfaceFormatPair> available) noexcept
    {
        switch (preferences.Output)
        {
        case DisplayOutput::Sdr:
            return ChooseSdr(available);
        case DisplayOutput::Hdr:
            return ChooseHdrOrSdr(preferences.Encoding, available);
        default:
            return SurfaceChoice{};
        }
    }

    Gamut PrimariesOf(DisplayEncoding encoding) noexcept
    {
        return encoding == DisplayEncoding::Hdr10Pq ? Rec2020 : Rec709;
    }

    DisplayTarget ResolveDisplayTarget(const DisplayColorCapabilities& capabilities, const SurfaceChoice& surface, const DisplayOverrides& overrides) noexcept
    {
        const float paperWhiteNits = ResolvePaperWhiteNits(capabilities, overrides);

        DisplayTarget target{};
        target.PaperWhiteNits = paperWhiteNits;
        target.PeakNits = ResolvePeakNits(capabilities, overrides, surface.Encoding, paperWhiteNits);
        target.BlackNits = ResolveBlackNits(capabilities, overrides, paperWhiteNits);
        target.Primaries = PrimariesOf(surface.Encoding);
        target.Encoding = surface.Encoding;

        return target;
    }

    double HeadroomOf(const DisplayTarget& target) noexcept
    {
        return static_cast<double>(target.PeakNits) / static_cast<double>(target.PaperWhiteNits);
    }

    namespace
    {
        SurfaceChoice FirstOffered(std::span<const SurfaceCandidate> candidates, std::span<const SurfaceFormatPair> available) noexcept
        {
            for (const SurfaceCandidate& candidate : candidates)
            {
                const auto offered = std::ranges::find_if(available, [&candidate](const SurfaceFormatPair& pair)
                {
                    return pair.Format == candidate.Format && pair.Space == candidate.Space;
                });

                if (offered != available.end())
                {
                    return SurfaceChoice{ candidate.Format, candidate.Space, candidate.Encoding };
                }
            }

            return SurfaceChoice{};
        }

        SurfaceChoice ChooseSdr(std::span<const SurfaceFormatPair> available) noexcept
        {
            return FirstOffered(k_SdrCandidates, available);
        }

        SurfaceChoice ChooseHdrOrSdr(DisplayEncoding preferred_encoding, std::span<const SurfaceFormatPair> available) noexcept
        {
            // PQ goes first unless scRGB is preferred: any other preference, SDR included, is not an HDR encoding
            const bool scRgbFirst = preferred_encoding == DisplayEncoding::ScRgbLinear;
            const std::span<const SurfaceCandidate> first = scRgbFirst ? std::span<const SurfaceCandidate>{ k_ScRgbCandidates } : std::span<const SurfaceCandidate>{ k_Hdr10Candidates };
            const std::span<const SurfaceCandidate> second = scRgbFirst ? std::span<const SurfaceCandidate>{ k_Hdr10Candidates } : std::span<const SurfaceCandidate>{ k_ScRgbCandidates };

            const SurfaceChoice firstChoice = FirstOffered(first, available);

            if (firstChoice.Encoding != DisplayEncoding::Invalid)
            {
                return firstChoice;
            }

            const SurfaceChoice secondChoice = FirstOffered(second, available);

            if (secondChoice.Encoding != DisplayEncoding::Invalid)
            {
                return secondChoice;
            }

            return ChooseSdr(available);
        }

        float ResolvePaperWhiteNits(const DisplayColorCapabilities& capabilities, const DisplayOverrides& overrides) noexcept
        {
            const float reported = capabilities.sdrWhiteLevel > 0.0f ? capabilities.sdrWhiteLevel : k_FallbackPaperWhiteNits;
            const float paperWhiteNits = overrides.PaperWhiteNits.value_or(reported);

            return std::max(paperWhiteNits, k_MinimumPaperWhiteNits);
        }

        float ResolvePeakNits(const DisplayColorCapabilities& capabilities, const DisplayOverrides& overrides, DisplayEncoding encoding, float paper_white_nits) noexcept
        {
            if (encoding != DisplayEncoding::ScRgbLinear && encoding != DisplayEncoding::Hdr10Pq)
            {
                return paper_white_nits;
            }

            const float peakNits = overrides.PeakNits.value_or(capabilities.MaxLuminance);

            return std::max(peakNits, paper_white_nits);
        }

        float ResolveBlackNits(const DisplayColorCapabilities& capabilities, const DisplayOverrides& overrides, float paper_white_nits) noexcept
        {
            const float blackNits = overrides.BlackNits.value_or(capabilities.MinLuminance);

            return std::clamp(blackNits, 0.0f, paper_white_nits);
        }
    }
}
