#pragma once
#ifndef DIAMOND_DOGS_DISPLAY_SYSTEM_SWAPCHAIN_IMPL_HPP
#define DIAMOND_DOGS_DISPLAY_SYSTEM_SWAPCHAIN_IMPL_HPP
#include "PlatformTypes.hpp"
#include "ImageDataFormats.hpp"
#include <cstdint>
#include <vector>
#include <memory>
#include <vulkan/vulkan_core.h>

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
    [[nodiscard]] VkSurfaceFormatKHR FindBestFormat() const noexcept;
    /** @brief Attempts to find format closest to requested format */
    [[nodiscard]] VkSurfaceFormatKHR FindClosestFormat(const VkSurfaceFormatKHR& requested_format) const noexcept;
};

[[nodiscard]] std::vector<SurfaceFormatPair> QuerySurfaceFormatPairs(const SwapchainCreateInfo& create_info);

struct SwapchainImpl
{
    SwapchainImpl(const SwapchainCreateInfo& create_info);
    ~SwapchainImpl();

    void Create(const SwapchainCreateInfo& create_info);
    [[nodiscard]] SwapchainRecreateResult Recreate(const SwapchainCreateInfo& create_info);
    void Destroy();
    void ApplyHdrMetadata(const SwapchainHdrMetadata& metadata);

    /** @brief The create info of the current swapchain, so that a resize can recreate it with the same parameters */
    SwapchainCreateInfo CreateInfo;
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
    std::unique_ptr<SwapchainInfo> Info;

private:
    /** @brief Converts application-specific swapchain information to Vulkan-compatible structure */
    VkSwapchainCreateInfoKHR getCreateInfo(const SwapchainCreateInfo& create_info, VkSwapchainKHR old_swapchain) const noexcept;
    void querySurface(const SwapchainCreateInfo& create_info);
    void chooseSurfaceFormat(const SwapchainCreateInfo& create_info);
    /** @brief Returns the surface's current extent, or the framebuffer size clamped to the surface limits when the
     * surface lets the swapchain decide. Zero area means a minimized window. */
    [[nodiscard]] VkExtent2D chooseExtent(const SwapchainCreateInfo& create_info) const noexcept;
    [[nodiscard]] uint32_t chooseMinImageCount(const SwapchainCreateInfo& create_info) const noexcept;
    void createSwapchainHandle(const SwapchainCreateInfo& create_info, VkSwapchainKHR old_swapchain);
    void setHdrMetadata(const SwapchainCreateInfo& create_info);
    void createSwapchainImages();
    void createSwapchainImageViews();
    void destroySwapchainImageViews();
};

#endif // !DIAMOND_DOGS_DISPLAY_SYSTEM_SWAPCHAIN_IMPL_HPP