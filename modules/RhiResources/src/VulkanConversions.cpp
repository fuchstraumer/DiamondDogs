#include "VulkanConversions.hpp"
#include "ImageDataFormatsVulkan.hpp"
#include "ResourceBarriers.hpp"
#include "RhiResourceFlags.hpp"
#include "ResourceTypes.hpp"
#include "vulkan/vulkan_core.h"
#include <algorithm>
#include <cstdint>
#include <span>

namespace
{
    [[nodiscard]] bool HasAny(BufferUsageBits bits, BufferUsageBits mask) noexcept;
    [[nodiscard]] bool HasAny(ImageUsageBits bits, ImageUsageBits mask) noexcept;
    [[nodiscard]] bool HasAny(ResourceCreationFlags flags, ResourceCreationFlags mask) noexcept;
    [[nodiscard]] VkComponentSwizzle ToVkSwizzle(ComponentSwizzle swizzle) noexcept;

    // each of these needs a device feature or extension that the context does not enable today
    constexpr BufferUsageBits k_FeatureGatedBufferUsages =
        BufferUsageBits::AllAccelerationStructure |
        BufferUsageBits::AllMicromap |
        BufferUsageBits::ShaderBindingTable |
        BufferUsageBits::TransformFeedback |
        BufferUsageBits::TransformFeedbackCounter |
        BufferUsageBits::GeneratedCommandsInput |
        BufferUsageBits::GeneratedCommandsPreprocess;

    // VK_IMAGE_USAGE_FRAGMENT_DENSITY_MAP_BIT_EXT is not a valid usage unless the extension is enabled
    constexpr ImageUsageBits k_FeatureGatedImageUsages = ImageUsageBits::FragmentDensityMapRead;
}

bool BufferUsageIsSupported(BufferUsageBits usage, bool device_addresses_enabled) noexcept
{
    if (HasAny(usage, k_FeatureGatedBufferUsages))
    {
        return false;
    }

    return device_addresses_enabled || !HasAny(usage, BufferUsageBits::DeviceAddress);
}

bool ImageUsageIsSupported(ImageUsageBits usage) noexcept
{
    return !HasAny(usage, k_FeatureGatedImageUsages);
}

VkBufferUsageFlags ToVkBufferUsage(BufferUsageBits usage, bool has_texel_view) noexcept
{
    VkBufferUsageFlags result = 0u;
    if (HasAny(usage, BufferUsageBits::TransferSource))
    {
        result |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    }

    if (HasAny(usage, BufferUsageBits::TransferDestination))
    {
        result |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }

    if (HasAny(usage, BufferUsageBits::AllShaderUBO))
    {
        result |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    }

    if (HasAny(usage, BufferUsageBits::AllShaderStorage))
    {
        result |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    }

    if (has_texel_view && HasAny(usage, BufferUsageBits::AllShaderSRV | BufferUsageBits::AllShaderUBO))
    {
        result |= VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
    }

    if (has_texel_view && HasAny(usage, BufferUsageBits::AllShaderUAV))
    {
        result |= VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
    }

    if (HasAny(usage, BufferUsageBits::IndexInput))
    {
        result |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    }

    if (HasAny(usage, BufferUsageBits::VertexInput))
    {
        result |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    }

    // device-generated commands read their command stream through the indirect buffer usage. The preprocess buffer of
    // VK_EXT_device_generated_commands needs the 64-bit usage word, and the create refuses these bits today
    if (HasAny(usage, BufferUsageBits::AllIndirect | BufferUsageBits::GeneratedCommandsInput | BufferUsageBits::GeneratedCommandsPreprocess))
    {
        result |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    }

    if (HasAny(usage, BufferUsageBits::ConditionalRendering))
    {
        result |= VK_BUFFER_USAGE_CONDITIONAL_RENDERING_BIT_EXT;
    }

    if (HasAny(usage, BufferUsageBits::TransformFeedback))
    {
        result |= VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT;
    }

    if (HasAny(usage, BufferUsageBits::TransformFeedbackCounter))
    {
        result |= VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_COUNTER_BUFFER_BIT_EXT;
    }

    if (HasAny(usage, BufferUsageBits::AccelerationStructureStorage))
    {
        result |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
    }

    if (HasAny(usage, BufferUsageBits::AccelerationStructureBuildInput))
    {
        result |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    }

    // a scratch buffer is a storage buffer that the build reaches by address
    if (HasAny(usage, BufferUsageBits::AccelerationStructureBuildScratch))
    {
        result |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    }

    if (HasAny(usage, BufferUsageBits::ShaderBindingTable))
    {
        result |= VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR;
    }

    if (HasAny(usage, BufferUsageBits::MicromapStorage))
    {
        result |= VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT;
    }

    if (HasAny(usage, BufferUsageBits::MicromapBuildInput))
    {
        result |= VK_BUFFER_USAGE_MICROMAP_BUILD_INPUT_READ_ONLY_BIT_EXT;
    }

    if (HasAny(usage, BufferUsageBits::DeviceAddress))
    {
        result |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    }

    return result;
}

