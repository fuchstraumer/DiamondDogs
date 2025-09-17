#include "ResourceDataFormats.hpp"
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

ResourceFormat GetResourceFormat(const char* formatName, ResourceDataType dataType) noexcept
{
    if (!formatName)
    {
        return ResourceFormat{ ResourceComponentFormats::Invalid, ResourceDataType::UNorm };
    }
    
    // Convert to string_view for efficient operations
    const std::string_view name{formatName};
    
    // Single component formats
    if (StringsEqual(name, "r8"))
        return ResourceFormat{ ResourceComponentFormats::R8, dataType };
    if (StringsEqual(name, "r16"))
        return ResourceFormat{ ResourceComponentFormats::R16, dataType };
    if (StringsEqual(name, "r32"))
        return ResourceFormat{ ResourceComponentFormats::R32, dataType };
    if (StringsEqual(name, "r64"))
        return ResourceFormat{ ResourceComponentFormats::R64, dataType };

    // Two component formats
    if (StringsEqual(name, "rg8"))
        return ResourceFormat{ ResourceComponentFormats::RG8, dataType };
    if (StringsEqual(name, "rg16"))
        return ResourceFormat{ ResourceComponentFormats::RG16, dataType };
    if (StringsEqual(name, "rg32"))
        return ResourceFormat{ ResourceComponentFormats::RG32, dataType };
    if (StringsEqual(name, "rg64"))
        return ResourceFormat{ ResourceComponentFormats::RG64, dataType };

    // Three component formats
    if (StringsEqual(name, "rgb8"))
        return ResourceFormat{ ResourceComponentFormats::RGB8, dataType };
    if (StringsEqual(name, "rgb16"))
        return ResourceFormat{ ResourceComponentFormats::RGB16, dataType };
    if (StringsEqual(name, "rgb32"))
        return ResourceFormat{ ResourceComponentFormats::RGB32, dataType };
    if (StringsEqual(name, "rgb64"))
        return ResourceFormat{ ResourceComponentFormats::RGB64, dataType };

    // Four component formats
    if (StringsEqual(name, "rgba8"))
        return ResourceFormat{ ResourceComponentFormats::RGBA8, dataType };
    if (StringsEqual(name, "rgba16"))
        return ResourceFormat{ ResourceComponentFormats::RGBA16, dataType };
    if (StringsEqual(name, "rgba32"))
        return ResourceFormat{ ResourceComponentFormats::RGBA32, dataType };
    if (StringsEqual(name, "rgba64"))
        return ResourceFormat{ ResourceComponentFormats::RGBA64, dataType };

    // Special packed formats
    if (StringsEqual(name, "a2r10g10b10"))
        return ResourceFormat{ ResourceComponentFormats::A2R10G10B10, dataType };
    if (StringsEqual(name, "a2b10g10r10"))
        return ResourceFormat{ ResourceComponentFormats::A2B10G10R10, dataType };
    if (StringsEqual(name, "r5g6b5"))
        return ResourceFormat{ ResourceComponentFormats::R5G6B5, dataType };
    if (StringsEqual(name, "r5g5b5a1"))
        return ResourceFormat{ ResourceComponentFormats::R5G5B5A1, dataType };
    if (StringsEqual(name, "r4g4b4a4"))
        return ResourceFormat{ ResourceComponentFormats::R4G4B4A4, dataType };
        
    // Depth/stencil formats
    if (StringsEqual(name, "d16"))
        return ResourceFormat{ ResourceComponentFormats::D16, dataType };
    if (StringsEqual(name, "d24"))
        return ResourceFormat{ ResourceComponentFormats::D24, dataType };
    if (StringsEqual(name, "d32"))
        return ResourceFormat{ ResourceComponentFormats::D32, dataType };
    if (StringsEqual(name, "d24s8"))
        return ResourceFormat{ ResourceComponentFormats::D24S8, dataType };
    if (StringsEqual(name, "d32s8"))
        return ResourceFormat{ ResourceComponentFormats::D32S8, dataType };
    if (StringsEqual(name, "s8"))
        return ResourceFormat{ ResourceComponentFormats::S8, dataType };
        
    // Compressed formats
    if (StringsEqual(name, "bc1"))
        return ResourceFormat{ ResourceComponentFormats::BC1, dataType };
    if (StringsEqual(name, "bc2"))
        return ResourceFormat{ ResourceComponentFormats::BC2, dataType };
    if (StringsEqual(name, "bc3"))
        return ResourceFormat{ ResourceComponentFormats::BC3, dataType };
    if (StringsEqual(name, "bc4"))
        return ResourceFormat{ ResourceComponentFormats::BC4, dataType };
    if (StringsEqual(name, "bc5"))
        return ResourceFormat{ ResourceComponentFormats::BC5, dataType };
    if (StringsEqual(name, "bc6h"))
        return ResourceFormat{ ResourceComponentFormats::BC6H, dataType };
    if (StringsEqual(name, "bc7"))
        return ResourceFormat{ ResourceComponentFormats::BC7, dataType };
        
    return ResourceFormat{ ResourceComponentFormats::Invalid, ResourceDataType::UNorm };
}

