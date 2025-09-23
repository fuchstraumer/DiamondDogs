#include "PlatformDisplayInfo.hpp"
#include <vector>
#include <cmath>
#include <unordered_map>
#include <string>
#include <cassert>
#include <stdexcept>
#include <algorithm>

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wingdi.h>
#include <physicalmonitorenumerationapi.h>

struct EDIDChromaticityCoordinates
{
    // only true if EDID data was available and successfully parsed
    bool HasValidData{ false };
    float RedX{ 0.0f };
    float RedY{ 0.0f };
    float GreenX{ 0.0f };
    float GreenY{ 0.0f };
    float BlueX{ 0.0f };
    float BlueY{ 0.0f };
    float WhitePointX{ 0.0f };
    float WhitePointY{ 0.0f };
};

struct EDIDLuminanceData
{
    bool HasValidData{ false };
    float Max{ 0.0f }; // in nits
    float Min{ 0.0f }; // in nits
    float MaxAverage{ 0.0f }; // in nits
};

struct EDIDHdrData
{
    EDIDLuminanceData Luminance;
    EDIDChromaticityCoordinates Chromaticity;
};

// stub here because I have to include more windows headers to get this :(
std::vector<uint8_t> GetEDIDFromRegistry(const std::wstring& monitorFriendlyName);
EDIDHdrData GetEDIDHdrData(const std::vector<uint8_t>& edidData);


/** @brief Information about a display mode for a single monitor: analogous to GLFW monitor modes */
struct DisplayMode
{
    uint32_t Width;
    uint32_t Height;
    float RefreshRate;

    constexpr bool operator==(const DisplayMode& other) const noexcept
    {
        return Width == other.Width && Height == other.Height && std::fabs(RefreshRate - other.RefreshRate) < 0.1f;
    }

    constexpr bool operator!=(const DisplayMode& other) const noexcept
    {
        return !(*this == other);
    }

    constexpr bool operator<(const DisplayMode& other) const noexcept
    {
        if (Width != other.Width)
        {
            return Width < other.Width;
        }
        else if (Height != other.Height)
        {
            return Height < other.Height;
        }
        else
        {
            return RefreshRate < other.RefreshRate;
        }
    }
};

// used to help us map GLFWmonitors to Windows monitors
struct DisplayConfigMonitorInfo
{
    // Adapter is like "parent" device
    std::string FriendlyName;
    LUID TargetAdapterId;
    UINT32 TargetId;
    DISPLAYCONFIG_PATH_INFO DisplayPathInfo{};
    float ActiveRefreshRate{ 0.0f };
    uint32_t ActiveWidth{ 0 };
    uint32_t ActiveHeight{ 0 };
    uint32_t BitDepth{ 0 };
    bool IsPrimary{ false };
    DisplayColorCapabilities ColorCapabilities{};
};

std::string ConvertFromWideString(const std::wstring& wideStr)
{
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wideStr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string str(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wideStr.c_str(), -1, &str[0], size_needed, nullptr, nullptr);
    return str;
}

std::unordered_map<std::string, DisplayConfigMonitorInfo> g_WindowsMonitorInfoMap;

