#pragma once
#ifndef FOUNDATION_IMAGE_DATA_FORMATS_HPP
#define FOUNDATION_IMAGE_DATA_FORMATS_HPP
#include <cstdint>

// Forward declaration to hide Vulkan headers from users
enum VkFormat : int;

enum class ImageComponentFormats : uint8_t
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
    BRGA8,      // BGRA ordering
    BGRA8,      // BGRA ordering
    RGBA16,
    RGBA32,
    
    // Special packed formats
    A2R10G10B10,  // 2-bit alpha, 10-bit RGB
    A2B10G10R10,  // 2-bit alpha, 10-bit BGR
    R5G6B5,       // 5-bit red, 6-bit green, 5-bit blue
    R5G5B5A1,     // 5-bit RGB, 1-bit alpha
    R4G4B4A4,     // 4-bit RGBA
    B10G11R11,    // 10-bit BGR, 11-bit green, 11-bit red, UNORM PACK32 (HDR colorbuffer)

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

enum class ImageDataType : uint8_t
{
    Invalid = 0,
    UNorm,      // Unsigned normalized [0,1]
    SNorm,      // Signed normalized [-1,1]
    UInt,       // Unsigned integer
    SInt,       // Signed integer
    Float,      // Floating point
    sRGB,       // sRGB color space
    
    // Default for most formats
    Default = UNorm
};

struct ImageFormat
{
    ImageComponentFormats ComponentFormat{ ImageComponentFormats::Invalid };
    ImageDataType DataType{ ImageDataType::Default };
    
    // Convenience constructors
    constexpr ImageFormat() = default;
    constexpr ImageFormat(ImageComponentFormats components, ImageDataType dataType = ImageDataType::Default)
        : ComponentFormat(components), DataType(dataType) {}
    
    // Equality operators for easy comparison
    constexpr bool operator==(const ImageFormat& other) const noexcept
    {
        return ComponentFormat == other.ComponentFormat && DataType == other.DataType;
    }

    constexpr bool operator!=(const ImageFormat& other) const noexcept
    {
        return !(*this == other);
    }
    
    // Check if format is valid
    constexpr bool IsValid() const noexcept
    {
        return ComponentFormat != ImageComponentFormats::Invalid;
    }
};

struct ImageFormatDescriptor
{

};

/**
 * @brief Get a ImageFormat from a format name string and data type
 * @param formatName Format name like "rgba8", "r32", "a2r10g10b10", etc.
 * @param dataType Data type (normalization, integer type, etc.)
 * @return ImageFormat struct with component format and data type
 *
 * Examples:
 * - GetImageFormat("rgba8", ImageDataType::UNorm) -> RGBA8 with unsigned normalized values
 * - GetImageFormat("r32", ImageDataType::Float) -> R32 as floating point
 * - GetImageFormat("rgba8", ImageDataType::SRGB) -> RGBA8 in sRGB color space
 * - GetImageFormat("a2r10g10b10", ImageDataType::UInt) -> 10-bit RGB, 2-bit alpha as unsigned int
 */
ImageFormat GetImageFormat(const char* formatName, ImageDataType dataType = ImageDataType::Default) noexcept;

/**
 * @brief Convert ImageFormat to VkFormat
 * @param format ImageFormat to convert
 * @return Corresponding VkFormat value
 */
VkFormat ToVkFormat(const ImageFormat& format) noexcept;

/**
 * @brief Create a ResourceFormat using component format and data type
 * @param components Component layout (e.g., RGBA8, R32, etc.)
 * @param dataType Data type (UNorm, Float, etc.)
 * @return ImageFormat struct
 */
constexpr ImageFormat MakeImageFormat(ImageComponentFormats components, ImageDataType dataType = ImageDataType::Default) noexcept
{
    return ImageFormat{components, dataType};
}

/**
 * @brief Get format descriptor information for a ImageFormat
 * @param format ImageFormat to describe
 * @return ImageFormatDescriptor with component and size information
 */
ImageFormatDescriptor GetResourceFormatDescriptor(ImageFormat format) noexcept;

