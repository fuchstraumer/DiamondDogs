#pragma once
#ifndef DIAMOND_DOGS_PLATFORM_SYSTEM_TYPES_HPP
#define DIAMOND_DOGS_PLATFORM_SYSTEM_TYPES_HPP
#include "ImageDataFormats.hpp"
#include <cstdint>

enum class PlatformWindowMode
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

#endif //!DIAMOND_DOGS_PLATFORM_SYSTEM_TYPES_HPP