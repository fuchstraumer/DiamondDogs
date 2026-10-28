#include "VulkanConversions.hpp"
#include "ImageDataFormatsVulkan.hpp"
#include "ResourceFlags.hpp"
#include "ResourceTypes.hpp"
#include "vulkan/vulkan_core.h"
#include <cstdint>

namespace
{
    [[nodiscard]] bool HasAny(BufferUsageBits bits, BufferUsageBits mask) noexcept;
    [[nodiscard]] bool HasAny(ImageUsageBits bits, ImageUsageBits mask) noexcept;
    [[nodiscard]] bool HasAny(ResourceCreationFlags flags, ResourceCreationFlags mask) noexcept;
    [[nodiscard]] VkComponentSwizzle ToVkSwizzle(ComponentSwizzle swizzle) noexcept;

    constexpr BufferUsageBits k_DeviceAddressUsages =
        BufferUsageBits::AccelerationStructureBuild |
        BufferUsageBits::ShaderBindingTable |
        BufferUsageBits::AccelerationStructureBuildScratch;
}

bool BufferUsageIsSupported(BufferUsageBits usage) noexcept
{
    return !HasAny(usage, k_DeviceAddressUsages);
}

VkBufferUsageFlags ToVkBufferUsage(BufferUsageBits usage, bool has_texel_view) noexcept
{
    VkBufferUsageFlags result = 0u;
    if (HasAny(usage, BufferUsageBits::CopySource))
    {
        result |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    }
    if (HasAny(usage, BufferUsageBits::CopyDestination))
    {
        result |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }
    if (HasAny(usage, BufferUsageBits::AllShaderUBO))
    {
        result |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    }
    if (HasAny(usage, BufferUsageBits::AllShaderSRV | BufferUsageBits::AllShaderUAV))
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
    if (HasAny(usage, BufferUsageBits::VertexOrIndex))
    {
        result |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    }
    if (HasAny(usage, BufferUsageBits::AllIndirect))
    {
        result |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    }
    if (HasAny(usage, BufferUsageBits::ConditionalRendering))
    {
        result |= VK_BUFFER_USAGE_CONDITIONAL_RENDERING_BIT_EXT;
    }
    if (HasAny(usage, BufferUsageBits::DeviceAddressBuffer))
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
    if (HasAny(usage, ImageUsageBits::ShadingRateImage))
    {
        result |= VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;
    }
    return result;
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
        ToVkSwizzle(mapping.R),
        ToVkSwizzle(mapping.G),
        ToVkSwizzle(mapping.B),
        ToVkSwizzle(mapping.A),
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
    // ImageCompareOp matches VkCompareOp value for value, Never and None both being 0
    return static_cast<VkCompareOp>(op);
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
    if (HasAny(bits, ImageUsageBits::PresentSource))
    {
        return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }
    // as of VK_KHR_synchronization2, we can use VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL for all read-only usages of our attachments and driver will distinguish between depth/RTV
    else if (HasAny(bits, ImageUsageBits::AllAttachmentRead) && !HasAny(bits, ImageUsageBits::AllAttachmentWrite))
    {
        return VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
    }
    else if (HasAny(bits, ImageUsageBits::AllAttachmentWrite))
    {
        // If any attachment write flag is set, we use the VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL layout introduced in Vulkan 1.2 and synchronization2.
        return VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
    }
    else if (HasAny(bits, ImageUsageBits::AllShaderUAV))
    {
        // storage images must be in GENERAL. this is checked before the read-only case, so that an image that is
        // both sampled and written by shaders stays in a layout that permits both
        return VK_IMAGE_LAYOUT_GENERAL;
    }
    else if (HasAny(bits, ImageUsageBits::AllShaderSRV | ImageUsageBits::SampledImage))
    {
        return VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
    }
    else if (HasAny(bits, ImageUsageBits::ShadingRateImage))
    {
        return VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR;
    }
    // the transfer bits come last: they name a layout only when they are the whole usage. an uploaded texture also
    // carries TransferDestination, and its resting layout is the shader one
    else if (HasAny(bits, ImageUsageBits::TransferDestination))
    {
        return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    }
    else if (HasAny(bits, ImageUsageBits::TransferSource))
    {
        return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    }
    return VK_IMAGE_LAYOUT_UNDEFINED;
}

VkImageLayout ImageLayoutAfterTransfer(ImageUsageBits bits) noexcept
{
    const VkImageLayout layout = ImageLayoutFromUsageBits(bits);
    return layout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_IMAGE_LAYOUT_GENERAL : layout;
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
        default:
            return VK_COMPONENT_SWIZZLE_IDENTITY;
        }
    }
}