/**
 * @brief Check if a format is a depth or depth-stencil format
 * @param format ResourceFormat to check
 * @return True if the format contains depth information
 */
constexpr bool IsDepthFormat(const ImageFormat& format) noexcept
{
    return format.ComponentFormat == ImageComponentFormats::D16 ||
           format.ComponentFormat == ImageComponentFormats::D24 ||
           format.ComponentFormat == ImageComponentFormats::D32 ||
           format.ComponentFormat == ImageComponentFormats::D24S8 ||
           format.ComponentFormat == ImageComponentFormats::D32S8;
}

/**
 * @brief Check if a format contains stencil information
 * @param format ResourceFormat to check
 * @return True if the format contains stencil information
 */
constexpr bool IsStencilFormat(const ImageFormat& format) noexcept
{
    return format.ComponentFormat == ImageComponentFormats::S8 ||
           format.ComponentFormat == ImageComponentFormats::D24S8 ||
           format.ComponentFormat == ImageComponentFormats::D32S8;
}

/**
 * @brief Check if a format is compressed
 * @param format ResourceFormat to check
 * @return True if the format is block-compressed
 */
constexpr bool IsCompressedFormat(const ImageFormat& format) noexcept
{
    return format.ComponentFormat >= ImageComponentFormats::BC1 &&
           format.ComponentFormat <= ImageComponentFormats::BC7;
}

/**
 * @brief Check if a format is in sRGB color space
 * @param format ResourceFormat to check
 * @return True if the format uses sRGB encoding
 */
constexpr bool IsSRGBFormat(const ImageFormat& format) noexcept
{
    return format.DataType == ImageDataType::SRGB;
}

// Common format constants for convenience
namespace CommonFormats
{
    // Common color formats
    constexpr ImageFormat RGBA8_UNorm = {ImageComponentFormats::RGBA8, ImageDataType::UNorm};
    constexpr ImageFormat RGBA8_SRGB = {ImageComponentFormats::RGBA8, ImageDataType::SRGB};
    constexpr ImageFormat RGBA16_Float = {ImageComponentFormats::RGBA16, ImageDataType::Float};
    constexpr ImageFormat RGBA32_Float = {ImageComponentFormats::RGBA32, ImageDataType::Float};

    constexpr ImageFormat RGB8_UNorm = {ImageComponentFormats::RGB8, ImageDataType::UNorm};
    constexpr ImageFormat RGB8_SRGB = {ImageComponentFormats::RGB8, ImageDataType::SRGB};

    constexpr ImageFormat RG8_UNorm = {ImageComponentFormats::RG8, ImageDataType::UNorm};
    constexpr ImageFormat RG16_Float = {ImageComponentFormats::RG16, ImageDataType::Float};
    constexpr ImageFormat RG32_Float = {ImageComponentFormats::RG32, ImageDataType::Float};

    constexpr ImageFormat R8_UNorm = {ImageComponentFormats::R8, ImageDataType::UNorm};
    constexpr ImageFormat R16_Float = {ImageComponentFormats::R16, ImageDataType::Float};
    constexpr ImageFormat R32_Float = {ImageComponentFormats::R32, ImageDataType::Float};

    // Common depth formats
    constexpr ImageFormat Depth16 = {ImageComponentFormats::D16, ImageDataType::UNorm};
    constexpr ImageFormat Depth32 = {ImageComponentFormats::D32, ImageDataType::Float};
    constexpr ImageFormat Depth24Stencil8 = {ImageComponentFormats::D24S8, ImageDataType::UNorm};

    // Common HDR formats
    constexpr ImageFormat HDR_RGB16 = {ImageComponentFormats::RGB16, ImageDataType::Float};
    constexpr ImageFormat HDR_RGBA16 = {ImageComponentFormats::RGBA16, ImageDataType::Float};
    constexpr ImageFormat HDR_A2R10G10B10 = {ImageComponentFormats::A2R10G10B10, ImageDataType::UNorm};
    constexpr ImageFormat HDR_B10G11R11 = {ImageComponentFormats::B10G11R11, ImageDataType::UNorm};
}

#endif //!FOUNDATION_IMAGE_DATA_FORMATS_HPP
