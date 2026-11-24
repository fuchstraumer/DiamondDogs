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
// The device feature that VK_PRESENT_MODE_FIFO_LATEST_READY needs (EXT or KHR); the window stage asks for it as Wanted.
// Without it, the swapchain does not offer that mode, even where the surface does.
inline constexpr std::string_view k_FifoLatestReadyFeature{ "VkPhysicalDevicePresentModeFifoLatestReadyFeaturesKHR.presentModeFifoLatestReady" };

// Present ids and present waits (presentation plan P3). The window stage asks for all four as Wanted. The 2 versions
// also need the surface to support them; the first versions need only the device.
inline constexpr std::string_view k_PresentId2Feature{ "VkPhysicalDevicePresentId2FeaturesKHR.presentId2" };
inline constexpr std::string_view k_PresentWait2Feature{ "VkPhysicalDevicePresentWait2FeaturesKHR.presentWait2" };
inline constexpr std::string_view k_PresentIdFeature{ "VkPhysicalDevicePresentIdFeaturesKHR.presentId" };
inline constexpr std::string_view k_PresentWaitFeature{ "VkPhysicalDevicePresentWaitFeaturesKHR.presentWait" };

// Which present wait the current swapchain uses. Chosen at each create, best first.
enum class PresentWaitSupport : uint8_t
{
    None = 0,           // no present ids: a caller paces on CPU time only
    PresentWait = 1,    // VK_KHR_present_id and VK_KHR_present_wait
    PresentWait2 = 2    // VK_KHR_present_id2 and VK_KHR_present_wait2, supported by this surface
};

enum class PresentWaitStatus : uint8_t
{
    Invalid = 0,
    Presented = 1,      // the present with this id, or a later one, reached the display
    Timeout = 2,
    OutOfDate = 3,      // the swapchain went out of date; recreate it
    Retired = 4,        // the id went to a swapchain that a Recreate replaced: there is nothing left to wait for
    NotPresented = 5,   // no present has had this id yet (0, or above LastPresentId); waiting would only time out
    Unsupported = 6,    // PresentWaitSupport::None
    Failed = 7
};

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

enum class PresentModeChange : uint8_t
{
    Invalid = 0,
    Unchanged = 1,      // the swapchain already uses this mode
    NextPresent = 2,    // compatible with the current mode: the next present uses it, with no recreate
    NeedsRecreate = 3,  // the surface offers it, but a new swapchain is needed: the next Recreate uses it
    Unsupported = 4     // the surface does not offer it
};

// Counts for tests and measurements, since the swapchain was made
struct SwapchainStatistics
{
    uint32_t Recreates{ 0u };
    uint64_t RecreateNanoseconds{ 0u };         // the sum over every Recreate
    uint64_t MaxRecreateNanoseconds{ 0u };
    uint32_t PresentModeSwitches{ 0u };         // switches that needed no recreate
    uint32_t RetiredWithPendingPresents{ 0u };  // old swapchains kept until their presents finished
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
     * fallback on a device that has it. Call it before the first acquire; the next Recreate also drops the
     * maintenance1 structures from the swapchain's create info. */
    void LimitToCorePresentation() noexcept;

    // Swapchain maintenance1 extras (presentation plan P2). Each needs UsesMaintenance1.

    /** @brief True while the window has a framebuffer with area. A minimized window has none; with present scaling, the
     * swapchain does not report that as out of date, so a caller checks it before it acquires. */
    [[nodiscard]] bool WindowHasArea() const noexcept;
    /** @brief The present modes the surface offers and the device can use */
    [[nodiscard]] std::vector<::PresentMode> SupportedPresentModes() const;
    /** @brief The present modes the current swapchain can switch to with no recreate (the current one included) */
    [[nodiscard]] std::vector<::PresentMode> SwitchablePresentModes() const;
    /** @brief Changes the present mode: at the next present when it is switchable, else at the next Recreate. */
    [[nodiscard]] PresentModeChange SetPresentMode(::PresentMode mode);
    /** @brief When on (the default), and the surface supports it, a swapchain whose window changed size is scaled by the
     * presentation engine (aspect-ratio stretch where offered, centered) until it is recreated, instead of reporting
     * out of date at once. Takes effect at the next Recreate. */
    void SetPresentScaling(bool enabled) noexcept;
    /** @brief When on, the images get their memory at their first acquire, so a Recreate returns sooner; image views
     * are then made at the first acquire too, and ImageViewHandle is null before it. Off by default, because a caller
     * may read every view before it acquires. Takes effect at the next Recreate. Known problem (2026-11-23): on NVIDIA
     * driver 32.0.16.1714, the second Recreate of a deferred swapchain crashes inside the driver; the PlatformSystem
     * README has the details. */
    void SetDeferredImageMemory(bool enabled) noexcept;
    [[nodiscard]] bool UsesPresentScaling() const noexcept;
    [[nodiscard]] bool UsesDeferredImageMemory() const noexcept;
    [[nodiscard]] SwapchainStatistics Statistics() const noexcept;

    // Present ids and present waits (presentation plan P3). On the thread that presents, like the rest.
    /** @brief The present wait the current swapchain uses. It can change at a Recreate. */
    [[nodiscard]] PresentWaitSupport PresentWaitTier() const noexcept;
    /** @brief The id of the last present that the swapchain accepted; 0 before the first one, and always 0 with
     * PresentWaitSupport::None. Ids increase by one per present, across recreates. */
    [[nodiscard]] uint64_t LastPresentId() const noexcept;
    /** @brief Blocks until the present with `present_id` (or a later one) reached the display, or until timeout_ns
     * passes. Returns at once for an id this swapchain cannot wait on (NotPresented, Retired, Unsupported). */
    [[nodiscard]] PresentWaitStatus WaitForPresent(uint64_t present_id, uint64_t timeout_ns);

private:
    std::unique_ptr<SwapchainImpl> impl;
    class PlatformWindowSystem* platformSystem{ nullptr };
};

#endif // !DIAMOND_DOGS_DISPLAY_SYSTEM_SWAPCHAIN_HPP
