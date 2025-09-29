#pragma once
#ifndef RESOURCE_CONTEXT_CREATE_INFO_WRAPPERS_HPP
#define RESOURCE_CONTEXT_CREATE_INFO_WRAPPERS_HPP
#include "ResourceTypes.hpp"
#include <vulkan/vulkan_core.h>
#include <vector>
#include <optional>

/*
    Originally we made these to deal with some of the quirks of the Vulkan create info structs and their pointer members, but after the API
    change to take our custom create info objects that abstract away the Vulkan API details from users, these wrappers have become less critical.
    However, they still serve as a useful bridge between our internal representations and the Vulkan API, especially for handling pNext chains
    and other Vulkan-specific nuances (as we are not yet fully API portable internally, even if our higher-level abstractions mostly are by now).
*/

struct ResourceContextBufferCreateInfo
{
    ResourceContextBufferCreateInfo() = default;
    ResourceContextBufferCreateInfo(const VkBufferCreateInfo& vk_buffer_info);
    // explicit cast back to buffer create info for API calls
    explicit operator VkBufferCreateInfo() const noexcept;
    VkBufferCreateFlags CreateFlags{ 0u };
    VkDeviceSize Size{ 0u };
    VkBufferUsageFlags UsageFlags{ 0u };
    VkSharingMode SharingMode{ VK_SHARING_MODE_EXCLUSIVE };
    std::vector<uint32_t> QueueFamilyIndices;
    // potential valid pNext members
    std::optional<VkBufferUsageFlags2CreateInfo> UsageFlags2Info;
    std::optional<VkBufferDeviceAddressCreateInfoEXT> DeviceAddressInfo;
};

struct ResourceContextImageCreateInfo
{
    ResourceContextImageCreateInfo() = default;
    ResourceContextImageCreateInfo(const VkImageCreateInfo& vk_image_info);
    // explicit cast back to image create info for API calls
    explicit operator VkImageCreateInfo() const noexcept;
    VkImageCreateFlags CreateFlags{ 0u };
    VkImageType ImageType{ VK_IMAGE_TYPE_2D };
    VkFormat Format{ VK_FORMAT_UNDEFINED };
    VkExtent3D Extent{ 0, 0, 0 };
    uint32_t MipLevels{ 1 };
    uint32_t ArrayLayers{ 1 };
    VkSampleCountFlagBits Samples{ VK_SAMPLE_COUNT_1_BIT };
    VkImageTiling Tiling{ VK_IMAGE_TILING_OPTIMAL };
    VkImageUsageFlags UsageFlags{ 0 };
    VkSharingMode SharingMode{ VK_SHARING_MODE_EXCLUSIVE };
    std::vector<uint32_t> QueueFamilyIndices;
    VkImageLayout InitialLayout{ VK_IMAGE_LAYOUT_UNDEFINED };
    std::optional<VkImageSwapchainCreateInfoKHR> SwapchainInfo;
};

#endif // !RESOURCE_CONTEXT_CREATE_INFO_WRAPPERS_HPP