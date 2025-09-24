#pragma once
#ifndef DIAMOND_DOGS_DISPLAY_SYSTEM_SWAPCHAIN_IMPL_HPP
#define DIAMOND_DOGS_DISPLAY_SYSTEM_SWAPCHAIN_IMPL_HPP
#include "PlatformTypes.hpp"
#include <vulkan/vulkan_core.h>
#include <vector>
#include <atomic>
#include <memory>

struct AppSurfaceFormat
{
    ImageFormat Format;
    ColorSpace Space;
};

struct SwapchainInfo
{
    SwapchainInfo(const VkPhysicalDevice& dvc, const VkSurfaceKHR& sfc);
    VkSurfaceCapabilities2KHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
    std::vector<VkColorSpaceKHR> colorSpaces;
    /** @brief Attempts to find best format using a hueristic weighing bit depth and color spaces. Doesn't take any user input, used for default cases */
    VkSurfaceFormatKHR FindBestFormat() const noexcept;
    /** @brief Attempts to find format closest to requested format */
    VkSurfaceFormatKHR FindClosestFormat(const VkSurfaceFormatKHR& requestedFormat) const noexcept;
};

struct SwapchainImpl
{
    SwapchainImpl(const SwapchainCreateInfo& createInfo);
    ~SwapchainImpl();

    void Create(const SwapchainCreateInfo& createInfo);
    void Destroy();

    VkDevice ParentDevice;
    // we keep both formats around, as it makes conversion and queries easier
    AppSurfaceFormat AppFormat;
    VkSurfaceFormatKHR VulkanFormat;
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