VkFormat ToVkFormat(const ResourceFormat& format) noexcept
{
    const ResourceDataType dataType = format.DataType;
    
    switch (format.ComponentFormat)
    {
        case ResourceComponentFormats::R8:
            if (dataType == ResourceDataType::UInt) return VK_FORMAT_R8_UINT;
            if (dataType == ResourceDataType::SInt) return VK_FORMAT_R8_SINT;
            if (dataType == ResourceDataType::SNorm) return VK_FORMAT_R8_SNORM;
            if (dataType == ResourceDataType::SRGB) return VK_FORMAT_R8_SRGB;
            return VK_FORMAT_R8_UNORM; // Default
            
        case ResourceComponentFormats::R16:
            if (dataType == ResourceDataType::UInt) return VK_FORMAT_R16_UINT;
            if (dataType == ResourceDataType::SInt) return VK_FORMAT_R16_SINT;
            if (dataType == ResourceDataType::SNorm) return VK_FORMAT_R16_SNORM;
            if (dataType == ResourceDataType::Float) return VK_FORMAT_R16_SFLOAT;
            return VK_FORMAT_R16_UNORM; // Default
            
        case ResourceComponentFormats::R32:
            if (dataType == ResourceDataType::UInt) return VK_FORMAT_R32_UINT;
            if (dataType == ResourceDataType::SInt) return VK_FORMAT_R32_SINT;
            if (dataType == ResourceDataType::Float) return VK_FORMAT_R32_SFLOAT;
            return VK_FORMAT_R32_SFLOAT; // Default for 32-bit is usually float
            
        case ResourceComponentFormats::R64:
            if (dataType == ResourceDataType::UInt) return VK_FORMAT_R64_UINT;
            if (dataType == ResourceDataType::SInt) return VK_FORMAT_R64_SINT;
            return VK_FORMAT_R64_SFLOAT; // Default
            
        case ResourceComponentFormats::RG8:
            if (dataType == ResourceDataType::UInt) return VK_FORMAT_R8G8_UINT;
            if (dataType == ResourceDataType::SInt) return VK_FORMAT_R8G8_SINT;
            if (dataType == ResourceDataType::SNorm) return VK_FORMAT_R8G8_SNORM;
            if (dataType == ResourceDataType::SRGB) return VK_FORMAT_R8G8_SRGB;
            return VK_FORMAT_R8G8_UNORM;
            
        case ResourceComponentFormats::RG16:
            if (dataType == ResourceDataType::UInt) return VK_FORMAT_R16G16_UINT;
            if (dataType == ResourceDataType::SInt) return VK_FORMAT_R16G16_SINT;
            if (dataType == ResourceDataType::SNorm) return VK_FORMAT_R16G16_SNORM;
            if (dataType == ResourceDataType::Float) return VK_FORMAT_R16G16_SFLOAT;
            return VK_FORMAT_R16G16_UNORM;
            
        case ResourceComponentFormats::RG32:
            if (dataType == ResourceDataType::UInt) return VK_FORMAT_R32G32_UINT;
            if (dataType == ResourceDataType::SInt) return VK_FORMAT_R32G32_SINT;
            return VK_FORMAT_R32G32_SFLOAT;
            
        case ResourceComponentFormats::RG64:
            if (dataType == ResourceDataType::UInt) return VK_FORMAT_R64G64_UINT;
            if (dataType == ResourceDataType::SInt) return VK_FORMAT_R64G64_SINT;
            return VK_FORMAT_R64G64_SFLOAT;
            
        case ResourceComponentFormats::RGB8:
            if (dataType == ResourceDataType::UInt) return VK_FORMAT_R8G8B8_UINT;
            if (dataType == ResourceDataType::SInt) return VK_FORMAT_R8G8B8_SINT;
            if (dataType == ResourceDataType::SNorm) return VK_FORMAT_R8G8B8_SNORM;
            if (dataType == ResourceDataType::SRGB) return VK_FORMAT_R8G8B8_SRGB;
            return VK_FORMAT_R8G8B8_UNORM;
            
        case ResourceComponentFormats::RGB16:
            if (dataType == ResourceDataType::UInt) return VK_FORMAT_R16G16B16_UINT;
            if (dataType == ResourceDataType::SInt) return VK_FORMAT_R16G16B16_SINT;
            if (dataType == ResourceDataType::SNorm) return VK_FORMAT_R16G16B16_SNORM;
            if (dataType == ResourceDataType::Float) return VK_FORMAT_R16G16B16_SFLOAT;
            return VK_FORMAT_R16G16B16_UNORM;
            
        case ResourceComponentFormats::RGB32:
            if (dataType == ResourceDataType::UInt) return VK_FORMAT_R32G32B32_UINT;
            if (dataType == ResourceDataType::SInt) return VK_FORMAT_R32G32B32_SINT;
            return VK_FORMAT_R32G32B32_SFLOAT;
            
        case ResourceComponentFormats::RGB64:
            if (dataType == ResourceDataType::UInt) return VK_FORMAT_R64G64B64_UINT;
            if (dataType == ResourceDataType::SInt) return VK_FORMAT_R64G64B64_SINT;
            return VK_FORMAT_R64G64B64_SFLOAT;
            
        case ResourceComponentFormats::RGBA8:
            if (dataType == ResourceDataType::UInt) return VK_FORMAT_R8G8B8A8_UINT;
            if (dataType == ResourceDataType::SInt) return VK_FORMAT_R8G8B8A8_SINT;
            if (dataType == ResourceDataType::SNorm) return VK_FORMAT_R8G8B8A8_SNORM;
            if (dataType == ResourceDataType::SRGB) return VK_FORMAT_R8G8B8A8_SRGB;
            return VK_FORMAT_R8G8B8A8_UNORM;
            
        case ResourceComponentFormats::RGBA16:
            if (dataType == ResourceDataType::UInt) return VK_FORMAT_R16G16B16A16_UINT;
            if (dataType == ResourceDataType::SInt) return VK_FORMAT_R16G16B16A16_SINT;
            if (dataType == ResourceDataType::SNorm) return VK_FORMAT_R16G16B16A16_SNORM;
            if (dataType == ResourceDataType::Float) return VK_FORMAT_R16G16B16A16_SFLOAT;
            return VK_FORMAT_R16G16B16A16_UNORM;
            
        case ResourceComponentFormats::RGBA32:
            if (dataType == ResourceDataType::UInt) return VK_FORMAT_R32G32B32A32_UINT;
            if (dataType == ResourceDataType::SInt) return VK_FORMAT_R32G32B32A32_SINT;
            return VK_FORMAT_R32G32B32A32_SFLOAT;
            
        case ResourceComponentFormats::RGBA64:
            if (dataType == ResourceDataType::UInt) return VK_FORMAT_R64G64B64A64_UINT;
            if (dataType == ResourceDataType::SInt) return VK_FORMAT_R64G64B64A64_SINT;
            return VK_FORMAT_R64G64B64A64_SFLOAT;
            
        // Special packed formats
        case ResourceComponentFormats::A2R10G10B10:
            if (dataType == ResourceDataType::UInt) return VK_FORMAT_A2R10G10B10_UINT_PACK32;
            if (dataType == ResourceDataType::SInt) return VK_FORMAT_A2R10G10B10_SINT_PACK32;
            if (dataType == ResourceDataType::SNorm) return VK_FORMAT_A2R10G10B10_SNORM_PACK32;
            return VK_FORMAT_A2R10G10B10_UNORM_PACK32;
            
        case ResourceComponentFormats::A2B10G10R10:
            if (dataType == ResourceDataType::UInt) return VK_FORMAT_A2B10G10R10_UINT_PACK32;
            if (dataType == ResourceDataType::SInt) return VK_FORMAT_A2B10G10R10_SINT_PACK32;
            if (dataType == ResourceDataType::SNorm) return VK_FORMAT_A2B10G10R10_SNORM_PACK32;
            return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
            
        case ResourceComponentFormats::R5G6B5:
            return VK_FORMAT_R5G6B5_UNORM_PACK16;
            
        case ResourceComponentFormats::R5G5B5A1:
            return VK_FORMAT_R5G5B5A1_UNORM_PACK16;
            
        case ResourceComponentFormats::R4G4B4A4:
            return VK_FORMAT_R4G4B4A4_UNORM_PACK16;
            
        // Depth/stencil formats
        case ResourceComponentFormats::D16:
            return VK_FORMAT_D16_UNORM;
            
        case ResourceComponentFormats::D24:
            return VK_FORMAT_X8_D24_UNORM_PACK32;
            
        case ResourceComponentFormats::D32:
            return VK_FORMAT_D32_SFLOAT;
            
        case ResourceComponentFormats::D24S8:
            return VK_FORMAT_D24_UNORM_S8_UINT;
            
        case ResourceComponentFormats::D32S8:
            return VK_FORMAT_D32_SFLOAT_S8_UINT;
            
        case ResourceComponentFormats::S8:
            return VK_FORMAT_S8_UINT;
            
        // Compressed formats
        case ResourceComponentFormats::BC1:
            if (dataType == ResourceDataType::SRGB) return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
            return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
            
        case ResourceComponentFormats::BC2:
            if (dataType == ResourceDataType::SRGB) return VK_FORMAT_BC2_SRGB_BLOCK;
            return VK_FORMAT_BC2_UNORM_BLOCK;
            
        case ResourceComponentFormats::BC3:
            if (dataType == ResourceDataType::SRGB) return VK_FORMAT_BC3_SRGB_BLOCK;
            return VK_FORMAT_BC3_UNORM_BLOCK;
            
        case ResourceComponentFormats::BC4:
            if (dataType == ResourceDataType::SNorm) return VK_FORMAT_BC4_SNORM_BLOCK;
            return VK_FORMAT_BC4_UNORM_BLOCK;
            
        case ResourceComponentFormats::BC5:
            if (dataType == ResourceDataType::SNorm) return VK_FORMAT_BC5_SNORM_BLOCK;
            return VK_FORMAT_BC5_UNORM_BLOCK;
            
        case ResourceComponentFormats::BC6H:
            if (dataType == ResourceDataType::SInt) return VK_FORMAT_BC6H_SFLOAT_BLOCK;
            return VK_FORMAT_BC6H_UFLOAT_BLOCK;
            
        case ResourceComponentFormats::BC7:
            if (dataType == ResourceDataType::SRGB) return VK_FORMAT_BC7_SRGB_BLOCK;
            return VK_FORMAT_BC7_UNORM_BLOCK;
            
        case ResourceComponentFormats::Invalid:
        default:
            return VK_FORMAT_UNDEFINED;
    }
}
