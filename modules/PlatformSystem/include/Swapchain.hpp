#pragma once
#ifndef DIAMOND_DOGS_PLATFORM_SYSTEM_SWAPCHAIN_HPP
#define DIAMOND_DOGS_PLATFORM_SYSTEM_SWAPCHAIN_HPP
#include "PlatformTypes.hpp"
#include "ImageDataFormats.hpp"
#include "Math.hpp"
#include <memory>

struct SwapchainImpl;

class Swapchain
{
    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;
public:
    Swapchain(const SwapchainCreateInfo& createInfo);
    ~Swapchain();

    Swapchain(Swapchain&& other) noexcept;
    Swapchain& operator=(Swapchain&& other) noexcept;

    /** @brief Takes swapchain create info as we may have adjusted parameters that caused the recreation to occur. */
    void Recreate(const SwapchainCreateInfo& createInfo);
    /** @brief Required as part of the swapchain lifecycle, for handling recreation events */
    void Destroy();

    /** @brief Get the native swapchain handle. @return `VkSwapchainKHR` as `const void*` */
    const void* GetNativeHandle() const noexcept;

    math::Float2 GetExtent() const noexcept;
    uint32_t ImageCount() const noexcept;
    ImageFormat GetImageFormat() const noexcept;
    ColorSpace ColorSpace() const noexcept;
    const void* GetImageHandle(uint32_t index) const noexcept;
    const void* GetImageViewHandle(uint32_t index) const noexcept;

private:
    std::unique_ptr<SwapchainImpl> impl;
    class PlatformWindowSystem* PlatformSystem{ nullptr };
};

#endif // !DIAMOND_DOGS_DISPLAY_SYSTEM_SWAPCHAIN_HPP
