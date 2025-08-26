#pragma once
#ifndef RESOURCE_CONTEXT_RESOURCE_DATA_FORMATS_HPP
#define RESOURCE_CONTEXT_RESOURCE_DATA_FORMATS_HPP
#include <cstdint>

// Forward declaration to hide Vulkan headers from users
enum VkFormat : int;

enum class ResourceComponentFormats : uint8_t
{
    // Invalid/undefined format
    Invalid = 0,

    // Single component formats
    R8,
    R16,
    R32,
    
    // Two component formats
    RG8,
    RG16,
    RG32,
    
    // Three component formats  
    RGB8,
    RGB16,
    RGB32,
    
    // Four component formats
    RGBA8,
    RGBA16,
    RGBA32,
    
    // Special packed formats
    A2R10G10B10,  // 2-bit alpha, 10-bit RGB
    A2B10G10R10,  // 2-bit alpha, 10-bit BGR
    R5G6B5,       // 5-bit red, 6-bit green, 5-bit blue
    R5G5B5A1,     // 5-bit RGB, 1-bit alpha
    R4G4B4A4,     // 4-bit RGBA
    
    // High precision formats
    R64,
    RG64,
    RGB64,
    RGBA64,
    
    // Depth/stencil formats
    D16,
    D24,
    D32,
    D24S8,        // 24-bit depth, 8-bit stencil
    D32S8,        // 32-bit depth, 8-bit stencil
    S8,           // 8-bit stencil only
    
    // Compressed formats (common ones)
    BC1,          // DXT1
    BC2,          // DXT3  
    BC3,          // DXT5
    BC4,          // RGTC1
    BC5,          // RGTC2
    BC6H,         // BPTC float
    BC7,          // BPTC
    
    // Add more as needed
    Count
};

enum class ResourceDataType : uint8_t
{
    UNorm,      // Unsigned normalized [0,1]
    SNorm,      // Signed normalized [-1,1]
    UInt,       // Unsigned integer
    SInt,       // Signed integer
    Float,      // Floating point
    SRGB,       // sRGB color space
    
    // Default for most formats
    Default = UNorm
};

struct ResourceFormat
{
    ResourceComponentFormats ComponentFormat{ ResourceComponentFormats::Invalid };
    ResourceDataType DataType{ ResourceDataType::Default };
    
    // Convenience constructors
    constexpr ResourceFormat() = default;
    constexpr ResourceFormat(ResourceComponentFormats components, ResourceDataType dataType = ResourceDataType::Default)
        : ComponentFormat(components), DataType(dataType) {}
    
    // Equality operators for easy comparison
    constexpr bool operator==(const ResourceFormat& other) const noexcept
    {
        return ComponentFormat == other.ComponentFormat && DataType == other.DataType;
    }
    
    constexpr bool operator!=(const ResourceFormat& other) const noexcept
    {
        return !(*this == other);
    }
    
    // Check if format is valid
    constexpr bool IsValid() const noexcept
    {
        return ComponentFormat != ResourceComponentFormats::Invalid;
    }
};

/**
 * @brief Get a ResourceFormat from a format name string and data type
 * @param formatName Format name like "rgba8", "r32", "a2r10g10b10", etc.
 * @param dataType Data type (normalization, integer type, etc.)
 * @return ResourceFormat struct with component format and data type
 * 
 * Examples:
 * - GetResourceFormat("rgba8", ResourceDataType::UNorm) -> RGBA8 with unsigned normalized values
 * - GetResourceFormat("r32", ResourceDataType::Float) -> R32 as floating point
 * - GetResourceFormat("rgba8", ResourceDataType::SRGB) -> RGBA8 in sRGB color space
 * - GetResourceFormat("a2r10g10b10", ResourceDataType::UInt) -> 10-bit RGB, 2-bit alpha as unsigned int
 */
ResourceFormat GetResourceFormat(const char* formatName, ResourceDataType dataType = ResourceDataType::Default) noexcept;

/**
 * @brief Convert ResourceFormat to VkFormat
 * @param format ResourceFormat to convert
 * @return Corresponding VkFormat value
 */
VkFormat ToVkFormat(const ResourceFormat& format) noexcept;

/**
 * @brief Create a ResourceFormat using component format and data type
 * @param components Component layout (e.g., RGBA8, R32, etc.)
 * @param dataType Data type (UNorm, Float, etc.)
 * @return ResourceFormat struct
 */
constexpr ResourceFormat MakeResourceFormat(ResourceComponentFormats components, ResourceDataType dataType = ResourceDataType::Default) noexcept
{
    return ResourceFormat{components, dataType};
}

