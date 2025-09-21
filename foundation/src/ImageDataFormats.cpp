#include "ImageDataFormats.hpp"
#include <vulkan/vulkan_core.h>
#include <string_view>
#include <cctype>
#include <algorithm>

namespace
{
    // Helper function for case-insensitive string comparison using string_view
    bool StringsEqual(std::string_view a, std::string_view b) noexcept
    {
        if (a.size() != b.size())
        {
            return false;
        }
        
        return std::equal(a.begin(), a.end(), b.begin(), 
            [](char ca, char cb) {
                return std::tolower(static_cast<unsigned char>(ca)) == 
                       std::tolower(static_cast<unsigned char>(cb));
            });
    }
}

ImageFormat GetImageFormat(const char* formatName, ImageDataType dataType) noexcept
{
    if (!formatName)
    {
        return ImageFormat{ ImageComponentFormats::Invalid, ImageDataType::UNorm };
    }
    
    // Convert to string_view for efficient operations
    const std::string_view name{formatName};
    
    // Single component formats
    if (StringsEqual(name, "r8"))
    {
        return ImageFormat{ ImageComponentFormats::R8, dataType };
    }
    if (StringsEqual(name, "r16"))
    {
        return ImageFormat{ ImageComponentFormats::R16, dataType };
    }
    if (StringsEqual(name, "r32"))
    {
        return ImageFormat{ ImageComponentFormats::R32, dataType };
    }
    if (StringsEqual(name, "r64"))
    {
        return ImageFormat{ ImageComponentFormats::R64, dataType };
    }

    // Two component formats
    if (StringsEqual(name, "rg8"))
    {
        return ImageFormat{ ImageComponentFormats::RG8, dataType };
    }
    if (StringsEqual(name, "rg16"))
    {
        return ImageFormat{ ImageComponentFormats::RG16, dataType };
    }
    if (StringsEqual(name, "rg32"))
    {
        return ImageFormat{ ImageComponentFormats::RG32, dataType };
    }
    if (StringsEqual(name, "rg64"))
    {
        return ImageFormat{ ImageComponentFormats::RG64, dataType };
    }

    // Three component formats
    if (StringsEqual(name, "rgb8"))
    {
        return ImageFormat{ ImageComponentFormats::RGB8, dataType };
    }
    if (StringsEqual(name, "rgb16"))
    {
        return ImageFormat{ ImageComponentFormats::RGB16, dataType };
    }
    if (StringsEqual(name, "rgb32"))
    {
        return ImageFormat{ ImageComponentFormats::RGB32, dataType };
    }
    if (StringsEqual(name, "rgb64"))
    {
        return ImageFormat{ ImageComponentFormats::RGB64, dataType };
    }

    // Four component formats
    if (StringsEqual(name, "rgba8"))
    {
        return ImageFormat{ ImageComponentFormats::RGBA8, dataType };
    }
    if (StringsEqual(name, "rgba16"))
    {
        return ImageFormat{ ImageComponentFormats::RGBA16, dataType };
    }
    if (StringsEqual(name, "rgba32"))
    {
        return ImageFormat{ ImageComponentFormats::RGBA32, dataType };
    }
    if (StringsEqual(name, "rgba64"))
    {
        return ImageFormat{ ImageComponentFormats::RGBA64, dataType };
    }

    // Special packed formats
    if (StringsEqual(name, "a2r10g10b10"))
    {
        return ImageFormat{ ImageComponentFormats::A2R10G10B10, dataType };
    }
    if (StringsEqual(name, "a2b10g10r10"))
    {
        return ImageFormat{ ImageComponentFormats::A2B10G10R10, dataType };
    }
    if (StringsEqual(name, "r5g6b5"))
    {
        return ImageFormat{ ImageComponentFormats::R5G6B5, dataType };
    }
    if (StringsEqual(name, "r5g5b5a1"))
    {
        return ImageFormat{ ImageComponentFormats::R5G5B5A1, dataType };
    }
    if (StringsEqual(name, "r4g4b4a4"))
    {
        return ImageFormat{ ImageComponentFormats::R4G4B4A4, dataType };
    }
        
    // Depth/stencil formats
    if (StringsEqual(name, "d16"))
    {
        return ImageFormat{ ImageComponentFormats::D16, dataType };
    }
    if (StringsEqual(name, "d24"))
    {
        return ImageFormat{ ImageComponentFormats::D24, dataType };
    }
    if (StringsEqual(name, "d32"))
    {
        return ImageFormat{ ImageComponentFormats::D32, dataType };
    }
    if (StringsEqual(name, "d24s8"))
    {
        return ImageFormat{ ImageComponentFormats::D24S8, dataType };
    }
    if (StringsEqual(name, "d32s8"))
    {
        return ImageFormat{ ImageComponentFormats::D32S8, dataType };
    }
    if (StringsEqual(name, "s8"))
    {
        return ImageFormat{ ImageComponentFormats::S8, dataType };
    }
        
    // Compressed formats
    if (StringsEqual(name, "bc1"))
    {
        return ImageFormat{ ImageComponentFormats::BC1, dataType };
    }
    if (StringsEqual(name, "bc2"))
    {
        return ImageFormat{ ImageComponentFormats::BC2, dataType };
    }
    if (StringsEqual(name, "bc3"))
    {
        return ImageFormat{ ImageComponentFormats::BC3, dataType };
    }
    if (StringsEqual(name, "bc4"))
    {
        return ImageFormat{ ImageComponentFormats::BC4, dataType };
    }
    if (StringsEqual(name, "bc5"))
    {
        return ImageFormat{ ImageComponentFormats::BC5, dataType };
    }
    if (StringsEqual(name, "bc6h"))
    {
        return ImageFormat{ ImageComponentFormats::BC6H, dataType };
    }
    if (StringsEqual(name, "bc7"))
    {
        return ImageFormat{ ImageComponentFormats::BC7, dataType };
    }
        
    return ImageFormat{ ImageComponentFormats::Invalid, ImageDataType::UNorm };
}

