#pragma once
#ifndef DIAMOND_DOGS_PLATFORM_SYSTEM_TYPES_HPP
#define DIAMOND_DOGS_PLATFORM_SYSTEM_TYPES_HPP
#include <cstdint>

namespace Platform
{
    enum class WindowMode
    {
        /** @brief Conventional bordered window with set initial dimensions */
        Windowed,
        /** @brief Exclusive fullscreen mode using a specific monitor */
        Fullscreen,
        /** @brief Borderless windowed mode that covers the entire screen */
        FullScreenWindowed,
        /** @brief Maximized windowed mode that fills the screen (excepting taskbar or other such OS primitives) but retains window borders */
        MaximizedWindowed
    };

    enum class ColorSpace : uint16_t
    {
        /** @brief Standard Red Green Blue color space */
        sRGB = 1 << 0,
        /** @brief Linear color space */
        Linear = 1 << 1,
        /** @brief High Dynamic Range sRGB with nonlinear encoding */
        HDR_sRGB_Nonlinear = 1 << 2,
        /** @brief High Dynamic Range Display P3 color space with nonlinear encoding */
        HDR_DisplayP3_Nonlinear = 1 << 3,
        /** @brief High Dynamic Range Extended sRGB color space with linear encoding */
        HDR_Extended_sRGB_Linear = 1 << 4,
        /** @brief High Dynamic Range HDR10 color space with ST2084 (PQ) transfer function */
        HDR_HDR10_ST2084 = 1 << 5,
        /** @brief High Dynamic Range Dolby Vision color space. @note Unsupported currently. */
        HDR_DolbyVision = 1 << 6,
        /** @brief High Dynamic Range HDR10 with Hybrid Log-Gamma (HLG) transfer function */
        HDR_HDR10_HLG = 1 << 7
    };

    /** @brief Returned from PlatformWindowSystem::GetHDRCapabilities() or PlatformWindowSystem::GetWindowCapabilities() to describe the HDR support of the current display or all available
     *  displays on a system. This can be done before the creation of any windows or surfaces, to allow for initial configuration and preparation of RHI systems.
     */
    struct HDRCapabilities
    {
        bool Supported{ false };
        ColorSpace ColorSpace{ ColorSpace::sRGB };
        float MaxLuminance{ 0.0f };
        float MinLuminance{ 0.0f };
        float MaxContentLightLevel{ 0.0f };
        float MaxFrameAverageLightLevel{ 0.0f };
        uint32_t SupportedColorSpacesBitmask{ 0u }; // bitmask of ColorSpace values that are supported
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
        HDRCapabilities HDRCapabilities;
    };


    struct PlatformWindowCreateInfo
    {
        const char* WindowName;
        DisplayInfo* DisplayToUse; // if null, use primary display
        ColorSpace DesiredColorSpace{ ColorSpace::sRGB };
        WindowMode DesiredWindowMode{ WindowMode::Windowed };
        uint32_t InitialWidth{ 800 };
        uint32_t InitialHeight{ 600 };
    };

}

#endif //!DIAMOND_DOGS_PLATFORM_SYSTEM_TYPES_HPP