/**
 * @brief Get format descriptor information for a ResourceFormat
 * @param format ResourceFormat to describe
 * @return ResourceFormatDescriptor with component and size information
 */
ResourceFormatDescriptor GetResourceFormatDescriptor(ResourceFormat format) noexcept;

/**
 * @brief Check if a format is a depth or depth-stencil format
 * @param format ResourceFormat to check
 * @return True if the format contains depth information
 */
constexpr bool IsDepthFormat(const ResourceFormat& format) noexcept
{
    return format.ComponentFormat == ResourceComponentFormats::D16 ||
           format.ComponentFormat == ResourceComponentFormats::D24 ||
           format.ComponentFormat == ResourceComponentFormats::D32 ||
           format.ComponentFormat == ResourceComponentFormats::D24S8 ||
           format.ComponentFormat == ResourceComponentFormats::D32S8;
}

/**
 * @brief Check if a format contains stencil information
 * @param format ResourceFormat to check
 * @return True if the format contains stencil information
 */
constexpr bool IsStencilFormat(const ResourceFormat& format) noexcept
{
    return format.ComponentFormat == ResourceComponentFormats::S8 ||
           format.ComponentFormat == ResourceComponentFormats::D24S8 ||
           format.ComponentFormat == ResourceComponentFormats::D32S8;
}

/**
 * @brief Check if a format is compressed
 * @param format ResourceFormat to check
 * @return True if the format is block-compressed
 */
constexpr bool IsCompressedFormat(const ResourceFormat& format) noexcept
{
    return format.ComponentFormat >= ResourceComponentFormats::BC1 &&
           format.ComponentFormat <= ResourceComponentFormats::BC7;
}

/**
 * @brief Check if a format is in sRGB color space
 * @param format ResourceFormat to check
 * @return True if the format uses sRGB encoding
 */
constexpr bool IsSRGBFormat(const ResourceFormat& format) noexcept
{
    return format.DataType == ResourceDataType::SRGB;
}

// Common format constants for convenience
namespace CommonFormats
{
    // Common color formats
    constexpr ResourceFormat RGBA8_UNorm = {ResourceComponentFormats::RGBA8, ResourceDataType::UNorm};
    constexpr ResourceFormat RGBA8_SRGB = {ResourceComponentFormats::RGBA8, ResourceDataType::SRGB};
    constexpr ResourceFormat RGBA16_Float = {ResourceComponentFormats::RGBA16, ResourceDataType::Float};
    constexpr ResourceFormat RGBA32_Float = {ResourceComponentFormats::RGBA32, ResourceDataType::Float};
    
    constexpr ResourceFormat RGB8_UNorm = {ResourceComponentFormats::RGB8, ResourceDataType::UNorm};
    constexpr ResourceFormat RGB8_SRGB = {ResourceComponentFormats::RGB8, ResourceDataType::SRGB};
    
    constexpr ResourceFormat RG8_UNorm = {ResourceComponentFormats::RG8, ResourceDataType::UNorm};
    constexpr ResourceFormat RG16_Float = {ResourceComponentFormats::RG16, ResourceDataType::Float};
    constexpr ResourceFormat RG32_Float = {ResourceComponentFormats::RG32, ResourceDataType::Float};
    
    constexpr ResourceFormat R8_UNorm = {ResourceComponentFormats::R8, ResourceDataType::UNorm};
    constexpr ResourceFormat R16_Float = {ResourceComponentFormats::R16, ResourceDataType::Float};
    constexpr ResourceFormat R32_Float = {ResourceComponentFormats::R32, ResourceDataType::Float};
    
    // Common depth formats
    constexpr ResourceFormat Depth16 = {ResourceComponentFormats::D16, ResourceDataType::UNorm};
    constexpr ResourceFormat Depth32 = {ResourceComponentFormats::D32, ResourceDataType::Float};
    constexpr ResourceFormat Depth24Stencil8 = {ResourceComponentFormats::D24S8, ResourceDataType::UNorm};
    
    // Common HDR formats
    constexpr ResourceFormat HDR_RGB16 = {ResourceComponentFormats::RGB16, ResourceDataType::Float};
    constexpr ResourceFormat HDR_RGBA16 = {ResourceComponentFormats::RGBA16, ResourceDataType::Float};
    constexpr ResourceFormat HDR_A2R10G10B10 = {ResourceComponentFormats::A2R10G10B10, ResourceDataType::UNorm};
}

#endif //!RESOURCE_CONTEXT_RESOURCE_DATA_FORMATS_HPP