VkFormat ToVkFormat(const ImageFormat& format) noexcept
{
    const ImageDataType dataType = format.DataType;
    
    switch (format.ComponentFormat)
    {
        case ImageComponentFormats::R8:
            if (dataType == ImageDataType::UInt)
            {
                return VK_FORMAT_R8_UINT;
            }
            else if (dataType == ImageDataType::SInt)
            {
                return VK_FORMAT_R8_SINT;
            }
            else if (dataType == ImageDataType::SNorm)
            {
                return VK_FORMAT_R8_SNORM;
            }
            else if (dataType == ImageDataType::sRGB)
            {
                return VK_FORMAT_R8_SRGB;
            }
            else
            {
                return VK_FORMAT_R8_UNORM; // Default
            }

        case ImageComponentFormats::R16:
            if (dataType == ImageDataType::UInt)
            {
                return VK_FORMAT_R16_UINT;
            }
            else if (dataType == ImageDataType::SInt)
            {
                return VK_FORMAT_R16_SINT;
            }
            else if (dataType == ImageDataType::SNorm)
            {
                return VK_FORMAT_R16_SNORM;
            }
            else if (dataType == ImageDataType::Float)
            {
                return VK_FORMAT_R16_SFLOAT;
            }
            else
            {
                return VK_FORMAT_R16_UNORM; // Default
            }

        case ImageComponentFormats::R32:
            if (dataType == ImageDataType::UInt)
            {
                return VK_FORMAT_R32_UINT;
            }
            else if (dataType == ImageDataType::SInt)
            {
                return VK_FORMAT_R32_SINT;
            }
            else if (dataType == ImageDataType::Float)
            {
                return VK_FORMAT_R32_SFLOAT;
            }
            else
            {
                return VK_FORMAT_R32_SFLOAT; // Default for 32-bit is usually float
            }

        case ImageComponentFormats::R64:
            if (dataType == ImageDataType::UInt)
            {
                return VK_FORMAT_R64_UINT;
            }
            else if (dataType == ImageDataType::SInt)
            {
                return VK_FORMAT_R64_SINT;
            }
            else
            {
                return VK_FORMAT_R64_SFLOAT; // Default
            }
        case ImageComponentFormats::RG8:
            if (dataType == ImageDataType::UInt)
            {
                return VK_FORMAT_R8G8_UINT;
            }
            else if (dataType == ImageDataType::SInt)
            {
                return VK_FORMAT_R8G8_SINT;
            }
            else if (dataType == ImageDataType::SNorm)
            {
                return VK_FORMAT_R8G8_SNORM;
            }
            else if (dataType == ImageDataType::sRGB)
            {
                return VK_FORMAT_R8G8_SRGB;
            }
            else
            {
                return VK_FORMAT_R8G8_UNORM;
            }
            
        case ImageComponentFormats::RG16:
            if (dataType == ImageDataType::UInt)
            {
                return VK_FORMAT_R16G16_UINT;
            }
            else if (dataType == ImageDataType::SInt)
            {
                return VK_FORMAT_R16G16_SINT;
            }
            else if (dataType == ImageDataType::SNorm)
            {
                return VK_FORMAT_R16G16_SNORM;
            }
            else if (dataType == ImageDataType::Float)
            {
                return VK_FORMAT_R16G16_SFLOAT;
            }
            else
            {
                return VK_FORMAT_R16G16_UNORM;
            }

        case ImageComponentFormats::RG32:
            if (dataType == ImageDataType::UInt)
            {
                return VK_FORMAT_R32G32_UINT;
            }
            else if (dataType == ImageDataType::SInt)
            {
                return VK_FORMAT_R32G32_SINT;
            }
            else
            {
                return VK_FORMAT_R32G32_SFLOAT;
            }

        case ImageComponentFormats::RG64:
            if (dataType == ImageDataType::UInt)
            {
                return VK_FORMAT_R64G64_UINT;
            }
            else if (dataType == ImageDataType::SInt)
            {
                return VK_FORMAT_R64G64_SINT;
            }
            else
            {
                return VK_FORMAT_R64G64_SFLOAT;
            }

        case ImageComponentFormats::RGB8:
            if (dataType == ImageDataType::UInt)
            {
                return VK_FORMAT_R8G8B8_UINT;
            }
            else if (dataType == ImageDataType::SInt)
            {
                return VK_FORMAT_R8G8B8_SINT;
            }
            else if (dataType == ImageDataType::SNorm)
            {
                return VK_FORMAT_R8G8B8_SNORM;
            }
            else if (dataType == ImageDataType::sRGB)
            {
                return VK_FORMAT_R8G8B8_SRGB;
            }
            else
            {
                return VK_FORMAT_R8G8B8_UNORM;
            }

        case ImageComponentFormats::RGB16:
            if (dataType == ImageDataType::UInt)
            {
                return VK_FORMAT_R16G16B16_UINT;
            }
            else if (dataType == ImageDataType::SInt)
            {
                return VK_FORMAT_R16G16B16_SINT;
            }
            else if (dataType == ImageDataType::SNorm)
            {
                return VK_FORMAT_R16G16B16_SNORM;
            }
            else if (dataType == ImageDataType::Float)
            {
                return VK_FORMAT_R16G16B16_SFLOAT;
            }
            else
            {
                return VK_FORMAT_R16G16B16_UNORM;
            }

        case ImageComponentFormats::RGB32:
            if (dataType == ImageDataType::UInt)
            {
                return VK_FORMAT_R32G32B32_UINT;
            }
            else if (dataType == ImageDataType::SInt)
            {
                return VK_FORMAT_R32G32B32_SINT;
            }
            else
            {
                return VK_FORMAT_R32G32B32_SFLOAT;
            }

        case ImageComponentFormats::RGB64:
            if (dataType == ImageDataType::UInt)
            {
                return VK_FORMAT_R64G64B64_UINT;
            }
            else if (dataType == ImageDataType::SInt)
            {
                return VK_FORMAT_R64G64B64_SINT;
            }
            else
            {
                return VK_FORMAT_R64G64B64_SFLOAT;
            }

        case ImageComponentFormats::RGBA8:
            if (dataType == ImageDataType::UInt)
            {
                return VK_FORMAT_R8G8B8A8_UINT;
            }
            else if (dataType == ImageDataType::SInt)
            {
                return VK_FORMAT_R8G8B8A8_SINT;
            }
            else if (dataType == ImageDataType::SNorm)
            {
                return VK_FORMAT_R8G8B8A8_SNORM;
            }
            else if (dataType == ImageDataType::sRGB)
            {
                return VK_FORMAT_R8G8B8A8_SRGB;
            }
            else
            {
                return VK_FORMAT_R8G8B8A8_UNORM;
            }

        case ImageComponentFormats::RGBA16:
            if (dataType == ImageDataType::UInt)
            {
                return VK_FORMAT_R16G16B16A16_UINT;
            }
            else if (dataType == ImageDataType::SInt)
            {
                return VK_FORMAT_R16G16B16A16_SINT;
            }
            else if (dataType == ImageDataType::SNorm)
            {
                return VK_FORMAT_R16G16B16A16_SNORM;
            }
            else if (dataType == ImageDataType::Float)
            {
                return VK_FORMAT_R16G16B16A16_SFLOAT;
            }
            else if (dataType == ImageDataType::UNorm)
            {
                return VK_FORMAT_R16G16B16A16_UNORM;
            }
            else
            {
                return VK_FORMAT_UNDEFINED;
            }

        case ImageComponentFormats::RGBA32:
            if (dataType == ImageDataType::UInt)
            {
                return VK_FORMAT_R32G32B32A32_UINT;
            }
            else if (dataType == ImageDataType::SInt)
            {
                return VK_FORMAT_R32G32B32A32_SINT;
            }
            else if (dataType == ImageDataType::Float)
            {
                return VK_FORMAT_R32G32B32A32_SFLOAT;
            }
            else
            {
                return VK_FORMAT_UNDEFINED;
            }
            
        case ImageComponentFormats::RGBA64:
            if (dataType == ImageDataType::UInt)
            {
                return VK_FORMAT_R64G64B64A64_UINT;
            }
            else if (dataType == ImageDataType::SInt)
            {
                return VK_FORMAT_R64G64B64A64_SINT;
            }
            else if (dataType == ImageDataType::Float)
            {
                return VK_FORMAT_R64G64B64A64_SFLOAT;
            }
            else
            {
                return VK_FORMAT_UNDEFINED;
            }

        // Special packed formats
        case ImageComponentFormats::A2R10G10B10:
            if (dataType == ImageDataType::UInt)
            {
                return VK_FORMAT_A2R10G10B10_UINT_PACK32;
            }
            else if (dataType == ImageDataType::SInt)
            {
                return VK_FORMAT_A2R10G10B10_SINT_PACK32;
            }
            else if (dataType == ImageDataType::SNorm)
            {
                return VK_FORMAT_A2R10G10B10_SNORM_PACK32;
            }
            else if (dataType == ImageDataType::UNorm)
            {
                return VK_FORMAT_A2R10G10B10_UNORM_PACK32;
            }
            else
            {
                return VK_FORMAT_UNDEFINED; // Invalid for other types
            }
            
        case ImageComponentFormats::A2B10G10R10:
            if (dataType == ImageDataType::UInt)
            {
                return VK_FORMAT_A2B10G10R10_UINT_PACK32;
            }
            else if (dataType == ImageDataType::SInt)
            {
                return VK_FORMAT_A2B10G10R10_SINT_PACK32;
            }
            else if (dataType == ImageDataType::SNorm)
            {
                return VK_FORMAT_A2B10G10R10_SNORM_PACK32;
            }
            else if (dataType == ImageDataType::UNorm)
            {
                return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
            }
            else
            {
                return VK_FORMAT_UNDEFINED; // Invalid for other types
            }

        case ImageComponentFormats::R5G6B5:
            return VK_FORMAT_R5G6B5_UNORM_PACK16;
            
        case ImageComponentFormats::R5G5B5A1:
            return VK_FORMAT_R5G5B5A1_UNORM_PACK16;
            
        case ImageComponentFormats::R4G4B4A4:
            return VK_FORMAT_R4G4B4A4_UNORM_PACK16;
            
        // Depth/stencil formats
        case ImageComponentFormats::D16:
            return VK_FORMAT_D16_UNORM;
            
        case ImageComponentFormats::D24:
            return VK_FORMAT_X8_D24_UNORM_PACK32;
            
        case ImageComponentFormats::D32:
            return VK_FORMAT_D32_SFLOAT;
            
        case ImageComponentFormats::D24S8:
            return VK_FORMAT_D24_UNORM_S8_UINT;
            
        case ImageComponentFormats::D32S8:
            return VK_FORMAT_D32_SFLOAT_S8_UINT;
            
        case ImageComponentFormats::S8:
            return VK_FORMAT_S8_UINT;
            
        // Compressed formats
        case ImageComponentFormats::BC1:
            if (dataType == ImageDataType::sRGB)
            {
                return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
            }
            else if (dataType == ImageDataType::UNorm)
            {
                return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
            }
            else
            {
                return VK_FORMAT_UNDEFINED; // BC1 is only defined for UNorm and sRGB
            }
            
        case ImageComponentFormats::BC2:
            if (dataType == ImageDataType::sRGB)
            {
                return VK_FORMAT_BC2_SRGB_BLOCK;
            }
            else if (dataType == ImageDataType::UNorm)
            {
                return VK_FORMAT_BC2_UNORM_BLOCK;
            }
            else
            {
                return VK_FORMAT_UNDEFINED; // BC2 is only defined for UNorm and sRGB
            }
            
        case ImageComponentFormats::BC3:
            if (dataType == ImageDataType::sRGB)
            {
                return VK_FORMAT_BC3_SRGB_BLOCK;
            }
            else if (dataType == ImageDataType::UNorm)
            {
                return VK_FORMAT_BC3_UNORM_BLOCK;
            }
            else
            {
                return VK_FORMAT_UNDEFINED; // BC3 is only defined for UNorm and sRGB
            }
            
        case ImageComponentFormats::BC4:
            if (dataType == ImageDataType::SNorm)
            {
                return VK_FORMAT_BC4_SNORM_BLOCK;
            }
            else if (dataType == ImageDataType::UNorm)
            {
                return VK_FORMAT_BC4_UNORM_BLOCK;
            }
            else
            {
                return VK_FORMAT_UNDEFINED; // BC4 is only defined for UNorm and SNorm
            }
            
        case ImageComponentFormats::BC5:
            if (dataType == ImageDataType::SNorm)
            {
                return VK_FORMAT_BC5_SNORM_BLOCK;
            }
            else if (dataType == ImageDataType::UNorm)
            {
                return VK_FORMAT_BC5_UNORM_BLOCK;
            }
            else
            {
                return VK_FORMAT_UNDEFINED; // BC5 is only defined for UNorm and SNorm
            }

        case ImageComponentFormats::BC6H:
            if (dataType == ImageDataType::UFloat)
            {
                return VK_FORMAT_BC6H_UFLOAT_BLOCK;
            }
            else if (dataType == ImageDataType::Float)
            {
                return VK_FORMAT_BC6H_SFLOAT_BLOCK;
            }
            else
            {
                return VK_FORMAT_UNDEFINED; // BC6H is only defined for Float and UFloat
            }
            
        case ImageComponentFormats::BC7:
            if (dataType == ImageDataType::sRGB)
            {
                return VK_FORMAT_BC7_SRGB_BLOCK;
            }
            else if (dataType == ImageDataType::UNorm)
            {
                return VK_FORMAT_BC7_UNORM_BLOCK;
            }
            else
            {
                return VK_FORMAT_UNDEFINED; // BC7 is only defined for UNorm and sRGB
            }
            
        case ImageComponentFormats::Invalid:
            [[fallthrough]];
        default:
            return VK_FORMAT_UNDEFINED;
    }
}

