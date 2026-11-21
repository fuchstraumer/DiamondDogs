#include "LoomFormats.hpp"
#include "ImageDataFormats.hpp"
#include "ShaderManifestLayout.hpp"
#include <optional>
#include <string_view>

namespace loom
{

namespace
{
    using lodestone::TextureFormat;

    // Depth formats have one meaning each, so the data type that the caller wrote does not matter.
    std::optional<TextureFormat> DepthFormatOf(ImageComponentFormats components)
    {
        switch (components)
        {
        case ImageComponentFormats::D16:
            return TextureFormat::Depth16Unorm;
        case ImageComponentFormats::D24:
            return TextureFormat::Depth24Plus;
        case ImageComponentFormats::D24S8:
            return TextureFormat::Depth24PlusStencil8;
        case ImageComponentFormats::D32:
            return TextureFormat::Depth32Float;
        default:
            break;
        }

        return std::nullopt;
    }

    std::optional<TextureFormat> UnormFormatOf(ImageComponentFormats components)
    {
        switch (components)
        {
        case ImageComponentFormats::R8:
            return TextureFormat::R8Unorm;
        case ImageComponentFormats::RG8:
            return TextureFormat::RG8Unorm;
        case ImageComponentFormats::RGBA8:
            return TextureFormat::RGBA8Unorm;
        case ImageComponentFormats::BGRA8:
            return TextureFormat::BGRA8Unorm;
        // A2B10G10R10 is Vulkan's packed order: red in the low bits, as in TextureFormat's R10G10B10A2
        case ImageComponentFormats::A2B10G10R10:
            return TextureFormat::R10G10B10A2Unorm;
        default:
            break;
        }

        return std::nullopt;
    }

    std::optional<TextureFormat> SrgbFormatOf(ImageComponentFormats components)
    {
        switch (components)
        {
        case ImageComponentFormats::RGBA8:
            return TextureFormat::RGBA8UnormSrgb;
        case ImageComponentFormats::BGRA8:
            return TextureFormat::BGRA8UnormSrgb;
        default:
            break;
        }

        return std::nullopt;
    }

    std::optional<TextureFormat> FloatFormatOf(ImageComponentFormats components)
    {
        switch (components)
        {
        case ImageComponentFormats::R16:
            return TextureFormat::R16Float;
        case ImageComponentFormats::RG16:
            return TextureFormat::RG16Float;
        case ImageComponentFormats::RGBA16:
            return TextureFormat::RGBA16Float;
        case ImageComponentFormats::R32:
            return TextureFormat::R32Float;
        case ImageComponentFormats::RG32:
            return TextureFormat::RG32Float;
        case ImageComponentFormats::RGBA32:
            return TextureFormat::RGBA32Float;
        default:
            break;
        }

        return std::nullopt;
    }

