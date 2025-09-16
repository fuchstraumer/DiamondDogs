#include "HDRSupport.hpp"
#include <vector>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wingdi.h>
#include <physicalmonitorenumerationapi.h>

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