ImageFormat FromVkFormat(VkFormat format) noexcept
{
    switch (format)
    {
        // Single component R8 formats
        case VK_FORMAT_R8_UNORM:
            return ImageFormat{ ImageComponentFormats::R8, ImageDataType::UNorm };
        case VK_FORMAT_R8_SNORM:
            return ImageFormat{ ImageComponentFormats::R8, ImageDataType::SNorm };
        case VK_FORMAT_R8_UINT:
            return ImageFormat{ ImageComponentFormats::R8, ImageDataType::UInt };
        case VK_FORMAT_R8_SINT:
            return ImageFormat{ ImageComponentFormats::R8, ImageDataType::SInt };
        case VK_FORMAT_R8_SRGB:
            return ImageFormat{ ImageComponentFormats::R8, ImageDataType::sRGB };

        // Single component R16 formats
        case VK_FORMAT_R16_UNORM:
            return ImageFormat{ ImageComponentFormats::R16, ImageDataType::UNorm };
        case VK_FORMAT_R16_SNORM:
            return ImageFormat{ ImageComponentFormats::R16, ImageDataType::SNorm };
        case VK_FORMAT_R16_UINT:
            return ImageFormat{ ImageComponentFormats::R16, ImageDataType::UInt };
        case VK_FORMAT_R16_SINT:
            return ImageFormat{ ImageComponentFormats::R16, ImageDataType::SInt };
        case VK_FORMAT_R16_SFLOAT:
            return ImageFormat{ ImageComponentFormats::R16, ImageDataType::Float };

        // Single component R32 formats
        case VK_FORMAT_R32_UINT:
            return ImageFormat{ ImageComponentFormats::R32, ImageDataType::UInt };
        case VK_FORMAT_R32_SINT:
            return ImageFormat{ ImageComponentFormats::R32, ImageDataType::SInt };
        case VK_FORMAT_R32_SFLOAT:
            return ImageFormat{ ImageComponentFormats::R32, ImageDataType::Float };

        // Single component R64 formats
        case VK_FORMAT_R64_UINT:
            return ImageFormat{ ImageComponentFormats::R64, ImageDataType::UInt };
        case VK_FORMAT_R64_SINT:
            return ImageFormat{ ImageComponentFormats::R64, ImageDataType::SInt };
        case VK_FORMAT_R64_SFLOAT:
            return ImageFormat{ ImageComponentFormats::R64, ImageDataType::Float };

        // Two component RG8 formats
        case VK_FORMAT_R8G8_UNORM:
            return ImageFormat{ ImageComponentFormats::RG8, ImageDataType::UNorm };
        case VK_FORMAT_R8G8_SNORM:
            return ImageFormat{ ImageComponentFormats::RG8, ImageDataType::SNorm };
        case VK_FORMAT_R8G8_UINT:
            return ImageFormat{ ImageComponentFormats::RG8, ImageDataType::UInt };
        case VK_FORMAT_R8G8_SINT:
            return ImageFormat{ ImageComponentFormats::RG8, ImageDataType::SInt };
        case VK_FORMAT_R8G8_SRGB:
            return ImageFormat{ ImageComponentFormats::RG8, ImageDataType::sRGB };

        // Two component RG16 formats
        case VK_FORMAT_R16G16_UNORM:
            return ImageFormat{ ImageComponentFormats::RG16, ImageDataType::UNorm };
        case VK_FORMAT_R16G16_SNORM:
            return ImageFormat{ ImageComponentFormats::RG16, ImageDataType::SNorm };
        case VK_FORMAT_R16G16_UINT:
            return ImageFormat{ ImageComponentFormats::RG16, ImageDataType::UInt };
        case VK_FORMAT_R16G16_SINT:
            return ImageFormat{ ImageComponentFormats::RG16, ImageDataType::SInt };
        case VK_FORMAT_R16G16_SFLOAT:
            return ImageFormat{ ImageComponentFormats::RG16, ImageDataType::Float };

        // Two component RG32 formats
        case VK_FORMAT_R32G32_UINT:
            return ImageFormat{ ImageComponentFormats::RG32, ImageDataType::UInt };
        case VK_FORMAT_R32G32_SINT:
            return ImageFormat{ ImageComponentFormats::RG32, ImageDataType::SInt };
        case VK_FORMAT_R32G32_SFLOAT:
            return ImageFormat{ ImageComponentFormats::RG32, ImageDataType::Float };

        // Two component RG64 formats
        case VK_FORMAT_R64G64_UINT:
            return ImageFormat{ ImageComponentFormats::RG64, ImageDataType::UInt };
        case VK_FORMAT_R64G64_SINT:
            return ImageFormat{ ImageComponentFormats::RG64, ImageDataType::SInt };
        case VK_FORMAT_R64G64_SFLOAT:
            return ImageFormat{ ImageComponentFormats::RG64, ImageDataType::Float };

        // Three component RGB8 formats
        case VK_FORMAT_R8G8B8_UNORM:
            return ImageFormat{ ImageComponentFormats::RGB8, ImageDataType::UNorm };
        case VK_FORMAT_R8G8B8_SNORM:
            return ImageFormat{ ImageComponentFormats::RGB8, ImageDataType::SNorm };
        case VK_FORMAT_R8G8B8_UINT:
            return ImageFormat{ ImageComponentFormats::RGB8, ImageDataType::UInt };
        case VK_FORMAT_R8G8B8_SINT:
            return ImageFormat{ ImageComponentFormats::RGB8, ImageDataType::SInt };
        case VK_FORMAT_R8G8B8_SRGB:
            return ImageFormat{ ImageComponentFormats::RGB8, ImageDataType::sRGB };

        // Three component RGB16 formats
        case VK_FORMAT_R16G16B16_UNORM:
            return ImageFormat{ ImageComponentFormats::RGB16, ImageDataType::UNorm };
        case VK_FORMAT_R16G16B16_SNORM:
            return ImageFormat{ ImageComponentFormats::RGB16, ImageDataType::SNorm };
        case VK_FORMAT_R16G16B16_UINT:
            return ImageFormat{ ImageComponentFormats::RGB16, ImageDataType::UInt };
        case VK_FORMAT_R16G16B16_SINT:
            return ImageFormat{ ImageComponentFormats::RGB16, ImageDataType::SInt };
        case VK_FORMAT_R16G16B16_SFLOAT:
            return ImageFormat{ ImageComponentFormats::RGB16, ImageDataType::Float };

        // Three component RGB32 formats
        case VK_FORMAT_R32G32B32_UINT:
            return ImageFormat{ ImageComponentFormats::RGB32, ImageDataType::UInt };
        case VK_FORMAT_R32G32B32_SINT:
            return ImageFormat{ ImageComponentFormats::RGB32, ImageDataType::SInt };
        case VK_FORMAT_R32G32B32_SFLOAT:
            return ImageFormat{ ImageComponentFormats::RGB32, ImageDataType::Float };

        // Three component RGB64 formats
        case VK_FORMAT_R64G64B64_UINT:
            return ImageFormat{ ImageComponentFormats::RGB64, ImageDataType::UInt };
        case VK_FORMAT_R64G64B64_SINT:
            return ImageFormat{ ImageComponentFormats::RGB64, ImageDataType::SInt };
        case VK_FORMAT_R64G64B64_SFLOAT:
            return ImageFormat{ ImageComponentFormats::RGB64, ImageDataType::Float };

        // Four component RGBA8 formats
        case VK_FORMAT_R8G8B8A8_UNORM:
            return ImageFormat{ ImageComponentFormats::RGBA8, ImageDataType::UNorm };
        case VK_FORMAT_R8G8B8A8_SNORM:
            return ImageFormat{ ImageComponentFormats::RGBA8, ImageDataType::SNorm };
        case VK_FORMAT_R8G8B8A8_UINT:
            return ImageFormat{ ImageComponentFormats::RGBA8, ImageDataType::UInt };
        case VK_FORMAT_R8G8B8A8_SINT:
            return ImageFormat{ ImageComponentFormats::RGBA8, ImageDataType::SInt };
        case VK_FORMAT_R8G8B8A8_SRGB:
            return ImageFormat{ ImageComponentFormats::RGBA8, ImageDataType::sRGB };

        // Four component RGBA16 formats
        case VK_FORMAT_R16G16B16A16_UNORM:
            return ImageFormat{ ImageComponentFormats::RGBA16, ImageDataType::UNorm };
        case VK_FORMAT_R16G16B16A16_SNORM:
            return ImageFormat{ ImageComponentFormats::RGBA16, ImageDataType::SNorm };
        case VK_FORMAT_R16G16B16A16_UINT:
            return ImageFormat{ ImageComponentFormats::RGBA16, ImageDataType::UInt };
        case VK_FORMAT_R16G16B16A16_SINT:
            return ImageFormat{ ImageComponentFormats::RGBA16, ImageDataType::SInt };
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return ImageFormat{ ImageComponentFormats::RGBA16, ImageDataType::Float };

        // Four component RGBA32 formats
        case VK_FORMAT_R32G32B32A32_UINT:
            return ImageFormat{ ImageComponentFormats::RGBA32, ImageDataType::UInt };
        case VK_FORMAT_R32G32B32A32_SINT:
            return ImageFormat{ ImageComponentFormats::RGBA32, ImageDataType::SInt };
        case VK_FORMAT_R32G32B32A32_SFLOAT:
            return ImageFormat{ ImageComponentFormats::RGBA32, ImageDataType::Float };

        // Four component RGBA64 formats
        case VK_FORMAT_R64G64B64A64_UINT:
            return ImageFormat{ ImageComponentFormats::RGBA64, ImageDataType::UInt };
        case VK_FORMAT_R64G64B64A64_SINT:
            return ImageFormat{ ImageComponentFormats::RGBA64, ImageDataType::SInt };
        case VK_FORMAT_R64G64B64A64_SFLOAT:
            return ImageFormat{ ImageComponentFormats::RGBA64, ImageDataType::Float };

        // Special packed formats
        case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
            return ImageFormat{ ImageComponentFormats::A2R10G10B10, ImageDataType::UNorm };
        case VK_FORMAT_A2R10G10B10_SNORM_PACK32:
            return ImageFormat{ ImageComponentFormats::A2R10G10B10, ImageDataType::SNorm };
        case VK_FORMAT_A2R10G10B10_UINT_PACK32:
            return ImageFormat{ ImageComponentFormats::A2R10G10B10, ImageDataType::UInt };
        case VK_FORMAT_A2R10G10B10_SINT_PACK32:
            return ImageFormat{ ImageComponentFormats::A2R10G10B10, ImageDataType::SInt };

        case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
            return ImageFormat{ ImageComponentFormats::A2B10G10R10, ImageDataType::UNorm };
        case VK_FORMAT_A2B10G10R10_SNORM_PACK32:
            return ImageFormat{ ImageComponentFormats::A2B10G10R10, ImageDataType::SNorm };
        case VK_FORMAT_A2B10G10R10_UINT_PACK32:
            return ImageFormat{ ImageComponentFormats::A2B10G10R10, ImageDataType::UInt };
        case VK_FORMAT_A2B10G10R10_SINT_PACK32:
            return ImageFormat{ ImageComponentFormats::A2B10G10R10, ImageDataType::SInt };

        case VK_FORMAT_R5G6B5_UNORM_PACK16:
            return ImageFormat{ ImageComponentFormats::R5G6B5, ImageDataType::UNorm };

        case VK_FORMAT_R5G5B5A1_UNORM_PACK16:
            return ImageFormat{ ImageComponentFormats::R5G5B5A1, ImageDataType::UNorm };

        case VK_FORMAT_R4G4B4A4_UNORM_PACK16:
            return ImageFormat{ ImageComponentFormats::R4G4B4A4, ImageDataType::UNorm };

        // Depth/stencil formats
        case VK_FORMAT_D16_UNORM:
            return ImageFormat{ ImageComponentFormats::D16, ImageDataType::UNorm };

        case VK_FORMAT_X8_D24_UNORM_PACK32:
            return ImageFormat{ ImageComponentFormats::D24, ImageDataType::UNorm };

        case VK_FORMAT_D32_SFLOAT:
            return ImageFormat{ ImageComponentFormats::D32, ImageDataType::Float };

        case VK_FORMAT_D24_UNORM_S8_UINT:
            return ImageFormat{ ImageComponentFormats::D24S8, ImageDataType::UNorm };

        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return ImageFormat{ ImageComponentFormats::D32S8, ImageDataType::Float };

        case VK_FORMAT_S8_UINT:
            return ImageFormat{ ImageComponentFormats::S8, ImageDataType::UInt };

        // Compressed formats
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
            return ImageFormat{ ImageComponentFormats::BC1, ImageDataType::UNorm };
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
            return ImageFormat{ ImageComponentFormats::BC1, ImageDataType::sRGB };

        case VK_FORMAT_BC2_UNORM_BLOCK:
            return ImageFormat{ ImageComponentFormats::BC2, ImageDataType::UNorm };
        case VK_FORMAT_BC2_SRGB_BLOCK:
            return ImageFormat{ ImageComponentFormats::BC2, ImageDataType::sRGB };

        case VK_FORMAT_BC3_UNORM_BLOCK:
            return ImageFormat{ ImageComponentFormats::BC3, ImageDataType::UNorm };
        case VK_FORMAT_BC3_SRGB_BLOCK:
            return ImageFormat{ ImageComponentFormats::BC3, ImageDataType::sRGB };

        case VK_FORMAT_BC4_UNORM_BLOCK:
            return ImageFormat{ ImageComponentFormats::BC4, ImageDataType::UNorm };
        case VK_FORMAT_BC4_SNORM_BLOCK:
            return ImageFormat{ ImageComponentFormats::BC4, ImageDataType::SNorm };

        case VK_FORMAT_BC5_UNORM_BLOCK:
            return ImageFormat{ ImageComponentFormats::BC5, ImageDataType::UNorm };
        case VK_FORMAT_BC5_SNORM_BLOCK:
            return ImageFormat{ ImageComponentFormats::BC5, ImageDataType::SNorm };

        case VK_FORMAT_BC6H_UFLOAT_BLOCK:
            return ImageFormat{ ImageComponentFormats::BC6H, ImageDataType::UInt };
        case VK_FORMAT_BC6H_SFLOAT_BLOCK:
            return ImageFormat{ ImageComponentFormats::BC6H, ImageDataType::SInt };

        case VK_FORMAT_BC7_UNORM_BLOCK:
            return ImageFormat{ ImageComponentFormats::BC7, ImageDataType::UNorm };
        case VK_FORMAT_BC7_SRGB_BLOCK:
            return ImageFormat{ ImageComponentFormats::BC7, ImageDataType::sRGB };

        // BGRA formats (commonly used for swapchains)
        case VK_FORMAT_B8G8R8A8_UNORM:
            return ImageFormat{ ImageComponentFormats::BGRA8, ImageDataType::UNorm };
        case VK_FORMAT_B8G8R8A8_SNORM:
            return ImageFormat{ ImageComponentFormats::BGRA8, ImageDataType::SNorm };
        case VK_FORMAT_B8G8R8A8_UINT:
            return ImageFormat{ ImageComponentFormats::BGRA8, ImageDataType::UInt };
        case VK_FORMAT_B8G8R8A8_SINT:
            return ImageFormat{ ImageComponentFormats::BGRA8, ImageDataType::SInt };
        case VK_FORMAT_B8G8R8A8_SRGB:
            return ImageFormat{ ImageComponentFormats::BGRA8, ImageDataType::sRGB };

        // BGR formats (no alpha) - map to RGB8 since we don't have separate BGR enum
        case VK_FORMAT_B8G8R8_UNORM:
            return ImageFormat{ ImageComponentFormats::RGB8, ImageDataType::UNorm };
        case VK_FORMAT_B8G8R8_SNORM:
            return ImageFormat{ ImageComponentFormats::RGB8, ImageDataType::SNorm };
        case VK_FORMAT_B8G8R8_UINT:
            return ImageFormat{ ImageComponentFormats::RGB8, ImageDataType::UInt };
        case VK_FORMAT_B8G8R8_SINT:
            return ImageFormat{ ImageComponentFormats::RGB8, ImageDataType::SInt };
        case VK_FORMAT_B8G8R8_SRGB:
            return ImageFormat{ ImageComponentFormats::RGB8, ImageDataType::sRGB };

        // Invalid/undefined formats
        case VK_FORMAT_UNDEFINED:
            [[fallthrough]];
        default:
            return ImageFormat{ ImageComponentFormats::Invalid, ImageDataType::Invalid };
    }
}
