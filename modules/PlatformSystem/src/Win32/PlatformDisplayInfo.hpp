#pragma once
#ifndef DIAMOND_DOGS_PLATFORM_SYSTEM_HDR_SUPPORT_HPP
#define DIAMOND_DOGS_PLATFORM_SYSTEM_HDR_SUPPORT_HPP
#include "PlatformTypes.hpp"
#include <cstdint>

/**
 * @file PlatformDisplayInfo.hpp
 * @brief Platform-specific display information, currently focusing on HDR support and precise refresh rate detection.
 */

/** @brief Queries platform API, discovering if HDR is both supported and currently enabled. */
bool IsHDRSupportedAndEnabled();

/** @brief Attempts to toggle HDR support on/off for the primary display
 *  @return Returns true if the operation succeeded, false otherwise */
bool SetHDREnabled(bool enableHDR);

/** @brief Attempts to toggle advanced color (HDR + WCG) on/off for the primary display
 *  @note Windows typically manages HDR and WCG together through "advanced color"
 */
bool SetAdvancedColorEnabled(bool enableAdvancedColor);

DisplayColorCapabilities GetDisplayColorCapabilities();

#endif // !DIAMOND_DOGS_PLATFORM_SYSTEM_HDR_SUPPORT_HPP
