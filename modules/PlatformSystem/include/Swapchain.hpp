#pragma once
#ifndef DIAMOND_DOGS_PLATFORM_SYSTEM_SWAPCHAIN_HPP
#define DIAMOND_DOGS_PLATFORM_SYSTEM_SWAPCHAIN_HPP
#include "PlatformTypes.hpp"
#include "ImageDataFormats.hpp"
#include "math/Math.hpp"
#include <cstdint>
#include <chrono>
#include <memory>
#include <vector>

struct SwapchainImpl;

class Swapchain
{
public:
    Swapchain(const SwapchainCreateInfo& create_info);
    ~Swapchain();
    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;
    Swapchain(Swapchain&& other) noexcept;
    Swapchain& operator=(Swapchain&& other) noexcept;

    /** @brief Returns the pairs that the surface offers, in driver order, without a swapchain. Pairs that this module
     * does not map are left out. */
    [[nodiscard]] static std::vector<SurfaceFormatPair> QuerySurfaceFormats(const SwapchainCreateInfo& create_info);

    /** @brief Takes swapchain create info as we may have adjusted parameters that caused the recreation to occur.
     *
     * The new swapchain takes the surface's current extent, and is created with the old one as its `oldSwapchain`.
     * The handles that this object returns change, but the object itself stays valid. The caller must make sure that
     * the device has finished all work that uses the old images (`vkDeviceWaitIdle` is enough) before this call.
     */
    [[nodiscard]] SwapchainRecreateResult Recreate(const SwapchainCreateInfo& create_info);
    /** @brief Recreates with the create info that the swapchain was last created with. Used after a resize. */
    [[nodiscard]] SwapchainRecreateResult Recreate();
    /** @brief Required as part of the swapchain lifecycle, for handling recreation events */
    void Destroy();

    /** @brief Get the native swapchain handle. @return `VkSwapchainKHR` as `void*` */
    [[nodiscard]] void* GetNativeHandle() const noexcept;

    [[nodiscard]] math::Float2 GetExtent() const noexcept;
    [[nodiscard]] uint32_t ImageCount() const noexcept;
    [[nodiscard]] ImageFormat ImageFormat() const noexcept;
    [[nodiscard]] ColorSpace ColorSpace() const noexcept;
    [[nodiscard]] PresentMode PresentMode() const noexcept;
    [[nodiscard]] float RefreshRate() const noexcept;
    [[nodiscard]] std::chrono::duration<double, std::milli> TargetFrameInterval() const noexcept;
    [[nodiscard]] void* ImageHandle(uint32_t index) const noexcept;
    [[nodiscard]] void* ImageViewHandle(uint32_t index) const noexcept;
    /** @brief True when the images have transfer source usage, so FrameCapture can copy them out */
    [[nodiscard]] bool SupportsTransferSource() const noexcept;

    /** @brief Sets the static HDR metadata of the current swapchain. Does nothing without VK_EXT_hdr_metadata. Call it
     * again after a recreation. */
    void SetHdrMetadata(const SwapchainHdrMetadata& metadata);

private:
    std::unique_ptr<SwapchainImpl> impl;
    class PlatformWindowSystem* platformSystem{ nullptr };
};

#endif // !DIAMOND_DOGS_DISPLAY_SYSTEM_SWAPCHAIN_HPP
