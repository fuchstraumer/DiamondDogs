#include "PlatformDisplayInfo.hpp"
#include <vector>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wingdi.h>
#include <physicalmonitorenumerationapi.h>
#include <cmath>

bool IsHDRSupportedAndEnabled()
{
    UINT32 pathCount, modeCount;

    LONG result = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount);
    if (result != ERROR_SUCCESS)
    {
        return false;
    }

    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);

    result = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr);
    if (result != ERROR_SUCCESS)
    {
        return false;
    }

    for (UINT32 i = 0; i < pathCount; ++i)
    {
        const auto& path = paths[i];
        // TODO: What is the meaning of the _2 structs? Are there further caps and changes to how HDR works we need to account for?
        DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO colorInfo{};
        colorInfo.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
        colorInfo.header.size = sizeof(DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO);
        colorInfo.header.adapterId = path.targetInfo.adapterId;
        colorInfo.header.id = path.targetInfo.id;

        result = DisplayConfigGetDeviceInfo(&colorInfo.header);
        if (result == ERROR_SUCCESS)
        {
            if (colorInfo.advancedColorSupported && colorInfo.advancedColorEnabled)
            {
                return true;
            }
        }
    }

    return false;
}

bool SetHDREnabled(bool enableHDR)
{
    UINT32 pathCount, modeCount;

    LONG result = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount);
    if (result != ERROR_SUCCESS)
    {
        return false;
    }

    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);

    result = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr);
    if (result != ERROR_SUCCESS)
    {
        return false;
    }

    // Find the primary display or first HDR-capable display
    for (UINT32 i = 0; i < pathCount; ++i)
    {
        const auto& path = paths[i];
        
        // First check if HDR is supported on this display
        DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO getColorInfo{};
        getColorInfo.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
        getColorInfo.header.size = sizeof(DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO);
        getColorInfo.header.adapterId = path.targetInfo.adapterId;
        getColorInfo.header.id = path.targetInfo.id;

        result = DisplayConfigGetDeviceInfo(&getColorInfo.header);
        if (result == ERROR_SUCCESS && getColorInfo.advancedColorSupported)
        {
            // Attempt to set HDR state
            DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE setColorState{};
            setColorState.header.type = DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE;
            setColorState.header.size = sizeof(DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE);
            setColorState.header.adapterId = path.targetInfo.adapterId;
            setColorState.header.id = path.targetInfo.id;
            setColorState.enableAdvancedColor = enableHDR ? 1 : 0;

            result = DisplayConfigSetDeviceInfo(&setColorState.header);
            if (result == ERROR_SUCCESS)
            {
                return true;
            }
            // Note: Some systems may require elevated privileges or may not allow programmatic HDR toggling
            // This is especially true for systems with Windows HDR auto-switching enabled
        }
    }

    return false;
}

bool SetAdvancedColorEnabled(bool enableAdvancedColor)
{
    // This is essentially the same as SetHDREnabled, but more accurately named
    // since Windows manages HDR and WCG together through the "advanced color" setting
    return SetHDREnabled(enableAdvancedColor);
}

