#include "ImageDataFormats.hpp"
#include <string_view>
#include <cctype>
#include <algorithm>
#include <ranges>

namespace
{
    // Helper function for case-insensitive string comparison using string_view
    bool StringsEqual(std::string_view lhs, std::string_view rhs) noexcept
    {
        if (lhs.size() != rhs.size())
        {
            return false;
        }

        auto toLowerCompare = [](char ca, char cb)
        {
            return std::tolower(static_cast<unsigned char>(ca)) ==
                   std::tolower(static_cast<unsigned char>(cb));
        };

        return std::ranges::equal(lhs, rhs, toLowerCompare);
    }
}

ImageFormat GetImageFormat(const char* format_name, ImageDataType data_type) noexcept
{
    if (!format_name)
    {
        return ImageFormat{ ImageComponentFormats::Invalid, ImageDataType::UNorm };
    }

    // Convert to string_view for efficient operations
    const std::string_view name{format_name};

    // Single component formats
    if (StringsEqual(name, "r8"))
    {
        return ImageFormat{ ImageComponentFormats::R8, data_type };
    }
    if (StringsEqual(name, "r16"))
    {
        return ImageFormat{ ImageComponentFormats::R16, data_type };
    }
    if (StringsEqual(name, "r32"))
    {
        return ImageFormat{ ImageComponentFormats::R32, data_type };
    }
    if (StringsEqual(name, "r64"))
    {
        return ImageFormat{ ImageComponentFormats::R64, data_type };
    }

    // Two component formats
    if (StringsEqual(name, "rg8"))
    {
        return ImageFormat{ ImageComponentFormats::RG8, data_type };
    }
    if (StringsEqual(name, "rg16"))
    {
        return ImageFormat{ ImageComponentFormats::RG16, data_type };
    }
    if (StringsEqual(name, "rg32"))
    {
        return ImageFormat{ ImageComponentFormats::RG32, data_type };
    }
    if (StringsEqual(name, "rg64"))
    {
        return ImageFormat{ ImageComponentFormats::RG64, data_type };
    }

    // Three component formats
    if (StringsEqual(name, "rgb8"))
    {
        return ImageFormat{ ImageComponentFormats::RGB8, data_type };
    }
    if (StringsEqual(name, "rgb16"))
    {
        return ImageFormat{ ImageComponentFormats::RGB16, data_type };
    }
    if (StringsEqual(name, "rgb32"))
    {
        return ImageFormat{ ImageComponentFormats::RGB32, data_type };
    }
    if (StringsEqual(name, "rgb64"))
    {
        return ImageFormat{ ImageComponentFormats::RGB64, data_type };
    }

    // Four component formats
    if (StringsEqual(name, "rgba8"))
    {
        return ImageFormat{ ImageComponentFormats::RGBA8, data_type };
    }
    if (StringsEqual(name, "rgba16"))
    {
        return ImageFormat{ ImageComponentFormats::RGBA16, data_type };
    }
    if (StringsEqual(name, "rgba32"))
    {
        return ImageFormat{ ImageComponentFormats::RGBA32, data_type };
    }
    if (StringsEqual(name, "rgba64"))
    {
        return ImageFormat{ ImageComponentFormats::RGBA64, data_type };
    }

    // Special packed formats
    if (StringsEqual(name, "a2r10g10b10"))
    {
        return ImageFormat{ ImageComponentFormats::A2R10G10B10, data_type };
    }
    if (StringsEqual(name, "a2b10g10r10"))
    {
        return ImageFormat{ ImageComponentFormats::A2B10G10R10, data_type };
    }
    if (StringsEqual(name, "r5g6b5"))
    {
        return ImageFormat{ ImageComponentFormats::R5G6B5, data_type };
    }
    if (StringsEqual(name, "r5g5b5a1"))
    {
        return ImageFormat{ ImageComponentFormats::R5G5B5A1, data_type };
    }
    if (StringsEqual(name, "r4g4b4a4"))
    {
        return ImageFormat{ ImageComponentFormats::R4G4B4A4, data_type };
    }

    // Depth/stencil formats
    if (StringsEqual(name, "d16"))
    {
        return ImageFormat{ ImageComponentFormats::D16, data_type };
    }
    if (StringsEqual(name, "d24"))
    {
        return ImageFormat{ ImageComponentFormats::D24, data_type };
    }
    if (StringsEqual(name, "d32"))
    {
        return ImageFormat{ ImageComponentFormats::D32, data_type };
    }
    if (StringsEqual(name, "d24s8"))
    {
        return ImageFormat{ ImageComponentFormats::D24S8, data_type };
    }
    if (StringsEqual(name, "d32s8"))
    {
        return ImageFormat{ ImageComponentFormats::D32S8, data_type };
    }
    if (StringsEqual(name, "s8"))
    {
        return ImageFormat{ ImageComponentFormats::S8, data_type };
    }

    // Compressed formats
    if (StringsEqual(name, "bc1"))
    {
        return ImageFormat{ ImageComponentFormats::BC1, data_type };
    }
    if (StringsEqual(name, "bc2"))
    {
        return ImageFormat{ ImageComponentFormats::BC2, data_type };
    }
    if (StringsEqual(name, "bc3"))
    {
        return ImageFormat{ ImageComponentFormats::BC3, data_type };
    }
    if (StringsEqual(name, "bc4"))
    {
        return ImageFormat{ ImageComponentFormats::BC4, data_type };
    }
    if (StringsEqual(name, "bc5"))
    {
        return ImageFormat{ ImageComponentFormats::BC5, data_type };
    }
    if (StringsEqual(name, "bc6h"))
    {
        return ImageFormat{ ImageComponentFormats::BC6H, data_type };
    }
    if (StringsEqual(name, "bc7"))
    {
        return ImageFormat{ ImageComponentFormats::BC7, data_type };
    }

    return ImageFormat{ ImageComponentFormats::Invalid, ImageDataType::UNorm };
}
