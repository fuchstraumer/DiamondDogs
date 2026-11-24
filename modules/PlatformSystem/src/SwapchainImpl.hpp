#pragma once
#ifndef DIAMOND_DOGS_DISPLAY_SYSTEM_SWAPCHAIN_IMPL_HPP
#define DIAMOND_DOGS_DISPLAY_SYSTEM_SWAPCHAIN_IMPL_HPP
#include "containers/SmallVector.hpp"
#include "PlatformTypes.hpp"
#include "Swapchain.hpp"
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
[[nodiscard]] VkPresentModeKHR ToVkPresentMode(PresentMode mode) noexcept;
[[nodiscard]] PresentMode FromVkPresentMode(VkPresentModeKHR mode) noexcept;

struct SwapchainImpl
{
    // Presentation (SwapchainPresent.cpp)
    struct PresentFence
    {
        VkFence Fence{ VK_NULL_HANDLE };
        uint64_t Serial{ 0u };      // the present that last used it
        bool Pending{ false }; // todo-ship: maybe use top bits of Serial for this? this adds 7 bytes padding lol
    };

    // An old swapchain that a present may still use. It goes once every present up to LastSerial finished.
    struct RetiredSwapchain
    {
        VkSwapchainKHR Swapchain{ VK_NULL_HANDLE };
        foundation::SmallVector<VkSemaphore, 4> RenderDone;
        uint64_t LastSerial{ 0u };
    };

    // The acquire semaphore of a released image: signaled, never waited. An empty submit waits on it; once Fence
    // signals, the semaphore is free again.
    struct ConsumedSemaphore
    {
        VkSemaphore Semaphore{ VK_NULL_HANDLE };
        VkFence Fence{ VK_NULL_HANDLE };
    };

    SwapchainImpl(const SwapchainCreateInfo& create_info);
    ~SwapchainImpl();

    void Create(const SwapchainCreateInfo& create_info);
    [[nodiscard]] SwapchainRecreateResult Recreate(const SwapchainCreateInfo& create_info);
    void Destroy();
    void ApplyHdrMetadata(const SwapchainHdrMetadata& metadata);

    [[nodiscard]] SwapchainAcquire TryAcquire(uint64_t timeout_ns);
    [[nodiscard]] SwapchainPresentStatus Present(uint32_t image_index);
    [[nodiscard]] SwapchainReleaseStatus Release(uint32_t image_index);
    [[nodiscard]] bool WaitForPresents(uint64_t timeout_ns);
    void CreatePresentation();
    void RetirePresentation(VkSwapchainKHR old_swapchain);
    void DestroyPresentation();

    void InitPresentation();
    [[nodiscard]] PresentModeChange SetPresentMode(VkPresentModeKHR mode);
    void CreateImageView(size_t index);
    // A mode the surface offers and the device enabled the feature for (FIFO latest ready needs one)
    [[nodiscard]] bool PresentModeUsable(VkPresentModeKHR mode) const noexcept;
    
    /** @brief The create info of the current swapchain, so that a resize can recreate it with the same parameters */
    SwapchainCreateInfo CreateInfo;
    std::unique_ptr<SwapchainInfo> Info;
    VkDevice ParentDevice;
    VkSurfaceFormatKHR VulkanFormat;
    VkExtent2D Extent;
    uint32_t MinImageCount;
    uint32_t ImageCount;
    VkPresentModeKHR PresentMode;
    float RefreshRate{ 60.0f };
    VkSwapchainKHR Swapchain = VK_NULL_HANDLE;
    // P2: the modes the current swapchain can switch to, and the create choices for the next Recreate
    bool WantPresentScaling{ true };
    bool WantDeferredMemory{ false };
    bool PresentScalingActive{ false };
    bool DeferredMemoryActive{ false };
    bool CorePresentationOnly{ false };
    bool Maintenance1{ false };
    // we keep both formats around, as it makes conversion and queries easier
    AppSurfaceFormat AppFormat;

    uint32_t NextFence{ 0u };
    uint64_t PresentSerial{ 0u };
    VkQueue PresentQueue{ VK_NULL_HANDLE };
    PFN_vkReleaseSwapchainImagesKHR ReleaseImages{ nullptr };
    foundation::SmallVector<VkImage, 4> Images;
    foundation::SmallVector<VkImageView, 4> ImageViews;
    // free acquire semaphores; one more than the images is enough, and they are made when first needed
    foundation::SmallVector<VkSemaphore, 4> FreeAcquireSemaphores;
    // per image: the semaphore of its last acquire. It is free again when the image is acquired again, because the
    // present of that image waited on work that waited on this semaphore.
    foundation::SmallVector<VkSemaphore, 4> ImageAcquireSemaphores;
    foundation::SmallVector<VkSemaphore, 4> RenderDone;
    foundation::SmallVector<PresentFence, 4> PresentFences;
    foundation::SmallVector<RetiredSwapchain, 4> Retired;
    foundation::SmallVector<ConsumedSemaphore, 4> Consumed;
    foundation::SmallVector<VkFence, 4> FreeFences;
    foundation::SmallVector<VkPresentModeKHR, 8> SwitchableModes; // 7 enum values, 8 for rounding
    // todo-ship: strip this out in release builds
    SwapchainStatistics Statistics;

private:
    void pollPresentation();
    // Fills SwitchableModes for `mode`, and returns the scaling the surface supports with it
    [[nodiscard]] VkSurfacePresentScalingCapabilitiesKHR queryPresentModeSupport(VkPresentModeKHR mode);
    [[nodiscard]] uint64_t completedSerial() const noexcept;
    [[nodiscard]] VkSemaphore takeAcquireSemaphore();
    [[nodiscard]] VkFence takeFence();
    [[nodiscard]] VkFence takePresentFence();

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