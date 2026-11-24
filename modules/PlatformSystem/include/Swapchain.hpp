#pragma once
#ifndef DIAMOND_DOGS_PLATFORM_SYSTEM_SWAPCHAIN_HPP
#define DIAMOND_DOGS_PLATFORM_SYSTEM_SWAPCHAIN_HPP
#include "PlatformTypes.hpp"
#include "ImageDataFormats.hpp"
#include "math/Math.hpp"
#include <cstdint>
#include <chrono>
#include <memory>
#include <string_view>
#include <vector>

struct SwapchainImpl;

// The device feature that gives present fences and vkReleaseSwapchainImagesKHR (the EXT or the KHR extension). The
// window stage asks for it as Wanted. Without it, a recreate and the destruction need an idle device.
inline constexpr std::string_view k_SwapchainMaintenance1Feature{ "VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR.swapchainMaintenance1" };

enum class SwapchainAcquireStatus : uint8_t
{
    Invalid = 0,
    Acquired = 1,
    Suboptimal = 2,     // acquired, and its semaphore signals; recreate after this frame
    OutOfDate = 3,      // nothing acquired; recreate first
    NotReady = 4,       // nothing acquired yet; try again later
    Failed = 5
};

// One acquired image. The caller's submit waits on WaitSemaphore before the first use of the image, and signals
// RenderDoneSemaphore; Present waits on it. Both are VkSemaphore handles, owned by the swapchain.
struct SwapchainAcquire
{
    SwapchainAcquireStatus Status{ SwapchainAcquireStatus::Invalid };
    uint32_t ImageIndex{ 0u };
    void* Image{ nullptr };
    void* View{ nullptr };
    uint64_t WaitSemaphore{ 0u };
    uint64_t RenderDoneSemaphore{ 0u };
};

enum class SwapchainPresentStatus : uint8_t
{
    Invalid = 0,
    Presented = 1,
    Suboptimal = 2,     // presented; recreate before the next acquire
    OutOfDate = 3,      // not shown; recreate before the next acquire
    Failed = 4
};

enum class SwapchainReleaseStatus : uint8_t
{
    Invalid = 0,
    Released = 1,
    Unsupported = 2,    // no swapchain maintenance1: only a present gives an image back
    Failed = 3
};

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
     * The handles that this object returns change, but the object itself stays valid. No image may be acquired and
     * not yet presented or released. Every submit that waited on an acquire semaphore must have finished. With
     * maintenance1 (UsesMaintenance1), that is all: the old swapchain is destroyed later, once its presents finished.
     * Without it, the device must be idle (`vkDeviceWaitIdle` is enough).
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

    // Presentation. The swapchain owns the acquire and render-done semaphores and the present fences; the README of
    // PlatformSystem explains the reuse rules. All of these run on the thread that submits to the general queue.

    /** @brief Acquires the next image. Never waits longer than timeout_ns (0: never waits). */
    [[nodiscard]] SwapchainAcquire TryAcquire(uint64_t timeout_ns);
    /** @brief Presents an acquired image on the device's general queue, after its render-done semaphore signals. */
    [[nodiscard]] SwapchainPresentStatus Present(uint32_t image_index);
    /** @brief Gives back an acquired image that will not be presented (swapchain maintenance1). */
    [[nodiscard]] SwapchainReleaseStatus Release(uint32_t image_index);
    /** @brief Blocks until every present made through Present has finished, or until timeout_ns passes. False when it
     * timed out, or when the swapchain has no present fences: the caller must then wait for the device instead. */
    [[nodiscard]] bool WaitForPresents(uint64_t timeout_ns);
    /** @brief True when present fences and release are in use: the device has swapchain maintenance1, and
     * LimitToCorePresentation was not called. Then Recreate needs no idle device. */
    [[nodiscard]] bool UsesMaintenance1() const noexcept;
    /** @brief Uses core presentation only, as if the device had no swapchain maintenance1. For tests that prove the
     * fallback on a device that has it. Call it before the first acquire. */
    void LimitToCorePresentation() noexcept;

private:
    std::unique_ptr<SwapchainImpl> impl;
    class PlatformWindowSystem* platformSystem{ nullptr };
};

#endif // !DIAMOND_DOGS_DISPLAY_SYSTEM_SWAPCHAIN_HPP
