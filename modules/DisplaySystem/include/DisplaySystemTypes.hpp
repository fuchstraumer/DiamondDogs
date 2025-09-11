#pragma once
#ifndef DIAMOND_DOGS_DISPLAY_SYSTEM_TYPES_HPP
#define DIAMOND_DOGS_DISPLAY_SYSTEM_TYPES_HPP
#include <cstdint>
#include "ImageDataFormats.hpp"

/** @brief Defines color space for a surface - note that this is not the same as a format.
 *  One format can support multiple colorspaces, as this is a tonemapping transform used for final rendering.
*/
enum class ColorSpace : uint16_t
{
    /** @brief Standard sRGB nonlinear encoded color space */
    sRGB_Nonlinear = 1 << 0,
    /** @brief High Dynamic Range Display P3 color space with nonlinear encoding */
    Display_P3_Nonlinear = 1 << 1,
    /** @brief Extended sRGB linear encoded color space */
    Extended_sRGB_Linear = 1 << 2,
    /** @brief HDR Display P3 color space with linear encoding */
    Display_P3_Linear = 1 << 3,
    DCI_P3_Nonlinear = 1 << 4,
    BT709_Linear = 1 << 5,
    BT709_Nonlinear = 1 << 6,
    BT2020_Linear = 1 << 7,
    /** @brief High Dynamic Range HDR10 color space with ST2084 (PQ) transfer function */
    HDR10_ST2084 = 1 << 8,
    /** @brief High Dynamic Range HDR10 with Hybrid Log-Gamma (HLG) transfer function */
    HDR10_HLG = 1 << 9,
    Extended_sRGB_Nonlinear = 1 << 10,
    PassThrough = 1 << 11,
    DisplayNativeAMD = 1 << 12
};

enum class PresentMode : uint8_t
{
    /** @brief Aliases to immediate present mode. No buffering, high incidences of tearing.*/
    None = 0,
    /** @brief Aliases to simple FIFO mode - vertical sync and double-buffering, effectively.*/
    VerticalSync = 1,
    /** @brief Aliases to relaxed FIFO mode - if a frame is missed, tearing is allowed. Efficient and most effective on mobile platforms.*/
    VerticalSyncRelaxed = 2,
    /** @brief Aliases to Vulkan's mailbox mode, which effectively becomes triple-buffering.*/
    VerticalSyncMailbox = 3,
    /** @brief Aliases to demand-refresh shared mode. Application may refresh as it wishes, but will also guarantee that it refreshes on a call to present.*/
    SharedDemandRefresh = 4,
    /** @brief Alias to the continued-refresh mode. Swapchain will continously refresh the contents of the screen as it sees fit, and makes no guarantee of a refresh upon a call to present.*/
    SharedContinuousRefresh = 5,
    /** @brief Alias to FIFO latest ready mode. */
    VerticalSyncLatestReady = 6
};

/** @brief Describe the HDR support of the current display */
struct HDRCapabilities
{
    bool Supported{ false };
    float MaxLuminance{ 0.0f };
    float MinLuminance{ 0.0f };
    float MaxContentLightLevel{ 0.0f };
    float MaxFrameAverageLightLevel{ 0.0f };
    /** @brief Swapchain image format */
    ImageFormat SwapchainFormat{ ImageComponentFormats::Invalid, ImageDataType::Default };
    /** @brief Color space that was chosen during swapchain creation */
    ColorSpace ActiveColorSpace{ ColorSpace::sRGB_Nonlinear };
};

struct SwapchainCreateInfo
{
    uint64_t DeviceHandle{ 0u }; // VkDevice
    uint64_t PhysicalDeviceHandle{ 0u }; // VkPhysicalDevice
    void* PlatformWindowHandle{ nullptr }; // GLFWwindow*
    uint64_t SurfaceHandle{ 0u }; // VkSurfaceKHR
    /** @brief Min image count for swapchain */
    uint32_t MinImageCount{ 2 };
    /** @brief Optional image format to use for swapchain. If left to invalid default values, will auto-choose format: does not enable HDR */
    ImageFormat SwapchainFormat{ ImageComponentFormats::Invalid, ImageDataType::Default };
    /** @brief Preferred color space for SDR surfaces and content */
    ColorSpace SdrColorSpace{ ColorSpace::sRGB_Nonlinear };
    /** @brief Preferred color space if HDR is supported. Default value is DCI-P3, as this is the most highly supported.
     *  @note Enabling an HDR colorspace and using an HDR framebuffer is not enough, application shaders must perform final tonemapping and transforms.
     */
    ColorSpace HdrColorSpace{ ColorSpace::DCI_P3_Nonlinear };
    PresentMode SwapchainPresentMode{ PresentMode::None };
    /** @brief If set to true, Swapchain will attempt to use HdrColorSpace and best available colorbuffer format */
    bool TryEnableHDR{ false };
    /** @brief Pointer to the platform system this display swapchain will be a child of. */
    const void* PlatformSystemPtr{ nullptr };
    /** @brief Index of the monitor/display used with this window. Will be used to query the platform system for dimensional info. Default value of is UINT32_MAX, which means just use the "primary" display if not changed. */
    uint32_t DisplayIndex{ std::numeric_limits<uint32_t>::max() };
};

// for now, we only really need info about the swapchain. In the future, this system will likely grow in complexity as we add support for more display features
struct DisplaySystemCreateInfo
{
    SwapchainCreateInfo SwapchainInfo;
};

#endif // !DIAMOND_DOGS_DISPLAY_SYSTEM_TYPES_HPP