void SetWindowsMonitorMappings()
{
    g_WindowsMonitorInfoMap.clear();

    // Now try to associate DisplayConfig information
    UINT32 pathCount = 0;
    UINT32 modeCount = 0;
    LONG result = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount);
    if (result != ERROR_SUCCESS)
    {
        throw std::runtime_error("Failed to get display config buffer sizes.");
    }

    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);

    result = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr);
    if (result != ERROR_SUCCESS)
    {
        return;
    }

    for (UINT32 i = 0; i < pathCount; ++i)
    {
        const DISPLAYCONFIG_PATH_INFO& path = paths[i];
        DISPLAYCONFIG_TARGET_DEVICE_NAME targetName{};
        targetName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        targetName.header.size = sizeof(DISPLAYCONFIG_TARGET_DEVICE_NAME);
        targetName.header.adapterId = path.targetInfo.adapterId;
        targetName.header.id = path.targetInfo.id;
        result = DisplayConfigGetDeviceInfo(&targetName.header);
        if (result != ERROR_SUCCESS)
        {
            continue;
        }

        const std::wstring wFriendlyName = std::wstring(targetName.monitorFriendlyDeviceName);
        const std::string friendlyName = ConvertFromWideString(wFriendlyName);
        DisplayConfigMonitorInfo monitorInfo;
        monitorInfo.FriendlyName = friendlyName;
        monitorInfo.TargetAdapterId = path.targetInfo.adapterId;
        monitorInfo.TargetId = path.targetInfo.id;
        monitorInfo.DisplayPathInfo = path;

        // Get the mode info, and then check to see if it's a target mode. If it's not, we'll continue on: we only want to store info about target modes (i.e. monitors)
        UINT32 targetModeIdx = path.targetInfo.modeInfoIdx;
        if (targetModeIdx < modeCount)
        {
            assert(modes[targetModeIdx].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_TARGET);
            const DISPLAYCONFIG_MODE_INFO& modeInfo = modes[targetModeIdx];
            const DISPLAYCONFIG_VIDEO_SIGNAL_INFO& signalInfo = modeInfo.targetMode.targetVideoSignalInfo;
            monitorInfo.ActiveWidth = signalInfo.activeSize.cx;
            monitorInfo.ActiveHeight = signalInfo.activeSize.cy;
            monitorInfo.ActiveRefreshRate = static_cast<float>(signalInfo.vSyncFreq.Numerator) / static_cast<float>(signalInfo.vSyncFreq.Denominator);
            // the totalSize parameter seems to include extra pixels that are not part of the actual display area, so we won't use it
            //monitorInfo.MaxWidth = signalInfo.totalSize.cx;
            //monitorInfo.MaxHeight = signalInfo.totalSize.cy;
            // hardcoding this since the surface format is stored in the source info: it seems that is set by the GPU hardware/signal out, not by the surface
            // it would be interesting to see if we can eventually get this to display the special HDR format (like R11G11B10A2) flag value though
            monitorInfo.BitDepth = 32;
        }

        // now query color capabilities
        DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2 colorInfo{};
        colorInfo.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO_2;
        colorInfo.header.size = sizeof(DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2);
        colorInfo.header.adapterId = path.targetInfo.adapterId;
        colorInfo.header.id = path.targetInfo.id;
        result = DisplayConfigGetDeviceInfo(&colorInfo.header);
        if (result == ERROR_SUCCESS)
        {
            DisplayColorCapabilities caps{};

            caps.bitsPerColorChannel = colorInfo.bitsPerColorChannel;
            // our color encoding enum is offset by 1, as it contains an "invalid" sentinel value at 0
            caps.ColorMode = static_cast<DisplayColorCapabilities::ColorModes>(colorInfo.activeColorMode + 1);

            // Heuristics to determine HDR vs WCG support:
            // HDR typically requires 10+ bits and higher SDR white levels
            caps.hdrSupported = colorInfo.highDynamicRangeSupported;
            caps.hdrEnabled = colorInfo.highDynamicRangeUserEnabled;
            caps.wcgSupported = colorInfo.wideColorSupported;
            caps.wcgEnabled = colorInfo.wideColorUserEnabled;

            // Test toggle capability
            if (colorInfo.advancedColorSupported || colorInfo.highDynamicRangeSupported || colorInfo.wideColorSupported)
            {
                DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE testSetState{};
                testSetState.header.type = DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE;
                testSetState.header.size = sizeof(DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE);
                testSetState.header.adapterId = path.targetInfo.adapterId;
                testSetState.header.id = path.targetInfo.id;
                testSetState.enableAdvancedColor = colorInfo.advancedColorActive;

                LONG testResult = DisplayConfigSetDeviceInfo(&testSetState.header);
                bool canToggle = (testResult == ERROR_SUCCESS);

                // Windows typically toggles HDR and WCG together
                caps.hdrCanBeToggled = canToggle && caps.hdrSupported;
                caps.wcgCanBeToggled = canToggle && caps.wcgSupported;
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

            monitorInfo.ColorCapabilities = caps;
        }

        // Now move on to try and extract EDID luminance data
        std::vector<uint8_t> edidData = GetEDIDFromRegistry(wFriendlyName);
        EDIDHdrData hdrData = GetEDIDHdrData(edidData);
        if (hdrData.Luminance.HasValidData)
        {
            monitorInfo.ColorCapabilities.MaxLuminance = hdrData.Luminance.Max;
            monitorInfo.ColorCapabilities.MinLuminance = hdrData.Luminance.Min;
            monitorInfo.ColorCapabilities.MaxAverageLuminance = hdrData.Luminance.MaxAverage;
        }
        else
        {
            monitorInfo.ColorCapabilities.MaxLuminance = 0.0f;
            monitorInfo.ColorCapabilities.MinLuminance = 0.0f;
            monitorInfo.ColorCapabilities.MaxAverageLuminance = 0.0f;
        }

        if (hdrData.Chromaticity.HasValidData)
        {
            monitorInfo.ColorCapabilities.RedPrimaryX = hdrData.Chromaticity.RedX;
            monitorInfo.ColorCapabilities.RedPrimaryY = hdrData.Chromaticity.RedY;
            monitorInfo.ColorCapabilities.GreenPrimaryX = hdrData.Chromaticity.GreenX;
            monitorInfo.ColorCapabilities.GreenPrimaryY = hdrData.Chromaticity.GreenY;
            monitorInfo.ColorCapabilities.BluePrimaryX = hdrData.Chromaticity.BlueX;
            monitorInfo.ColorCapabilities.BluePrimaryY = hdrData.Chromaticity.BlueY;
            monitorInfo.ColorCapabilities.WhitePointX = hdrData.Chromaticity.WhitePointX;
            monitorInfo.ColorCapabilities.WhitePointY = hdrData.Chromaticity.WhitePointY;
        }
        else
        {
            monitorInfo.ColorCapabilities.RedPrimaryX = 0.0f;
            monitorInfo.ColorCapabilities.RedPrimaryY = 0.0f;
            monitorInfo.ColorCapabilities.GreenPrimaryX = 0.0f;
            monitorInfo.ColorCapabilities.GreenPrimaryY = 0.0f;
            monitorInfo.ColorCapabilities.BluePrimaryX = 0.0f;
            monitorInfo.ColorCapabilities.BluePrimaryY = 0.0f;
            monitorInfo.ColorCapabilities.WhitePointX = 0.0f;
            monitorInfo.ColorCapabilities.WhitePointY = 0.0f;
        }
        
        g_WindowsMonitorInfoMap.emplace(friendlyName, monitorInfo);
    }
}