DisplayColorCapabilities GetDisplayColorCapabilities()
{
    DisplayColorCapabilities caps{};
    UINT32 pathCount, modeCount;

    LONG result = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount);
    if (result != ERROR_SUCCESS)
    {
        return caps;
    }

    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);

    result = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr);
    if (result != ERROR_SUCCESS)
    {
        return caps;
    }

    for (UINT32 i = 0; i < pathCount; ++i)
    {
        const auto& path = paths[i];
        
        DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO colorInfo{};
        colorInfo.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
        colorInfo.header.size = sizeof(DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO);
        colorInfo.header.adapterId = path.targetInfo.adapterId;
        colorInfo.header.id = path.targetInfo.id;

        result = DisplayConfigGetDeviceInfo(&colorInfo.header);
        if (result == ERROR_SUCCESS)
        {
            // Advanced Color is Microsoft's umbrella term for HDR + WCG
            caps.advancedColorSupported = colorInfo.advancedColorSupported;
            caps.advancedColorEnabled = colorInfo.advancedColorEnabled;
            
            // Extract individual capabilities from the Windows API
            // Note: Windows API doesn't always clearly separate HDR vs WCG,
            // but we can infer based on color encoding and bit depth
            caps.bitsPerColorChannel = colorInfo.bitsPerColorChannel;
            caps.colorEncoding = static_cast<uint32_t>(colorInfo.colorEncoding);
            
            // Heuristics to determine HDR vs WCG support:
            // HDR typically requires 10+ bits and higher SDR white levels
            caps.hdrSupported = caps.advancedColorSupported && 
                              (caps.bitsPerColorChannel >= 10) && 
                              (caps.sdrWhiteLevel > 100.0f || caps.sdrWhiteLevel == 0.0f); // 0 means system default
            
            // WCG can work with 8-bit but is more common with 10-bit
            // WCG is indicated by non-sRGB color encoding
            caps.wcgSupported = caps.advancedColorSupported && 
                              (caps.colorEncoding != 0); // 0 typically means sRGB
            
            // Current state - if advanced color is enabled, assume both are enabled
            // (Windows doesn't provide granular HDR-only vs WCG-only control)
            caps.hdrEnabled = caps.advancedColorEnabled && caps.hdrSupported;
            caps.wcgEnabled = caps.advancedColorEnabled && caps.wcgSupported;
            
            // Test toggle capability
            if (caps.advancedColorSupported)
            {
                DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE testSetState{};
                testSetState.header.type = DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE;
                testSetState.header.size = sizeof(DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE);
                testSetState.header.adapterId = path.targetInfo.adapterId;
                testSetState.header.id = path.targetInfo.id;
                testSetState.enableAdvancedColor = colorInfo.advancedColorEnabled;

                LONG testResult = DisplayConfigSetDeviceInfo(&testSetState.header);
                bool canToggle = (testResult == ERROR_SUCCESS);
                
                // Windows typically toggles HDR and WCG together
                caps.hdrCanBeToggled = canToggle && caps.hdrSupported;
                caps.wcgCanBeToggled = canToggle && caps.wcgSupported;
            }
        }
        
        DISPLAYCONFIG_SDR_WHITE_LEVEL whiteLevelInfo{};
        whiteLevelInfo.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
        whiteLevelInfo.header.size = sizeof(DISPLAYCONFIG_SDR_WHITE_LEVEL);
        whiteLevelInfo.header.adapterId = path.targetInfo.adapterId;
        whiteLevelInfo.header.id = path.targetInfo.id;
        result = DisplayConfigGetDeviceInfo(&whiteLevelInfo.header);
        if (result == ERROR_SUCCESS)
        {
            caps.sdrWhiteLevel = static_cast<float>(whiteLevelInfo.SDRWhiteLevel);
        }
        else
        {
            caps.sdrWhiteLevel = 80.0f; // Default fallback
        }
        
        return caps; // Return capabilities for the first advanced color display found
    }

    return caps;
}

DisplayRefreshRateCapabilities GetDisplayRefreshRateCapabilities(float desiredRefreshRate)
{
    constexpr float k_DefaultDesiredRefreshRate = 60.0f;

    if (desiredRefreshRate <= 0.0f)
    {
        desiredRefreshRate = k_DefaultDesiredRefreshRate;
    }
    UINT32 pathCount, modeCount;

    LONG result = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount);
    if (result != ERROR_SUCCESS)
    {
        return DisplayRefreshRateCapabilities{ 0.0f, false, 0.0f};
    }

    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    float closestRefreshRate = -1.0f;
    float smallestDiff = 1e8f;

    result = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr);
    if (result != ERROR_SUCCESS)
    {
        return DisplayRefreshRateCapabilities{ 0.0f, false, 0.0f};
    }

    for (UINT32 i = 0; i < pathCount; ++i)
    {
        const DISPLAYCONFIG_PATH_INFO& path = paths[i];
        UINT32 targetModeIdx = path.targetInfo.modeInfoIdx;
        if (targetModeIdx < modeCount && targetModeIdx != DISPLAYCONFIG_PATH_MODE_IDX_INVALID)
        {
            const DISPLAYCONFIG_MODE_INFO& mode = modes[targetModeIdx];
            if (mode.infoType == DISPLAYCONFIG_MODE_INFO_TYPE_TARGET)
            {
                const DISPLAYCONFIG_VIDEO_SIGNAL_INFO& signalInfo = mode.targetMode.targetVideoSignalInfo;

                float refreshRate = static_cast<float>(signalInfo.vSyncFreq.Numerator) / static_cast<float>(signalInfo.vSyncFreq.Denominator);

                float difference = std::abs(refreshRate - desiredRefreshRate);
                if (difference < smallestDiff)
                {
                    smallestDiff = difference;
                    closestRefreshRate = refreshRate;
                }

            }
        }
    }

    DisplayRefreshRateCapabilities refreshCaps{};
    refreshCaps.RefreshRate = (closestRefreshRate > 0.0f) ? closestRefreshRate : k_DefaultDesiredRefreshRate;
    refreshCaps.IsInteger = std::abs(refreshCaps.RefreshRate - std::round(refreshCaps.RefreshRate)) < 0.01f;
    refreshCaps.RoundedRefreshRate = std::round(refreshCaps.RefreshRate);
    return refreshCaps;
}
