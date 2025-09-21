#pragma once
#ifndef DIAMOND_DOGS_PLATFORM_SYSTEM_HDR_SUPPORT_HPP
#define DIAMOND_DOGS_PLATFORM_SYSTEM_HDR_SUPPORT_HPP

#include <cstdint>

// Cross-platform HDR support detection and control
// Note: Linux implementation is limited due to lack of unified HDR APIs

// Queries system API to determine if HDR is enabled on this system
bool IsHDRSupportedAndEnabled();

// Attempts to toggle HDR support on/off for the primary display
// Returns true if the operation succeeded, false otherwise
// Note: May not be supported on all Linux configurations
bool SetHDREnabled(bool enableHDR);

// Attempts to toggle advanced color (HDR + WCG) on/off for the primary display
// Note: Linux may not distinguish between HDR and WCG at the API level
bool SetAdvancedColorEnabled(bool enableAdvancedColor);

// Gets the SDR white level in nits for the primary display (if HDR is supported)
// Returns 0.0f if HDR is not supported or the value cannot be retrieved
float GetSDRWhiteLevel();

// Legacy HDR-only capability info (for backward compatibility)
struct HDRCapabilityInfo
{
    bool isSupported{ false };
    bool isEnabled{ false };
    bool canBeToggled{ false };
    float sdrWhiteLevel{ 80.0f };
    uint32_t bitsPerColorChannel{ 8 };
};

// Gets detailed HDR and WCG capabilities for the primary display
struct DisplayColorCapabilities
{
    // HDR (High Dynamic Range) support
    bool hdrSupported{ false };
    bool hdrEnabled{ false };
    bool hdrCanBeToggled{ false };  // Very limited on Linux
    
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
    uint32_t colorEncoding{ 0 }; // Platform-specific encoding value
    
    // Helper methods
    bool IsFullAdvancedColorSupported() const { return hdrSupported && wcgSupported; }
    bool IsFullAdvancedColorEnabled() const { return hdrEnabled && wcgEnabled; }
};

DisplayColorCapabilities GetDisplayColorCapabilities();

// Legacy method for backward compatibility
HDRCapabilityInfo GetHDRCapabilities();

#endif // !DIAMOND_DOGS_PLATFORM_SYSTEM_HDR_SUPPORT_HPP