VkImageUsageFlags ToVkImageUsage(ImageUsageBits usage) noexcept
{
    VkImageUsageFlags result = 0u;
    if (HasAny(usage, ImageUsageBits::TransferSource))
    {
        result |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }

    if (HasAny(usage, ImageUsageBits::TransferDestination))
    {
        result |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }

    if (HasAny(usage, ImageUsageBits::AllShaderSRV | ImageUsageBits::SampledImage))
    {
        result |= VK_IMAGE_USAGE_SAMPLED_BIT;
    }

    if (HasAny(usage, ImageUsageBits::AllShaderUAV))
    {
        result |= VK_IMAGE_USAGE_STORAGE_BIT;
    }

    if (HasAny(usage, ImageUsageBits::AllRenderTarget))
    {
        result |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }

    if (HasAny(usage, ImageUsageBits::AllDsRead | ImageUsageBits::AllDsWrite))
    {
        result |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }

    if (HasAny(usage, ImageUsageBits::InputAttachmentRead))
    {
        result |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
    }

    if (HasAny(usage, ImageUsageBits::ShadingRateRead))
    {
        result |= VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;
    }

    if (HasAny(usage, ImageUsageBits::FragmentDensityMapRead))
    {
        result |= VK_IMAGE_USAGE_FRAGMENT_DENSITY_MAP_BIT_EXT;
    }

    return result;
}

VkBufferCreateInfo ToVkBufferCreateInfo(const BufferCreateInfo& create_info, std::span<const uint32_t> queue_family_indices) noexcept
{
    return VkBufferCreateInfo
    {
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0u,
        static_cast<VkDeviceSize>(create_info.Size),
        ToVkBufferUsage(create_info.Usage, create_info.ViewFormat.has_value()),
        queue_family_indices.size() > 1u ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
        static_cast<uint32_t>(queue_family_indices.size()),
        queue_family_indices.data()
    };
}

VkImageCreateInfo ToVkImageCreateInfo(const ImageCreateInfo& create_info, std::span<const uint32_t> queue_family_indices) noexcept
{
    const bool isCube = create_info.ViewInfo.has_value() && create_info.ViewInfo->ViewType == ImageViewType::Cube;

    return VkImageCreateInfo
    {
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        nullptr,
        isCube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0u,
        ToVkImageType(create_info.Type),
        ToVkFormat(create_info.Format),
        VkExtent3D{ create_info.Width, std::max(create_info.Height, 1u), std::max(create_info.Depth, 1u) },
        create_info.MipLevels,
        create_info.ArrayLayers,
        // the VkSampleCountFlagBits values are the counts themselves
        static_cast<VkSampleCountFlagBits>(std::max(create_info.SampleCount, 1u)),
        VK_IMAGE_TILING_OPTIMAL,
        ToVkImageUsage(create_info.Usage),
        queue_family_indices.size() > 1u ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
        static_cast<uint32_t>(queue_family_indices.size()),
        queue_family_indices.data(),
        VK_IMAGE_LAYOUT_UNDEFINED
    };
}

VkImageType ToVkImageType(ImageType type) noexcept
{
    switch (type)
    {
    case ImageType::Type1D:
        return VK_IMAGE_TYPE_1D;
    case ImageType::Type3D:
        return VK_IMAGE_TYPE_3D;
    default:
        return VK_IMAGE_TYPE_2D;
    }
}

VkImageViewType ToVkImageViewType(ImageViewType type) noexcept
{
    switch (type)
    {
    case ImageViewType::Type1D:
        return VK_IMAGE_VIEW_TYPE_1D;
    case ImageViewType::Type1DArray:
        return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
    case ImageViewType::Type2DArray:
        return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    case ImageViewType::Type3D:
        return VK_IMAGE_VIEW_TYPE_3D;
    case ImageViewType::Cube:
        return VK_IMAGE_VIEW_TYPE_CUBE;
    case ImageViewType::CubeArray:
        return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    default:
        return VK_IMAGE_VIEW_TYPE_2D;
    }
}

