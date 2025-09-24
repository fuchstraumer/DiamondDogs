#pragma once
#ifndef DIAMOND_DOGS_PLATFORM_SYSTEM_HDR_SUPPORT_HPP
#define DIAMOND_DOGS_PLATFORM_SYSTEM_HDR_SUPPORT_HPP
#include "PlatformTypes.hpp"

struct GLFWwindow;
struct GLFWmonitor;

/**
 * @file PlatformDisplayInfo.hpp
 * @brief Platform-specific display information, currently focusing on HDR support and precise refresh rate detection.
 */

/** @brief Queries platform API, discovering if HDR is both supported and currently enabled. */
bool IsHDRSupportedAndEnabled();

/** @brief Retrieves information about the primary display by querying platform API. Intended to be called before init of platform system, to configure that. */
DisplayInfo RetrievePlatformPrimaryDisplayInfo() noexcept;


#endif // !DIAMOND_DOGS_PLATFORM_SYSTEM_HDR_SUPPORT_HPP