    std::optional<TextureFormat> UintFormatOf(ImageComponentFormats components)
    {
        switch (components)
        {
        case ImageComponentFormats::R32:
            return TextureFormat::R32Uint;
        case ImageComponentFormats::RG32:
            return TextureFormat::RG32Uint;
        case ImageComponentFormats::RGBA32:
            return TextureFormat::RGBA32Uint;
        default:
            break;
        }

        return std::nullopt;
    }

}

std::optional<lodestone::TextureFormat> ToTextureFormat(const ImageFormat& format) noexcept
{
    const std::optional<TextureFormat> depth = DepthFormatOf(format.ComponentFormat);
    if (depth.has_value())
    {
        return depth;
    }

    switch (format.DataType)
    {
    case ImageDataType::UNorm:
        return UnormFormatOf(format.ComponentFormat);
    case ImageDataType::sRGB:
        return SrgbFormatOf(format.ComponentFormat);
    case ImageDataType::Float:
        return FloatFormatOf(format.ComponentFormat);
    case ImageDataType::UInt:
        return UintFormatOf(format.ComponentFormat);
    case ImageDataType::SNorm:
        if (format.ComponentFormat == ImageComponentFormats::RGBA8)
        {
            return TextureFormat::RGBA8Snorm;
        }

        break;
    case ImageDataType::SInt:
        [[fallthrough]];
    case ImageDataType::UFloat:
        [[fallthrough]];
    case ImageDataType::Invalid:
        break;
    }

    return std::nullopt;
}

std::string_view TextureFormatName(lodestone::TextureFormat format) noexcept
{
    switch (format)
    {
    case TextureFormat::R8Unorm:
        return "R8Unorm";
    case TextureFormat::RG8Unorm:
        return "RG8Unorm";
    case TextureFormat::RGBA8Unorm:
        return "RGBA8Unorm";
    case TextureFormat::RGBA8UnormSrgb:
        return "RGBA8UnormSrgb";
    case TextureFormat::BGRA8Unorm:
        return "BGRA8Unorm";
    case TextureFormat::BGRA8UnormSrgb:
        return "BGRA8UnormSrgb";
    case TextureFormat::R16Float:
        return "R16Float";
    case TextureFormat::RG16Float:
        return "RG16Float";
    case TextureFormat::RGBA16Float:
        return "RGBA16Float";
    case TextureFormat::R32Float:
        return "R32Float";
    case TextureFormat::RG32Float:
        return "RG32Float";
    case TextureFormat::RGBA32Float:
        return "RGBA32Float";
    case TextureFormat::R32Uint:
        return "R32Uint";
    case TextureFormat::RG32Uint:
        return "RG32Uint";
    case TextureFormat::RGBA32Uint:
        return "RGBA32Uint";
    case TextureFormat::R10G10B10A2Unorm:
        return "R10G10B10A2Unorm";
    case TextureFormat::R11G11B10Ufloat:
        return "R11G11B10Ufloat";
    case TextureFormat::Depth16Unorm:
        return "Depth16Unorm";
    case TextureFormat::Depth24Plus:
        return "Depth24Plus";
    case TextureFormat::Depth24PlusStencil8:
        return "Depth24PlusStencil8";
    case TextureFormat::Depth32Float:
        return "Depth32Float";
    case TextureFormat::RGBA8Snorm:
        return "RGBA8Snorm";
    case TextureFormat::Invalid:
        break;
    }

    return "Invalid";
}

FormatClass FormatClassOf(lodestone::TextureFormat format) noexcept
{
    switch (format)
    {
    case TextureFormat::Depth16Unorm:
        [[fallthrough]];
    case TextureFormat::Depth24Plus:
        [[fallthrough]];
    case TextureFormat::Depth24PlusStencil8:
        [[fallthrough]];
    case TextureFormat::Depth32Float:
        return FormatClass::Depth;
    case TextureFormat::R32Uint:
        [[fallthrough]];
    case TextureFormat::RG32Uint:
        [[fallthrough]];
    case TextureFormat::RGBA32Uint:
        return FormatClass::Uint;
    case TextureFormat::Invalid:
        return FormatClass::Invalid;
    default:
        break;
    }

    return FormatClass::Float;
}

int32_t ComponentCountOf(lodestone::TextureFormat format) noexcept
{
    switch (format)
    {
    case TextureFormat::R8Unorm:
        [[fallthrough]];
    case TextureFormat::R16Float:
        [[fallthrough]];
    case TextureFormat::R32Float:
        [[fallthrough]];
    case TextureFormat::R32Uint:
        [[fallthrough]];
    case TextureFormat::Depth16Unorm:
        [[fallthrough]];
    case TextureFormat::Depth24Plus:
        [[fallthrough]];
    case TextureFormat::Depth24PlusStencil8:
        [[fallthrough]];
    case TextureFormat::Depth32Float:
        return 1;
    case TextureFormat::RG8Unorm:
        [[fallthrough]];
    case TextureFormat::RG16Float:
        [[fallthrough]];
    case TextureFormat::RG32Float:
        [[fallthrough]];
    case TextureFormat::RG32Uint:
        return 2;
    case TextureFormat::R11G11B10Ufloat:
        return 3;
    case TextureFormat::RGBA8Unorm:
        [[fallthrough]];
    case TextureFormat::RGBA8UnormSrgb:
        [[fallthrough]];
    case TextureFormat::BGRA8Unorm:
        [[fallthrough]];
    case TextureFormat::BGRA8UnormSrgb:
        [[fallthrough]];
    case TextureFormat::RGBA16Float:
        [[fallthrough]];
    case TextureFormat::RGBA32Float:
        [[fallthrough]];
    case TextureFormat::RGBA32Uint:
        [[fallthrough]];
    case TextureFormat::R10G10B10A2Unorm:
        [[fallthrough]];
    case TextureFormat::RGBA8Snorm:
        return 4;
    case TextureFormat::Invalid:
        break;
    }

    return 0;
}

}
