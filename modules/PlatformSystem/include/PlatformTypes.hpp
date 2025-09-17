#pragma once
#ifndef DIAMOND_DOGS_PLATFORM_SYSTEM_TYPES_HPP
#define DIAMOND_DOGS_PLATFORM_SYSTEM_TYPES_HPP
#include "ImageDataFormats.hpp"
#include <cstdint>
#include <limits>

enum class PlatformWindowMode
{
    Invalid = 0,
    /** @brief Conventional bordered window with set initial dimensions */
    Windowed,
    /** @brief Exclusive fullscreen mode using a specific monitor */
    Fullscreen,
    /** @brief Borderless windowed mode that covers the entire screen */
    FullScreenWindowed,
    /** @brief Maximized windowed mode that fills the screen (excepting taskbar or other such OS primitives) but retains window borders */
    MaximizedWindowed
};

struct DisplayInfo
{
    uint32_t Width{ 0 };
    uint32_t Height{ 0 };
    /** Bit depth of the red color channel */
    uint8_t BitDepthRed{ 0 };
    /** Bit depth of the green color channel */
    uint8_t BitDepthGreen{ 0 };
    /** Bit depth of the blue color channel */
    uint8_t BitDepthBlue{ 0 };
    /** OS-specific horizontal display scale factor, for High DPI displays */
    float DisplayScaleX{ 1.0f };
    /** OS-specific vertical display scale factor, for High DPI displays */
    float DisplayScaleY{ 1.0f };
    float RefreshRate{ 0.0f };
    // index to monitor we chose when querying GLFW monitors: using index as pointer would not guarantee lifetime
    int MonitorIdx{ -1 };
};

/** @brief Struct that allows setting and toggling GLFW window hints, currently. */
struct PlatformWindowBehaviorFlags
{
    bool Resizable = true;
    bool Moveable = true;
    bool Decorated = true;
    bool FocusOnShow = false;
    bool CenterMouse = false;
};

struct PlatformWindowCreateInfo
{
    const char* WindowName;
    DisplayInfo* DisplayToUse; // if null, use primary display
    PlatformWindowMode DesiredWindowMode{ PlatformWindowMode::Windowed };
    uint32_t InitialWidth{ 800 };
    uint32_t InitialHeight{ 600 };
    uint32_t InitialPosX{ 0 };
    uint32_t InitialPosY{ 0 };
    PlatformWindowBehaviorFlags BehaviorFlags;
};

/** @brief Defines color space for a surface - note that this is not the same as a format.
 *  One format can support multiple colorspaces, as this is a tonemapping transform used for final rendering.
*/
enum class ColorSpace : uint16_t
{
    Invalid = 0,
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
    Invalid = 0,
    /** @brief Immediate present mode. No buffering, high incidences of tearing.*/
    Immediate = 1,
    /** @brief Aliases to simple FIFO mode - vertical sync and double-buffering, effectively.*/
    VerticalSync = 2,
    /** @brief Aliases to relaxed FIFO mode - if a frame is missed, tearing is allowed. Efficient and most effective on mobile platforms.*/
    VerticalSyncRelaxed = 3,
    /** @brief Aliases to Vulkan's mailbox mode, which effectively becomes triple-buffering.*/
    VerticalSyncMailbox = 4,
    /** @brief Aliases to demand-refresh shared mode. Application may refresh as it wishes, but will also guarantee that it refreshes on a call to present.*/
    SharedDemandRefresh = 5,
    /** @brief Alias to the continued-refresh mode. Swapchain will continously refresh the contents of the screen as it sees fit, and makes no guarantee of a refresh upon a call to present.*/
    SharedContinuousRefresh = 6,
    /** @brief Alias to FIFO latest ready mode. */
    VerticalSyncLatestReady = 7
};

/** @brief HDR/WCG information as queried from appropriate platform APIs, before system and swapchain initialization. Informs further configuration and usage. */
struct DisplayColorCapabilities
{
    // HDR (High Dynamic Range) support
    bool hdrSupported{ false };
    bool hdrEnabled{ false };
    bool hdrCanBeToggled{ false };  // Some systems may not allow programmatic HDR toggling
    
    // WCG (Wide Color Gamut) support - can be independent of HDR
    bool wcgSupported{ false };
    bool wcgEnabled{ false };
    bool wcgCanBeToggled{ false };
    
    // Advanced color generally refers to HDR + WCG combined
    bool advancedColorSupported{ false };
    bool advancedColorEnabled{ false };
    
    // Additional metadata
    float sdrWhiteLevel{ 80.0f }; // Default SDR white level in nits
    uint32_t bitsPerColorChannel{ 8 };
    uint32_t colorEncoding{ 0 }; // DISPLAYCONFIG_COLOR_ENCODING value
    
    // Helper methods
    bool IsFullAdvancedColorSupported() const { return hdrSupported && wcgSupported; }
    bool IsFullAdvancedColorEnabled() const { return hdrEnabled && wcgEnabled; }
};

/** @brief Describe the HDR support of the application as it is currently configured, after platform initialization.
 *  This is informed by 'DisplayColorCapabilities', which is queried before creating a window and swapchain.
 */
struct HDRCapabilities
{
    bool Supported{ false };
    bool Enabled{ false };
    float sdrWhiteLevel{ 80.0f }; // in nits
    uint32_t BitsPerColorChannel{ 8 };
    /** @brief Swapchain image format */
    ImageFormat SwapchainFormat{ ImageComponentFormats::Invalid, ImageDataType::Default };
    /** @brief Color space that was chosen during swapchain creation */
    ColorSpace ActiveColorSpace{ ColorSpace::sRGB_Nonlinear };
};

struct SwapchainCreateInfo
{
    uint64_t VkDeviceHandle{ 0u };
    uint64_t VkPhysicalDeviceHandle{ 0u };
    void* PlatformWindowHandle{ nullptr }; // GLFWwindow*
    uint64_t VkSurfaceHandle{ 0u };
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
    void* PlatformSystemPtr{ nullptr };
    /** @brief Index of the monitor/display used with this window. Will be used to query the platform system for dimensional info. Default value of is UINT32_MAX, which means just use the "primary" display if not changed. */
    uint32_t DisplayIndex{ std::numeric_limits<uint32_t>::max() };
};


#endif //!DIAMOND_DOGS_PLATFORM_SYSTEM_TYPES_HPP