#include "LoomFormats.hpp"
#include "ImageDataFormats.hpp"
#include "ShaderManifestLayout.hpp"
#include <optional>

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

}
