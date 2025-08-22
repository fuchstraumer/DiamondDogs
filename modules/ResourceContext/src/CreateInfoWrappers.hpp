#pragma once
#ifndef RESOURCE_CONTEXT_CREATE_INFO_WRAPPERS_HPP
#define RESOURCE_CONTEXT_CREATE_INFO_WRAPPERS_HPP
#include <vulkan/vulkan_core.h>
#include <vector>
#include <optional>

/*
    Because we take the various vulkan create info structures and stash them away for later, we run into some potential problems -
    mostly around the pointer members of the create info structs. Things like pQueueFamilyIndices especially are problematic, as they're
    dynamically set per runtime instance based on the current logical device... and when set in the message then pushed to our worker thread,
    calling thread will often move on and destroy the temporary local uint32_t array users make before worker thread reads it. 

    Then we get layer validation errors because pQueueFamilyIndices is invalid. Same issue can happen with debug strings, though this is less common
    since those usuaully end up baked into the executable in some form or another.

    These objects store the values and all the data we'll need, and then convert to VkBufferCreateInfo or VkImageCreateInfo with explicit casts when needed.
    These types are what we store in the EnTT containers, and what we associate with our entities.
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
    std::optional<VkExternalMemoryBufferCreateInfo> ExternalMemoryInfo;
    std::optional<VkDedicatedAllocationBufferCreateInfoNV> DedicatedAllocInfoNV;
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
    // potential valid pNext members that we support, which also need to be kept around as deep copies
    // until the API call is made and completed. yay!
    std::optional<VkDedicatedAllocationImageCreateInfoNV> DedicatedAllocInfoNV;
    std::optional<VkExternalMemoryImageCreateInfo> ExternalMemoryInfo;
    std::optional<VkImageStencilUsageCreateInfoEXT> StencilUsageInfo;
    std::optional<VkImageFormatListCreateInfo> FormatListInfo;
    std::optional<VkImageSwapchainCreateInfoKHR> SwapchainInfo;
};

#endif // !RESOURCE_CONTEXT_CREATE_INFO_WRAPPERS_HPP