bool IsHDRSupportedAndEnabled()
{
    SetWindowsMonitorMappings();
    // if any monitor does not support or have HDR enabled, return false
    return std::all_of(g_WindowsMonitorInfoMap.begin(), g_WindowsMonitorInfoMap.end(), [](const auto& pair)
    {
        return pair.second.ColorCapabilities.hdrSupported && pair.second.ColorCapabilities.hdrEnabled;
    });
}

// Monitor GUID
// {E6F07B5F-EE97-4A90-B076-33F57BF4EAA7}
#include <SetupApi.h>
#pragma comment(lib, "setupapi.lib")
const GUID GUID_DEVINTERFACE_MONITOR = { 0xE6F07B5F, 0xEE97, 0x4A90, { 0xB0, 0x76, 0x33, 0xF5, 0x7B, 0xF4, 0xEA, 0xA7 } };

std::vector<uint8_t> GetEDIDFromRegistry(const std::wstring& monitorFriendlyName)
{
    HKEY displaysKey;
    LONG result = RegOpenKeyExW(HKEY_LOCAL_MACHINE, 
        L"SYSTEM\\CurrentControlSet\\Enum\\DISPLAY", 0, KEY_READ, &displaysKey);
    
    if (result != ERROR_SUCCESS) return {};
    
    // Enumerate display devices
    DWORD subkeyIndex = 0;
    wchar_t subkeyName[256];
    DWORD subkeyNameSize = sizeof(subkeyName) / sizeof(wchar_t);
    
    while (RegEnumKeyExW(displaysKey, subkeyIndex++, subkeyName, &subkeyNameSize, 
                         nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS)
    {
        HKEY deviceKey;
        result = RegOpenKeyExW(displaysKey, subkeyName, 0, KEY_READ, &deviceKey);
        if (result != ERROR_SUCCESS)
        {
            subkeyNameSize = sizeof(subkeyName) / sizeof(wchar_t);
            continue;
        }
        
        // Enumerate device instances
        DWORD instanceIndex = 0;
        wchar_t instanceName[256];
        DWORD instanceNameSize = sizeof(instanceName) / sizeof(wchar_t);
        
        while (RegEnumKeyExW(deviceKey, instanceIndex++, instanceName, &instanceNameSize,
                             nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS)
        {
            HKEY instanceKey;
            std::wstring instancePath = std::wstring(subkeyName) + L"\\" + instanceName;
            result = RegOpenKeyExW(displaysKey, instancePath.c_str(), 0, KEY_READ, &instanceKey);
            
            if (result == ERROR_SUCCESS)
            {
                // Check device parameters for EDID
                HKEY paramsKey;
                result = RegOpenKeyExW(instanceKey, L"Device Parameters", 0, KEY_READ, &paramsKey);
                if (result == ERROR_SUCCESS)
                {
                    DWORD edidSize = 0;
                    result = RegQueryValueExW(paramsKey, L"EDID", nullptr, nullptr, nullptr, &edidSize);
                    if (result == ERROR_SUCCESS && edidSize > 0)
                    {
                        std::vector<uint8_t> edidData(edidSize);
                        result = RegQueryValueExW(paramsKey, L"EDID", nullptr, nullptr, 
                                                  edidData.data(), &edidSize);
                        if (result == ERROR_SUCCESS)
                        {
                            RegCloseKey(paramsKey);
                            RegCloseKey(instanceKey);
                            RegCloseKey(deviceKey);
                            RegCloseKey(displaysKey);
                            return edidData;
                        }
                    }
                    RegCloseKey(paramsKey);
                }
                RegCloseKey(instanceKey);
            }
            instanceNameSize = sizeof(instanceName) / sizeof(wchar_t);
        }
        
        RegCloseKey(deviceKey);
        subkeyNameSize = sizeof(subkeyName) / sizeof(wchar_t);
    }
    
    RegCloseKey(displaysKey);
    return {};
}

// most of this was found in other libraries, like libdisplay-info
EDIDHdrData GetEDIDHdrData(const std::vector<uint8_t>& edidData)
{
    EDIDHdrData hdrData;

    if (edidData.size() < 128)
    {
        return hdrData; // Invalid EDID data
    }
    

    // Parse chromaticity coordinates from base EDID block (bytes 25-34)
    // These are always present in EDID v1.3+
    if (edidData.size() >= 37)
    {
        EDIDChromaticityCoordinates chromaticity{};
        
        // EDID stores chromaticity in a packed format:
        // Bytes 25-26: Red and Green x coordinates (low 2 bits)
        // Bytes 27-28: Blue and White x coordinates (low 2 bits)  
        // Bytes 29-32: High 8 bits of Red, Green, Blue, White x coordinates
        // Bytes 33-36: High 8 bits of Red, Green, Blue, White y coordinates
        
        uint8_t rxry_low = edidData[25];
        uint8_t gxgy_low = edidData[26];
        uint8_t bxby_low = edidData[27];
        uint8_t wxwy_low = edidData[28];
        
        // Extract chromaticity coordinates (10-bit precision)
        auto extractCoordinate = [&](uint8_t high_byte, uint8_t low_bits) -> float
        {
            uint16_t value = (static_cast<uint16_t>(high_byte) << 2) | (low_bits & 0x03);
            return static_cast<float>(value) / 1024.0f;
        };
        
        chromaticity.RedX = extractCoordinate(edidData[29], (rxry_low >> 6) & 0x03);
        chromaticity.RedY = extractCoordinate(edidData[30], (rxry_low >> 4) & 0x03);
        chromaticity.GreenX = extractCoordinate(edidData[31], (gxgy_low >> 6) & 0x03);
        chromaticity.GreenY = extractCoordinate(edidData[32], (gxgy_low >> 4) & 0x03);
        chromaticity.BlueX = extractCoordinate(edidData[33], (bxby_low >> 6) & 0x03);
        chromaticity.BlueY = extractCoordinate(edidData[34], (bxby_low >> 4) & 0x03);
        chromaticity.WhitePointX = extractCoordinate(edidData[35], (wxwy_low >> 6) & 0x03);
        chromaticity.WhitePointY = extractCoordinate(edidData[36], (wxwy_low >> 4) & 0x03);

        // Validate coordinates are reasonable (CIE 1931 space)
        auto isValidCoordinate = [](float x, float y) -> bool
        {
            return x >= 0.0f && x <= 1.0f && y >= 0.0f && y <= 1.0f && (x + y) <= 1.0f;
        };
        
        chromaticity.HasValidData = 
            isValidCoordinate(chromaticity.RedX, chromaticity.RedY) &&
            isValidCoordinate(chromaticity.GreenX, chromaticity.GreenY) &&
            isValidCoordinate(chromaticity.BlueX, chromaticity.BlueY) &&
            isValidCoordinate(chromaticity.WhitePointX, chromaticity.WhitePointY);

        hdrData.Chromaticity = chromaticity;
    }

    for (size_t blockOffset = 128; blockOffset < edidData.size(); blockOffset += 128)
    {
        if (blockOffset + 128 > edidData.size())
        {
            break;
        }
        
        const uint8_t* block = &edidData[blockOffset];
        
        // Check if this is a CTA extension block
        if (block[0] != 0x02)
        {
            continue; // CTA tag
        }
        
        // Parse data blocks within CTA extension
        uint8_t dtdOffset = block[2];
        if (dtdOffset < 4)
        {
            continue;
        }
        
        for (size_t i = 4; i < dtdOffset; )
        {
            if (i >= dtdOffset)
            {
                break;
            }
            
            uint8_t tag = (block[i] >> 5) & 0x07;
            uint8_t length = block[i] & 0x1F;
            
            // Ensure we don't read beyond bounds
            if (i + length + 1 > dtdOffset)
            {
                break;
            }
            
            // HDR Static Metadata Data Block (tag = 7, extended tag = 0x06)
            if (tag == 7 && length >= 2 && i + 1 < dtdOffset && block[i + 1] == 0x06)
            {
                // HDR Static Metadata Data Block found
                // Data format: [tag+length][extended_tag][eotf][metadata_descriptor][luminance_data...]
                const uint8_t* data = &block[i + 2]; // Skip tag+length and extended tag
                uint8_t size = length - 1; // Subtract 1 for the extended tag byte
                
                // Check what luminance data is available based on size
                if (size > 2)
                {
                    // Max luminance is at data[2] (index 2 after eotf and metadata descriptor)
                    uint8_t maxLumCode = data[2];
                    if (maxLumCode > 0)
                    {
                        hdrData.Luminance.Max = 50.0f * std::pow(2.0f, maxLumCode / 32.0f);
                    }
                }
                
                if (size > 3)
                {
                    // Max average luminance is at data[3]
                    uint8_t maxAvgLumCode = data[3];
                    if (maxAvgLumCode > 0)
                    {
                        hdrData.Luminance.MaxAverage = 50.0f * std::pow(2.0f, maxAvgLumCode / 32.0f);
                    }
                }
                
                if (size > 4)
                {
                    // Min luminance is at data[4]
                    uint8_t minLumCode = data[4];
                    if (minLumCode > 0)
                    {
                        // Min luminance formula from CTA-861-G: MaxLum * (MinLumCode/255)^2 / 100
                        if (hdrData.Luminance.Max > 0.0f)
                        {
                            float minLumRatio = minLumCode / 255.0f;
                            hdrData.Luminance.Min = hdrData.Luminance.Max * (minLumRatio * minLumRatio) / 100.0f;
                        }
                    }
                }
                
                hdrData.Luminance.HasValidData = (hdrData.Luminance.Max > 0.0f);
                if (hdrData.Luminance.HasValidData)
                {
                    break;
                }
            }

            if (hdrData.Luminance.HasValidData)
            {
                break;
            }
            
            i += length + 1;
        }
    }

    return hdrData;

}
