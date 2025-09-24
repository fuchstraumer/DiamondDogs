#include "PlatformDisplayInfo.hpp"
#ifdef __linux__
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>

namespace
{
    // Linux HDR detection is very limited and heuristic-based
    // This attempts to detect some common scenarios
    
    bool DetectWaylandHDR()
    {
        // Check if we're running under Wayland
        const char* waylandDisplay = std::getenv("WAYLAND_DISPLAY");
        if (!waylandDisplay) {
            return false;
        }
        
        // Very basic heuristic: check for HDR-related environment variables
        // that some compositors might set
        const char* hdrHint = std::getenv("WAYLAND_HDR_ENABLED");
        return (hdrHint && strcmp(hdrHint, "1") == 0);
    }
    
    bool DetectX11HDR()
    {
        // X11 HDR detection is extremely limited
        // Most X11 setups don't support HDR properly
        
        #ifdef __linux__
        Display* display = XOpenDisplay(nullptr);
        if (!display) {
            return false;
        }
        
        // Very basic detection: check for high color depth modes
        Screen* screen = DefaultScreenOfDisplay(display);
        int depth = DefaultDepthOfScreen(screen);
        
        XCloseDisplay(display);
        
        // Heuristic: if we have >8 bits per channel, *maybe* HDR is possible
        // This is very unreliable but it's the best we can do with X11
        return depth >= 30; // 10 bits per channel = 30 bit total
        #else
        return false;
        #endif
    }
}

bool IsHDRSupportedAndEnabled()
{
    // Linux HDR detection strategy:
    // 1. Check if running under Wayland with HDR hints
    // 2. Fall back to X11 depth detection (unreliable)
    // 3. Default to false for safety
    
    if (DetectWaylandHDR())
    {
        return true;
    }
    
    if (DetectX11HDR())
    {
        // Even if X11 suggests HDR capability, we can't be sure it's enabled
        // Return false for safety unless explicitly hinted
        return false;
    }
    
    return false;
}

bool SetHDREnabled(bool enableHDR)
{
    // Linux HDR toggling is extremely limited
    // Most desktop environments don't provide programmatic HDR control
    
    // Future implementations could try:
    // - Wayland color management protocols (when standardized)
    // - KMS/DRM direct mode setting (requires root/special permissions)
    // - Desktop environment-specific APIs (GNOME/KDE)
    
    // For now, always return false to indicate unsupported
    return false;
}

bool SetAdvancedColorEnabled(bool enableAdvancedColor)
{
    // Same limitations as SetHDREnabled
    return SetHDREnabled(enableAdvancedColor);
}

float GetSDRWhiteLevel()
{
    // Linux doesn't provide a standard API for SDR white level
    // Return a reasonable default
    return 80.0f; // Standard SDR white level in nits
}

DisplayColorCapabilities GetDisplayColorCapabilities()
{
    DisplayColorCapabilities caps{};
    
    // Linux HDR capability detection is extremely limited
    // We can make some educated guesses but can't be definitive
    
    const char* waylandDisplay = std::getenv("WAYLAND_DISPLAY");
    const char* x11Display = std::getenv("DISPLAY");
    
    if (waylandDisplay)
    {
        // Wayland: slightly better HDR prospects
        // Some compositors support color management
        
        // Very conservative detection
        caps.wcgSupported = true;  // Most modern Wayland compositors can handle wider gamuts
        caps.hdrSupported = false; // Very few actually support HDR as of 2025
        
        // Check for explicit HDR hints
        const char* hdrHint = std::getenv("WAYLAND_HDR_ENABLED");
        if (hdrHint && strcmp(hdrHint, "1") == 0) {
            caps.hdrSupported = true;
            caps.hdrEnabled = true;
        }
        
    } 
    else if (x11Display)
    {
        // X11: very limited HDR support
        #ifdef __linux__
        Display* display = XOpenDisplay(nullptr);
        if (display)
        {
            Screen* screen = DefaultScreenOfDisplay(display);
            int depth = DefaultDepthOfScreen(screen);
            
            // Heuristic based on color depth
            if (depth >= 30)
            {
                caps.wcgSupported = true;
                caps.bitsPerColorChannel = 10;
            } 
            else if (depth >= 24)
            {
                caps.bitsPerColorChannel = 8;
            }
            
            XCloseDisplay(display);
        }
        #endif
    }
    
    // Advanced color is combination of HDR + WCG
    caps.advancedColorSupported = caps.hdrSupported && caps.wcgSupported;
    caps.advancedColorEnabled = caps.hdrEnabled && caps.wcgEnabled;
    
    // Linux generally doesn't allow programmatic toggling
    caps.hdrCanBeToggled = false;
    caps.wcgCanBeToggled = false;
    
    // Set reasonable defaults
    caps.sdrWhiteLevel = 80.0f;
    if (caps.bitsPerColorChannel == 0)
    {
        caps.bitsPerColorChannel = 8; // Safe default
    }
    
    return caps;
}

HDRCapabilityInfo GetHDRCapabilities()
{
    // Legacy wrapper that converts new structure to old format
    auto caps = GetDisplayColorCapabilities();
    
    HDRCapabilityInfo info{};
    info.isSupported = caps.hdrSupported;
    info.isEnabled = caps.hdrEnabled;
    info.canBeToggled = caps.hdrCanBeToggled;
    info.sdrWhiteLevel = caps.sdrWhiteLevel;
    info.bitsPerColorChannel = caps.bitsPerColorChannel;
    
    return info;
}

#endif // __linux__
