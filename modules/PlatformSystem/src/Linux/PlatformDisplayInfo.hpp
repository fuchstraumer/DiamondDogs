#pragma once
#ifndef DIAMOND_DOGS_PLATFORM_SYSTEM_HDR_SUPPORT_HPP
#define DIAMOND_DOGS_PLATFORM_SYSTEM_HDR_SUPPORT_HPP
#include "PlatformTypes.hpp"

#ifdef __linux__

// Queries system API to determine if HDR is enabled on this system
bool IsHDRSupportedAndEnabled();

// Attempts to toggle HDR support on/off for the primary display
// Returns true if the operation succeeded, false otherwise
// Note: May not be supported on all Linux configurations
bool SetHDREnabled(bool enableHDR);

// Attempts to toggle advanced color (HDR + WCG) on/off for the primary display
// Note: Linux may not distinguish between HDR and WCG at the API level
bool SetAdvancedColorEnabled(bool enableAdvancedColor);

DisplayColorCapabilities GetDisplayColorCapabilities();

DisplayRefreshRateCapabilities GetDisplayRefreshRateCapabilities(float desiredRefreshRate = 0.0f);

#endif // __linux__

#endif // !DIAMOND_DOGS_PLATFORM_SYSTEM_HDR_SUPPORT_HPP