VkImageViewType DefaultVkImageViewType(ImageType type, uint32_t array_layers) noexcept
{
    const bool isArray = array_layers > 1u;
    switch (type)
    {
    case ImageType::Type1D:
        return isArray ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D;
    case ImageType::Type3D:
        return VK_IMAGE_VIEW_TYPE_3D;
    default:
        return isArray ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
    }
}

VkImageAspectFlags ToVkImageAspect(ImageAspectFlags aspect) noexcept
{
    VkImageAspectFlags result = 0u;
    if ((aspect & ImageAspectFlags::Color) != ImageAspectFlags::None)
    {
        result |= VK_IMAGE_ASPECT_COLOR_BIT;
    }
    if ((aspect & ImageAspectFlags::Depth) != ImageAspectFlags::None)
    {
        result |= VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    if ((aspect & ImageAspectFlags::Stencil) != ImageAspectFlags::None)
    {
        result |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    return result;
}

ImageAspectFlags AspectFromVkFormat(VkFormat format) noexcept
{
    switch (format)
    {
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_X8_D24_UNORM_PACK32:
    case VK_FORMAT_D32_SFLOAT:
        return ImageAspectFlags::Depth;
    case VK_FORMAT_S8_UINT:
        return ImageAspectFlags::Stencil;
    case VK_FORMAT_D16_UNORM_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return ImageAspectFlags::DepthStencil;
    default:
        return ImageAspectFlags::Color;
    }
}

VkComponentMapping ToVkComponentMapping(const ImageComponentMapping& mapping) noexcept
{
    return VkComponentMapping
    {
        ToVkSwizzle(mapping.Red()),
        ToVkSwizzle(mapping.Green()),
        ToVkSwizzle(mapping.Blue()),
        ToVkSwizzle(mapping.Alpha()),
    };
}

VkFilter ToVkFilter(ImageFilteringMode mode) noexcept
{
    switch (mode)
    {
    case ImageFilteringMode::Nearest:
        return VK_FILTER_NEAREST;
    case ImageFilteringMode::Cubic:
        return VK_FILTER_CUBIC_EXT;
    default:
        return VK_FILTER_LINEAR;
    }
}

VkSamplerMipmapMode ToVkMipmapMode(ImageFilteringMode mode) noexcept
{
    return mode == ImageFilteringMode::Nearest ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
}

VkSamplerAddressMode ToVkAddressMode(SamplerCreateInfo::AddressMode mode) noexcept
{
    switch (mode)
    {
    case SamplerCreateInfo::AddressMode::MirroredRepeat:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case SamplerCreateInfo::AddressMode::ClampToEdge:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case SamplerCreateInfo::AddressMode::ClampToBorder:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    case SamplerCreateInfo::AddressMode::MirrorClampToEdge:
        return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
    default:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

VkBorderColor ToVkBorderColor(SamplerCreateInfo::ImageBorderColor color) noexcept
{
    switch (color)
    {
    case SamplerCreateInfo::ImageBorderColor::IntTransparentBlack:
        return VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
    case SamplerCreateInfo::ImageBorderColor::FloatOpaqueBlack:
        return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    case SamplerCreateInfo::ImageBorderColor::IntOpaqueBlack:
        return VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    case SamplerCreateInfo::ImageBorderColor::FloatOpaqueWhite:
        return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    case SamplerCreateInfo::ImageBorderColor::IntOpaqueWhite:
        return VK_BORDER_COLOR_INT_OPAQUE_WHITE;
    default:
        // todo: FloatCustom and IntCustom need the custom border color member, which SamplerCreateInfo does not have yet
        return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    }
}

VkCompareOp ToVkCompareOp(ImageCompareOp op) noexcept
{
    switch (op)
    {
    case ImageCompareOp::None:
        return VK_COMPARE_OP_MAX_ENUM;
    case ImageCompareOp::Never:
        return VK_COMPARE_OP_NEVER;
    case ImageCompareOp::Less:
        return VK_COMPARE_OP_LESS;
    case ImageCompareOp::Equal:
        return VK_COMPARE_OP_EQUAL;
    case ImageCompareOp::LessOrEqual:
        return VK_COMPARE_OP_LESS_OR_EQUAL;
    case ImageCompareOp::Greater:
        return VK_COMPARE_OP_GREATER;
    case ImageCompareOp::NotEqual:
        return VK_COMPARE_OP_NOT_EQUAL;
    case ImageCompareOp::GreaterOrEqual:
        return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case ImageCompareOp::Always:
        return VK_COMPARE_OP_ALWAYS;
    default:
        return VK_COMPARE_OP_MAX_ENUM;
    }
}

VkSamplerCreateInfo ToVkSamplerCreateInfo(const SamplerCreateInfo& create_info) noexcept
{
    return VkSamplerCreateInfo
    {
        VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        nullptr,
        0u,
        ToVkFilter(create_info.MagFilter),
        ToVkFilter(create_info.MinFilter),
        ToVkMipmapMode(create_info.MipFilter),
        ToVkAddressMode(create_info.AddressU),
        ToVkAddressMode(create_info.AddressV),
        ToVkAddressMode(create_info.AddressW),
        create_info.MipLoadBias,
        create_info.AnisotropyEnabled ? VK_TRUE : VK_FALSE,
        create_info.MaxAnisotropy,
        create_info.CompareEnabled ? VK_TRUE : VK_FALSE,
        ToVkCompareOp(create_info.CompareOp),
        create_info.MinLod,
        create_info.MaxLod,
        ToVkBorderColor(create_info.BorderColor),
        create_info.UnnormalizedCoordinates ? VK_TRUE : VK_FALSE,
    };
}

VkImageLayout ImageLayoutFromUsageBits(ImageUsageBits bits) noexcept
{
    // rhi::ImageLayout holds the VkImageLayout values (the RhiSystem conversions check that), so this is a cast
    return static_cast<VkImageLayout>(ToUnderlying(ImageLayoutOfUsage(bits)));
}

VmaMemoryUsage ToVmaMemoryUsage(ResourceDomain domain) noexcept
{
    switch (domain)
    {
    case ResourceDomain::GPUOnly:
        return VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    case ResourceDomain::CPUOnly:
    case ResourceDomain::GPUToCPU:
        return VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    default:
        return VMA_MEMORY_USAGE_AUTO;
    }
}

VmaAllocationCreateFlags ToVmaAllocationFlags(ResourceDomain domain, ResourceCreationFlags flags) noexcept
{
    VmaAllocationCreateFlags result = 0u;

    if (HasAny(flags, ResourceCreationFlags::DedicatedMemory))
    {
        result |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    }

    if (HasAny(flags, ResourceCreationFlags::CreateMapped | ResourceCreationFlags::PersistentlyMapped))
    {
        result |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    if (HasAny(flags, ResourceCreationFlags::HostWritesRandom))
    {
        result |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
    }
    else if (HasAny(flags, ResourceCreationFlags::HostWritesLinear | ResourceCreationFlags::PersistentlyMapped))
    {
        result |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    }
    else if (domain == ResourceDomain::GPUToCPU)
    {
        // readback: the host reads, and sequential-write memory may be uncached
        result |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
    }
    else if (DomainIsHostVisible(domain))
    {
        result |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    }

    return result;
}

bool DomainIsHostVisible(ResourceDomain domain) noexcept
{
    return domain == ResourceDomain::CPUOnly || domain == ResourceDomain::CPUToGPU || domain == ResourceDomain::GPUToCPU;
}

namespace
{
    bool HasAny(BufferUsageBits bits, BufferUsageBits mask) noexcept
    {
        return (bits & mask) != BufferUsageBits::Invalid;
    }

    bool HasAny(ImageUsageBits bits, ImageUsageBits mask) noexcept
    {
        return (bits & mask) != ImageUsageBits::Invalid;
    }

    bool HasAny(ResourceCreationFlags flags, ResourceCreationFlags mask) noexcept
    {
        return (flags & mask) != ResourceCreationFlags::None;
    }

    VkComponentSwizzle ToVkSwizzle(ComponentSwizzle swizzle) noexcept
    {
        switch (swizzle)
        {
        case ComponentSwizzle::Red:
            return VK_COMPONENT_SWIZZLE_R;
        case ComponentSwizzle::Green:
            return VK_COMPONENT_SWIZZLE_G;
        case ComponentSwizzle::Blue:
            return VK_COMPONENT_SWIZZLE_B;
        case ComponentSwizzle::Alpha:
            return VK_COMPONENT_SWIZZLE_A;
        case ComponentSwizzle::Zero:
            return VK_COMPONENT_SWIZZLE_ZERO;
        case ComponentSwizzle::One:
            return VK_COMPONENT_SWIZZLE_ONE;
        default:
            return VK_COMPONENT_SWIZZLE_IDENTITY;
        }
    }
}
