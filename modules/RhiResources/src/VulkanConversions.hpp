#pragma once
#ifndef RESOURCE_CONTEXT_VULKAN_CONVERSIONS_HPP
#define RESOURCE_CONTEXT_VULKAN_CONVERSIONS_HPP
#include "ResourceFlags.hpp"
#include "ResourceTypes.hpp"
#include <vulkan/vulkan_core.h>
#include <vk_mem_alloc.h>

/*
    Replaces CreateInfoWrappers. Those copied Vulkan create infos (and their pNext chains) into the message queue,
    back when the public API took Vulkan structs. The public API now takes our own create infos, so the only Vulkan
    work left is translating those into Vulkan values at the point of creation.
*/

/** @brief Returns false for usages that need buffer device address: DeviceAddressBuffer and the raytracing usages.
 *  Those are refused until the device address and bindless design is settled. */
[[nodiscard]] bool BufferUsageIsSupported(BufferUsageBits usage) noexcept;

/** @brief Returns the Vulkan usage flags for the usage bits. Transfer usage comes only from CopySource and
 *  CopyDestination: the flags have a cost on some hardware, so they are not added to every buffer. */
[[nodiscard]] VkBufferUsageFlags ToVkBufferUsage(BufferUsageBits usage, bool has_texel_view) noexcept;
/** @brief Returns the Vulkan usage flags for the usage bits. Transfer usage comes only from TransferSource and
 *  TransferDestination. */
[[nodiscard]] VkImageUsageFlags ToVkImageUsage(ImageUsageBits usage) noexcept;

[[nodiscard]] VkImageType ToVkImageType(ImageType type) noexcept;
[[nodiscard]] VkImageViewType ToVkImageViewType(ImageViewType type) noexcept;
/** @brief Returns the default view type for an image of this type with this many array layers. */
[[nodiscard]] VkImageViewType DefaultVkImageViewType(ImageType type, uint32_t array_layers) noexcept;
[[nodiscard]] VkImageAspectFlags ToVkImageAspect(ImageAspectFlags aspect) noexcept;
/** @brief Returns Depth, Stencil, or both for depth and stencil formats, and Color for every other format. */
[[nodiscard]] ImageAspectFlags AspectFromVkFormat(VkFormat format) noexcept;
[[nodiscard]] VkComponentMapping ToVkComponentMapping(const ImageComponentMapping& mapping) noexcept;

[[nodiscard]] VkFilter ToVkFilter(ImageFilteringMode mode) noexcept;
[[nodiscard]] VkSamplerMipmapMode ToVkMipmapMode(ImageFilteringMode mode) noexcept;
[[nodiscard]] VkSamplerAddressMode ToVkAddressMode(SamplerCreateInfo::AddressMode mode) noexcept;
[[nodiscard]] VkBorderColor ToVkBorderColor(SamplerCreateInfo::ImageBorderColor color) noexcept;
[[nodiscard]] VkCompareOp ToVkCompareOp(ImageCompareOp op) noexcept;
[[nodiscard]] VkSamplerCreateInfo ToVkSamplerCreateInfo(const SamplerCreateInfo& create_info) noexcept;

/** @brief Returns the image layout that the usage bits need. Used by the barriers and by the transfer system, which
 *  moves an uploaded image into this layout. */
[[nodiscard]] VkImageLayout ImageLayoutFromUsageBits(ImageUsageBits bits) noexcept;
/** @brief Returns the layout that the transfer system leaves an image in: the usage's layout, or GENERAL when the usage
 *  names no layout. ResourceContextImpl records the same value, so that the next transfer starts from it. */
[[nodiscard]] VkImageLayout ImageLayoutAfterTransfer(ImageUsageBits bits) noexcept;

[[nodiscard]] VmaMemoryUsage ToVmaMemoryUsage(ResourceDomain domain) noexcept;
/** @brief Returns the allocation flags for the creation flags. Host-visible domains get a host access flag when the
 *  creation flags name none, because VMA_MEMORY_USAGE_AUTO only picks mappable memory when one is set. */
[[nodiscard]] VmaAllocationCreateFlags ToVmaAllocationFlags(ResourceDomain domain, ResourceCreationFlags flags) noexcept;
[[nodiscard]] bool DomainIsHostVisible(ResourceDomain domain) noexcept;

#endif // !RESOURCE_CONTEXT_VULKAN_CONVERSIONS_HPP
