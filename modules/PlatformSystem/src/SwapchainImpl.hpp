#pragma once
#ifndef DIAMOND_DOGS_DISPLAY_SYSTEM_SWAPCHAIN_IMPL_HPP
#define DIAMOND_DOGS_DISPLAY_SYSTEM_SWAPCHAIN_IMPL_HPP
#include "PlatformTypes.hpp"
#include <vulkan/vulkan_core.h>
#include <vector>
#include <atomic>
#include <memory>

struct SwapchainInfo
{
    SwapchainInfo(const VkPhysicalDevice& dvc, const VkSurfaceKHR& sfc);
    VkSurfaceCapabilities2KHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
    std::vector<VkColorSpaceKHR> colorSpaces;
    VkSurfaceFormatKHR GetBestFormat(bool tryEnableHDR, ColorSpace preferredSdrColorSpace, ColorSpace preferredHdrColorSpace) const noexcept;
};

struct SwapchainImpl
{
    SwapchainImpl(const SwapchainCreateInfo& createInfo);
    ~SwapchainImpl();

    void Create(const SwapchainCreateInfo& createInfo);
    void Destroy();

    VkDevice ParentDevice;
    VkSurfaceFormatKHR Format;
    VkExtent2D Extent;
    uint32_t MinImageCount;
    uint32_t ImageCount;
    std::vector<VkImage> Images;
    std::vector<VkImageView> ImageViews;
    VkPresentModeKHR PresentMode;

    VkSwapchainKHR Swapchain = VK_NULL_HANDLE;
    // needed to properly recreate the swapchain
    VkSwapchainKHR OldSwapchain = VK_NULL_HANDLE;
    std::unique_ptr<SwapchainInfo> Info;
    std::atomic<bool> ShouldResize{ false };

private:
    /** @brief Converts application-specific swapchain information to Vulkan-compatible structure */
    VkSwapchainCreateInfoKHR GetCreateInfo(const SwapchainCreateInfo& createInfo) const noexcept;
    void CreateSwapchainImages();
    void CreateSwapchainImageViews();
};

#endif // !DIAMOND_DOGS_DISPLAY_SYSTEM_SWAPCHAIN_IMPL_HPP