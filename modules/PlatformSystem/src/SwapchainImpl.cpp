#include "SwapchainImpl.hpp"
#include "ImageDataFormats.hpp"
#include "ImageDataFormatsVulkan.hpp"
#include "PlatformTypes.hpp"
#include "Swapchain.hpp"
#include "PlatformSystem.hpp"
#include "Device.hpp"
#include "vulkan/vulkan_core.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <map>
#include <vector>

// Allows us to rank surface formats based on how "good" we believe they are
// We place higher scores on formats we believe best for HDR, including suitable HDR color spaces
// and 10-bit formats. Lower scores are placed on 8-bit formats and SDR color

namespace std
{
    template<>
    struct hash<VkSurfaceFormatKHR>
    {
        size_t operator()(const VkSurfaceFormatKHR& format) const noexcept
        {
            // Combine hashes of format and colorSpace using a simple but effective method
            const size_t formatHash = std::hash<uint32_t>{}(static_cast<uint32_t>(format.format));
            const size_t colorSpaceHash = std::hash<uint32_t>{}(static_cast<uint32_t>(format.colorSpace));
            // Use bit shifting and XOR to combine the hashes
            return formatHash ^ (colorSpaceHash << 1);
        }
    };
}

// needed as otherwise sType and pNext would be uninitialized and cause validation errors when we query for capabilities with extended structures
constexpr static VkSurfaceCapabilities2KHR k_DefaultSurfaceCapabilities2Khr = { VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR, nullptr };

static const std::unordered_map<VkSurfaceFormatKHR, size_t> k_SVkSurfaceFormatScores
{
    // 16-bit floating point formats - highest priority (1000+ points)
    { { VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_HDR10_ST2084_EXT }, 1300 },
    { { VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_HDR10_HLG_EXT }, 1290 },
    { { VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_BT2020_LINEAR_EXT }, 1280 },
    { { VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_DCI_P3_NONLINEAR_EXT }, 1200 },
    { { VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT }, 1190 },
    { { VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT }, 1100 },
    { { VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR }, 1050 },

    // 10-bit formats - high priority for HDR (800-900 points)
    { { VK_FORMAT_A2R10G10B10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT }, 950 },
    { { VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT }, 940 },
    { { VK_FORMAT_A2R10G10B10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_HLG_EXT }, 930 },
    { { VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_HLG_EXT }, 920 },
    { { VK_FORMAT_A2R10G10B10_UNORM_PACK32, VK_COLOR_SPACE_BT2020_LINEAR_EXT }, 900 },
    { { VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_BT2020_LINEAR_EXT }, 890 },
    { { VK_FORMAT_A2R10G10B10_UNORM_PACK32, VK_COLOR_SPACE_DCI_P3_NONLINEAR_EXT }, 850 },
    { { VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_DCI_P3_NONLINEAR_EXT }, 840 },
    { { VK_FORMAT_A2R10G10B10_UNORM_PACK32, VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT }, 830 },
    { { VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT }, 820 },
    { { VK_FORMAT_A2R10G10B10_UNORM_PACK32, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT }, 810 },
    { { VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT }, 800 },

    // 16-bit integer formats - medium-high priority (600-700 points)
    { { VK_FORMAT_R16G16B16A16_UNORM, VK_COLOR_SPACE_HDR10_ST2084_EXT }, 750 },
    { { VK_FORMAT_R16G16B16A16_UNORM, VK_COLOR_SPACE_HDR10_HLG_EXT }, 740 },
    { { VK_FORMAT_R16G16B16A16_UNORM, VK_COLOR_SPACE_BT2020_LINEAR_EXT }, 720 },
    { { VK_FORMAT_R16G16B16A16_UNORM, VK_COLOR_SPACE_DCI_P3_NONLINEAR_EXT }, 680 },
    { { VK_FORMAT_R16G16B16A16_UNORM, VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT }, 670 },
    { { VK_FORMAT_R16G16B16A16_UNORM, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT }, 650 },
    { { VK_FORMAT_R16G16B16A16_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR }, 600 },

    // 8-bit formats with HDR color spaces - medium priority (400-500 points)
    { { VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_HDR10_ST2084_EXT }, 500 },
    { { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_HDR10_ST2084_EXT }, 490 },
    { { VK_FORMAT_R8G8B8A8_SRGB, VK_COLOR_SPACE_HDR10_ST2084_EXT }, 480 },
    { { VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_HDR10_ST2084_EXT }, 470 },
    { { VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_HDR10_HLG_EXT }, 460 },
    { { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_HDR10_HLG_EXT }, 450 },
    { { VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_BT2020_LINEAR_EXT }, 440 },
    { { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_BT2020_LINEAR_EXT }, 430 },
    { { VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_DCI_P3_NONLINEAR_EXT }, 420 },
    { { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_DCI_P3_NONLINEAR_EXT }, 410 },
    { { VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT }, 400 },
    { { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT }, 390 },

    // 8-bit formats with extended sRGB - low-medium priority (200-300 points)
    { { VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT }, 300 },
    { { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT }, 290 },
    { { VK_FORMAT_R8G8B8A8_SRGB, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT }, 280 },
    { { VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT }, 270 },
    { { VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT }, 260 },
    { { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT }, 250 },
    { { VK_FORMAT_R8G8B8A8_SRGB, VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT }, 240 },
    { { VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT }, 230 },

    // Standard sRGB 8-bit formats - lowest priority (50-150 points)
    { { VK_FORMAT_R8G8B8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR }, 150 },
    { { VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR }, 140 },
    { { VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR }, 120 },
    { { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR }, 110 },
    
    // RGB8 formats (no alpha) - very low priority
    { { VK_FORMAT_R8G8B8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR }, 80 },
    { { VK_FORMAT_B8G8R8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR }, 70 },
    { { VK_FORMAT_R8G8B8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR }, 60 },
    { { VK_FORMAT_B8G8R8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR }, 50 },

    // Fallback/invalid entries
    { VkSurfaceFormatKHR{ VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_MAX_ENUM_KHR }, 0 }
};

VkColorSpaceKHR ToVkColorSpace(const ColorSpace space) noexcept
{
    switch (space)
    {
        case ColorSpace::sRGB_Nonlinear:
            return VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        case ColorSpace::Display_P3_Nonlinear:
            return VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT;
        case ColorSpace::Extended_sRGB_Linear:
            return VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT;
        case ColorSpace::Display_P3_Linear:
            return VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT;
        case ColorSpace::DCI_P3_Nonlinear:
            return VK_COLOR_SPACE_DCI_P3_NONLINEAR_EXT;
        case ColorSpace::BT709_Linear:
            return VK_COLOR_SPACE_BT709_LINEAR_EXT;
        case ColorSpace::BT709_Nonlinear:
            return VK_COLOR_SPACE_BT709_NONLINEAR_EXT;
        case ColorSpace::BT2020_Linear:
            return VK_COLOR_SPACE_BT2020_LINEAR_EXT;
        case ColorSpace::HDR10_ST2084:
            return VK_COLOR_SPACE_HDR10_ST2084_EXT;
        case ColorSpace::HDR10_HLG:
            return VK_COLOR_SPACE_HDR10_HLG_EXT;
        case ColorSpace::Extended_sRGB_Nonlinear:
            return VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT;
        case ColorSpace::PassThrough:
            return VK_COLOR_SPACE_PASS_THROUGH_EXT;
        case ColorSpace::DisplayNativeAMD:
            return VK_COLOR_SPACE_DISPLAY_NATIVE_AMD;
        default:
            return VK_COLOR_SPACE_MAX_ENUM_KHR;
    }
}

ColorSpace FromVkColorSpace(const VkColorSpaceKHR space) noexcept
{
    switch (space)
    {
        case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR:
            return ColorSpace::sRGB_Nonlinear;
        case VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT:
            return ColorSpace::Display_P3_Nonlinear;
        case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT:
            return ColorSpace::Extended_sRGB_Linear;
        case VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT:
            return ColorSpace::Display_P3_Linear;
        case VK_COLOR_SPACE_DCI_P3_NONLINEAR_EXT:
            return ColorSpace::DCI_P3_Nonlinear;
        case VK_COLOR_SPACE_BT709_LINEAR_EXT:
            return ColorSpace::BT709_Linear;
        case VK_COLOR_SPACE_BT709_NONLINEAR_EXT:
            return ColorSpace::BT709_Nonlinear;
        case VK_COLOR_SPACE_BT2020_LINEAR_EXT:
            return ColorSpace::BT2020_Linear;
        case VK_COLOR_SPACE_HDR10_ST2084_EXT:
            return ColorSpace::HDR10_ST2084;
        case VK_COLOR_SPACE_HDR10_HLG_EXT:
            return ColorSpace::HDR10_HLG;
        case VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT:
            return ColorSpace::Extended_sRGB_Nonlinear;
        case VK_COLOR_SPACE_PASS_THROUGH_EXT:
            return ColorSpace::PassThrough;
        case VK_COLOR_SPACE_DISPLAY_NATIVE_AMD:
            return ColorSpace::DisplayNativeAMD;
        default:
            return ColorSpace::Invalid;
    }
}

VkPresentModeKHR ToVkPresentMode(const PresentMode mode) noexcept
{
    switch (mode)
    {
        case PresentMode::Invalid:
            return VK_PRESENT_MODE_MAX_ENUM_KHR;
        case PresentMode::Immediate:
            return VK_PRESENT_MODE_IMMEDIATE_KHR;
        case PresentMode::VerticalSync:
            return VK_PRESENT_MODE_FIFO_KHR;
        case PresentMode::VerticalSyncRelaxed:
            return VK_PRESENT_MODE_FIFO_RELAXED_KHR;
        case PresentMode::VerticalSyncMailbox:
            return VK_PRESENT_MODE_MAILBOX_KHR;
        case PresentMode::SharedDemandRefresh:
            return VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR;
        case PresentMode::SharedContinuousRefresh:
            return VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR;
        case PresentMode::VerticalSyncLatestReady:
            return VK_PRESENT_MODE_FIFO_LATEST_READY_EXT;
        default:
            return VK_PRESENT_MODE_MAX_ENUM_KHR;
    }
}

// i have no idea how clang decides (incorrectly) that these are unused, but it sure does!
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif

static bool operator==(const VkSurfaceFormatKHR& lhs, const VkSurfaceFormatKHR& rhs) noexcept
{
    return (lhs.format == rhs.format) && (lhs.colorSpace == rhs.colorSpace);
}

static bool operator==(const VkSurfaceFormat2KHR& vk_surface_format, const AppSurfaceFormat& app_surface_format) noexcept
{
    const VkFormat appVkFormat = ToVkFormat(app_surface_format.Format);
    const VkColorSpaceKHR appColorSpace = ToVkColorSpace(app_surface_format.Space);
    return (vk_surface_format.surfaceFormat.format == appVkFormat) && (vk_surface_format.surfaceFormat.colorSpace == appColorSpace);
}

#ifdef __clang__
#pragma clang diagnostic pop
#endif

SwapchainInfo::SwapchainInfo(const VkPhysicalDevice& dvc, const VkSurfaceKHR& sfc) : capabilities(k_DefaultSurfaceCapabilities2Khr)
{
    const VkPhysicalDeviceSurfaceInfo2KHR surfaceInfo = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR, nullptr, sfc };
    // Query surface capabilities
    vkGetPhysicalDeviceSurfaceCapabilities2KHR(dvc, &surfaceInfo, &capabilities);

    // Query surface formats
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(dvc, sfc, &formatCount, nullptr);
    formats.resize(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(dvc, sfc, &formatCount, formats.data());

    // Query present modes
    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(dvc, sfc, &presentModeCount, nullptr);
    presentModes.resize(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(dvc, sfc, &presentModeCount, presentModes.data());

}

std::vector<SurfaceFormatPair> QuerySurfaceFormatPairs(const SwapchainCreateInfo& create_info)
{
    const rhi::Device* device = reinterpret_cast<const rhi::Device*>(create_info.RhiDevice);
    const VkPhysicalDevice physicalDevice = device->GetPhysicalDevice().As<VkPhysicalDevice>();
    const VkSurfaceKHR surface = reinterpret_cast<VkSurfaceKHR>(create_info.VkSurfaceHandle);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, surfaceFormats.data());

    std::vector<SurfaceFormatPair> pairs;
    pairs.reserve(surfaceFormats.size());

    for (const VkSurfaceFormatKHR& surfaceFormat : surfaceFormats)
    {
        const ImageFormat format = FromVkFormat(surfaceFormat.format);
        const ColorSpace space = FromVkColorSpace(surfaceFormat.colorSpace);

        if (format.IsValid() && space != ColorSpace::Invalid)
        {
            pairs.push_back(SurfaceFormatPair{ format, space });
        }
    }

    return pairs;
}

VkSurfaceFormatKHR SwapchainInfo::FindBestFormat() const noexcept
{
    // Create new unordered_map to hold scores for available formats, but use an ordered map and swap key-values to sort by score
    std::map<size_t, VkSurfaceFormatKHR> availableFormatScores;
    for (const auto& format : formats)
    {
        auto it = k_SVkSurfaceFormatScores.find(format);
        if (it != k_SVkSurfaceFormatScores.end())
        {
            availableFormatScores[it->second] = format;
        }
        else
        {
            availableFormatScores[0] = format; // Unknown formats get a score of 0
        }
    }

    VkSurfaceFormatKHR bestFormat{ VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_MAX_ENUM_KHR };
    // Now find the format with the highest score in availableFormatScores
    if (!availableFormatScores.empty())
    {
        bestFormat = availableFormatScores.rbegin()->second;
    }

    return bestFormat;
}

VkSurfaceFormatKHR SwapchainInfo::FindClosestFormat(const VkSurfaceFormatKHR& requested_format) const noexcept
{
    if (formats.empty())
    {
        return { VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_MAX_ENUM_KHR };
    }

    // 1. First priority: Exact match
    for (const auto& format : formats)
    {
        if (format.format == requested_format.format && format.colorSpace == requested_format.colorSpace)
        {
            return format;
        }
    }

    // 2. Second priority: Same format, different color space
    // Use scoring system to find the best color space match
    VkSurfaceFormatKHR bestSameFormat{ VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_MAX_ENUM_KHR };
    size_t bestSameFormatScore = 0;
    
    for (const auto& format : formats)
    {
        if (format.format == requested_format.format)
        {
            auto it = k_SVkSurfaceFormatScores.find(format);
            size_t score = (it != k_SVkSurfaceFormatScores.end()) ? it->second : 0;
            if (score > bestSameFormatScore)
            {
                bestSameFormatScore = score;
                bestSameFormat = format;
            }
        }
    }
    
    if (bestSameFormat.format != VK_FORMAT_UNDEFINED)
    {
        return bestSameFormat;
    }

    // 3. Third priority: Same color space, different format
    // Use scoring system to find the best format match for the requested color space
    VkSurfaceFormatKHR bestSameColorSpace{ VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_MAX_ENUM_KHR };
    size_t bestSameColorSpaceScore = 0;
    
    for (const auto& format : formats)
    {
        if (format.colorSpace == requested_format.colorSpace)
        {
            auto it = k_SVkSurfaceFormatScores.find(format);
            size_t score = (it != k_SVkSurfaceFormatScores.end()) ? it->second : 0;
            if (score > bestSameColorSpaceScore)
            {
                bestSameColorSpaceScore = score;
                bestSameColorSpace = format;
            }
        }
    }
    
    if (bestSameColorSpace.format != VK_FORMAT_UNDEFINED)
    {
        return bestSameColorSpace;
    }

    // 4. Fallback: Use the best available format according to our scoring system
    return FindBestFormat();
}

SwapchainImpl::SwapchainImpl(const SwapchainCreateInfo& create_info) :
    CreateInfo{ create_info },
    ParentDevice{ VK_NULL_HANDLE },
    AppFormat{},
    VulkanFormat{ .format = VK_FORMAT_UNDEFINED, .colorSpace = VK_COLOR_SPACE_MAX_ENUM_KHR },
    Extent{ .width=0, .height=0 },
    MinImageCount{ create_info.MinImageCount },
    ImageCount{ 0 },
    PresentMode{ VK_PRESENT_MODE_FIFO_KHR }
{
    rhi::Device* device = reinterpret_cast<rhi::Device*>(create_info.RhiDevice);
    ParentDevice = device->Handle().As<VkDevice>();
    Create(create_info);
}

SwapchainImpl::~SwapchainImpl()
{
    Destroy();
}


VkSwapchainCreateInfoKHR SwapchainImpl::getCreateInfo(const SwapchainCreateInfo& create_info,
                                                      VkSwapchainKHR old_swapchain) const noexcept
{
    VkSwapchainCreateInfoKHR swapchainCreateInfo;
    swapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainCreateInfo.pNext = nullptr;
    swapchainCreateInfo.flags = 0;

    swapchainCreateInfo.surface = reinterpret_cast<VkSurfaceKHR>(create_info.VkSurfaceHandle);
    swapchainCreateInfo.minImageCount = MinImageCount;
    swapchainCreateInfo.imageFormat = VulkanFormat.format;
    swapchainCreateInfo.imageColorSpace = VulkanFormat.colorSpace;
    swapchainCreateInfo.imageExtent = Extent;
    swapchainCreateInfo.imageArrayLayers = 1; // always 1 unless doing stereoscopic 3D
    // will need color attachment bits because we also write to the swapchain images, not just present them
    swapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    // transfer source lets FrameCapture copy the presented image out, when the surface allows it
    if ((Info->capabilities.surfaceCapabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0u)
    {
        swapchainCreateInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; // assume only one queue family for now
    swapchainCreateInfo.queueFamilyIndexCount = 0;
    swapchainCreateInfo.pQueueFamilyIndices = nullptr;
    swapchainCreateInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR; // no pre-transform
    swapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; // ignore alpha channel, for now - no blending with other windows or layers
    swapchainCreateInfo.presentMode = ToVkPresentMode(create_info.SwapchainPresentMode);
    swapchainCreateInfo.clipped = VK_TRUE; // expected default, we should hopefully never change this
    swapchainCreateInfo.oldSwapchain = old_swapchain;

    // this will need to be adjusted if we ever start setting pointer parameters....
    assert(swapchainCreateInfo.pNext == nullptr && (swapchainCreateInfo.pQueueFamilyIndices == nullptr));
    return swapchainCreateInfo;
}

void SwapchainImpl::Create(const SwapchainCreateInfo& create_info)
{
    if (create_info.RefreshRate > 1.0f)
    {
        RefreshRate = create_info.RefreshRate;
    }
    else if (create_info.PlatformSystemPtr != nullptr)
    {
        const auto* platformSystem = reinterpret_cast<const PlatformWindowSystem*>(create_info.PlatformSystemPtr);
        const auto& displayInfo = platformSystem->GetActiveDisplayInfo();
        const float queriedRate = displayInfo.RefreshRateCapabilities.RefreshRate;
        RefreshRate = (queriedRate > 1.0f) ? queriedRate : 60.0f;
    }
    else
    {
        RefreshRate = 60.0f;
    }

    querySurface(create_info);
    chooseSurfaceFormat(create_info);
    Extent = chooseExtent(create_info);
    if (Extent.width == 0u || Extent.height == 0u)
    {
        throw std::runtime_error("Cannot create a swapchain for a surface with no area: is the window minimized?");
    }
    MinImageCount = chooseMinImageCount(create_info);
    createSwapchainHandle(create_info, VK_NULL_HANDLE);
    setHdrMetadata(create_info);
    createSwapchainImages();
    createSwapchainImageViews();
}

SwapchainRecreateResult SwapchainImpl::Recreate(const SwapchainCreateInfo& create_info)
{
    if (create_info.RefreshRate > 1.0f)
    {
        RefreshRate = create_info.RefreshRate;
    }
    else if (create_info.PlatformSystemPtr != nullptr)
    {
        const auto* platformSystem = reinterpret_cast<const PlatformWindowSystem*>(create_info.PlatformSystemPtr);
        const auto& displayInfo = platformSystem->GetActiveDisplayInfo();
        const float queriedRate = displayInfo.RefreshRateCapabilities.RefreshRate;
        if (queriedRate > 1.0f)
        {
            RefreshRate = queriedRate;
        }
    }

    querySurface(create_info);
    const VkExtent2D newExtent = chooseExtent(create_info);
    if (newExtent.width == 0u || newExtent.height == 0u)
    {
        return SwapchainRecreateResult::SurfaceHasZeroArea;
    }

    // oldSwapchain must belong to the same surface
    const bool sameSurface = create_info.VkSurfaceHandle == CreateInfo.VkSurfaceHandle;
    CreateInfo = create_info;
    chooseSurfaceFormat(create_info);
    Extent = newExtent;
    MinImageCount = chooseMinImageCount(create_info);

    // the old swapchain lives until the new one exists, so that the presentation engine can hand its resources over
    destroySwapchainImageViews();
    const VkSwapchainKHR oldSwapchain = Swapchain;
    createSwapchainHandle(create_info, sameSurface ? oldSwapchain : VK_NULL_HANDLE);
    vkDestroySwapchainKHR(ParentDevice, oldSwapchain, nullptr);

    setHdrMetadata(create_info);
    createSwapchainImages();
    createSwapchainImageViews();
    return SwapchainRecreateResult::Success;
}

void SwapchainImpl::querySurface(const SwapchainCreateInfo& create_info)
{
    const rhi::Device* device = reinterpret_cast<const rhi::Device*>(create_info.RhiDevice);
    const VkPhysicalDevice physicalDevice = device->GetPhysicalDevice().As<VkPhysicalDevice>();
    const VkSurfaceKHR surface = reinterpret_cast<VkSurfaceKHR>(create_info.VkSurfaceHandle);
    Info = std::make_unique<SwapchainInfo>(physicalDevice, surface);
}

void SwapchainImpl::chooseSurfaceFormat(const SwapchainCreateInfo& create_info)
{
    // recall that VkSurfaceFormatKHR has two members: VkFormat format and VkColorSpaceKHR colorSpace, so that we're aware of both.
    // We'll need the former to set the swapchain image format, and the latter to inform the rendering pipeline and tonemapper about the color space in use.
    
    // a near match is an error: the caller needs exactly this encoding
    if (create_info.ExplicitSurfaceFormat.has_value())
    {
        const VkSurfaceFormatKHR requestedPair
        {
            ToVkFormat(create_info.ExplicitSurfaceFormat->Format),
            ToVkColorSpace(create_info.ExplicitSurfaceFormat->Space)
        };

        const auto offered = std::ranges::find_if(Info->formats, [&requestedPair](const VkSurfaceFormatKHR& surface_format)
        {
            return surface_format.format == requestedPair.format && surface_format.colorSpace == requestedPair.colorSpace;
        });

        if (offered == Info->formats.end())
        {
            throw std::runtime_error("The surface does not offer the explicitly requested format and color space pair");
        }

        VulkanFormat = *offered;
    }
    // Check if user specified a specific format, if so try to find closest match
    else if (create_info.SwapchainFormat.ComponentFormat != ImageComponentFormats::Invalid)
    {
        // User requested a specific format, try to find the closest match
        VkColorSpaceKHR preferredColorSpace = ToVkColorSpace(create_info.DesiredColorSpace);
        
        VkSurfaceFormatKHR requestedFormat
        {
            ToVkFormat(create_info.SwapchainFormat),
            preferredColorSpace
        };
        
        VulkanFormat = Info->FindClosestFormat(requestedFormat);
    }
    else
    {
        // No specific format requested, use our scoring heuristic to find the best one
        VulkanFormat = Info->FindBestFormat();
    }
}

VkExtent2D SwapchainImpl::chooseExtent(const SwapchainCreateInfo& create_info) const noexcept
{
    // the extent used to come from the active display info, which is the whole screen on the JSON path. The
    // surface knows the real size of the window
    const VkSurfaceCapabilitiesKHR& capabilities = Info->capabilities.surfaceCapabilities;
    constexpr uint32_t k_SurfaceTakesSwapchainExtent = std::numeric_limits<uint32_t>::max();
    if (capabilities.currentExtent.width != k_SurfaceTakesSwapchainExtent)
    {
        return capabilities.currentExtent;
    }

    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(static_cast<GLFWwindow*>(create_info.PlatformWindowHandle), &framebufferWidth, &framebufferHeight);
    const uint32_t clampedWidth = std::clamp(static_cast<uint32_t>(framebufferWidth), capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    const uint32_t clampedHeight = std::clamp(static_cast<uint32_t>(framebufferHeight), capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    return VkExtent2D{ clampedWidth, clampedHeight };
}

uint32_t SwapchainImpl::chooseMinImageCount(const SwapchainCreateInfo& create_info) const noexcept
{
    const VkSurfaceCapabilitiesKHR& capabilities = Info->capabilities.surfaceCapabilities;
    const uint32_t atLeastSurfaceMinimum = std::max(create_info.MinImageCount, capabilities.minImageCount);
    // a maximum of 0 means the surface sets no limit
    if (capabilities.maxImageCount == 0u)
    {
        return atLeastSurfaceMinimum;
    }
    return std::min(atLeastSurfaceMinimum, capabilities.maxImageCount);
}

void SwapchainImpl::createSwapchainHandle(const SwapchainCreateInfo& create_info, VkSwapchainKHR old_swapchain)
{
    const VkSwapchainCreateInfoKHR swapchainCreateInfo = getCreateInfo(create_info, old_swapchain);
    const VkResult result = vkCreateSwapchainKHR(ParentDevice, &swapchainCreateInfo, nullptr, &Swapchain);
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create swapchain!");
    }
    PresentMode = swapchainCreateInfo.presentMode;
}

void SwapchainImpl::ApplyHdrMetadata(const SwapchainHdrMetadata& metadata)
{
    const rhi::Device* device = reinterpret_cast<const rhi::Device*>(CreateInfo.RhiDevice);

    if (!device->HasExtension(VK_EXT_HDR_METADATA_EXTENSION_NAME))
    {
        return;
    }

    VkHdrMetadataEXT hdrMetadata{};
    hdrMetadata.sType = VK_STRUCTURE_TYPE_HDR_METADATA_EXT;
    hdrMetadata.pNext = nullptr;
    hdrMetadata.displayPrimaryRed.x = metadata.RedPrimaryX;
    hdrMetadata.displayPrimaryRed.y = metadata.RedPrimaryY;
    hdrMetadata.displayPrimaryGreen.x = metadata.GreenPrimaryX;
    hdrMetadata.displayPrimaryGreen.y = metadata.GreenPrimaryY;
    hdrMetadata.displayPrimaryBlue.x = metadata.BluePrimaryX;
    hdrMetadata.displayPrimaryBlue.y = metadata.BluePrimaryY;
    hdrMetadata.whitePoint.x = metadata.WhitePointX;
    hdrMetadata.whitePoint.y = metadata.WhitePointY;
    hdrMetadata.maxLuminance = metadata.MaxLuminance;
    hdrMetadata.minLuminance = metadata.MinLuminance;
    hdrMetadata.maxContentLightLevel = metadata.MaxContentLightLevel;
    hdrMetadata.maxFrameAverageLightLevel = metadata.MaxFrameAverageLightLevel;

    auto vkSetHdrMetadataEXT = reinterpret_cast<PFN_vkSetHdrMetadataEXT>(vkGetDeviceProcAddr(ParentDevice, "vkSetHdrMetadataEXT"));

    if (vkSetHdrMetadataEXT == nullptr)
    {
        throw std::runtime_error("Failed to get function pointer for vkSetHdrMetadataEXT, even with extension enabled and HDR on.");
    }

    vkSetHdrMetadataEXT(ParentDevice, 1, &Swapchain, &hdrMetadata);
}

void SwapchainImpl::setHdrMetadata(const SwapchainCreateInfo& create_info)
{
    // the caller sets the metadata
    if (create_info.ExplicitSurfaceFormat.has_value())
    {
        return;
    }

    const rhi::Device* device = reinterpret_cast<const rhi::Device*>(create_info.RhiDevice);
    assert(create_info.PlatformSystemPtr != nullptr && "PlatformSystemPtr must be set in SwapchainCreateInfo");
    const PlatformWindowSystem* platformSystem = reinterpret_cast<const PlatformWindowSystem*>(create_info.PlatformSystemPtr);
    const DisplayInfo& displayInfo = platformSystem->GetActiveDisplayInfo();

    if (!device->HasExtension(VK_EXT_HDR_METADATA_EXTENSION_NAME))
    {
        return;
    }

    // if HDR meatadata extension is supported, we can set HDR metadata on the swapchain if we're using an HDR format
    bool isHDRFormat = false;
    switch (ToVkFormat(create_info.SwapchainFormat))
    {
        case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
        case VK_FORMAT_R16G16B16A16_SFLOAT:
        case VK_FORMAT_R16G16B16A16_UNORM:
            isHDRFormat = true;
            break;
        default:
            isHDRFormat = false;
            break;
    }

    if (!isHDRFormat)
    {
        return;
    }

    const DisplayColorCapabilities& capabilities = displayInfo.ColorCapabilities;
    SwapchainHdrMetadata metadata{};
    metadata.RedPrimaryX = capabilities.RedPrimaryX;
    metadata.RedPrimaryY = capabilities.RedPrimaryY;
    metadata.GreenPrimaryX = capabilities.GreenPrimaryX;
    metadata.GreenPrimaryY = capabilities.GreenPrimaryY;
    metadata.BluePrimaryX = capabilities.BluePrimaryX;
    metadata.BluePrimaryY = capabilities.BluePrimaryY;
    metadata.WhitePointX = capabilities.WhitePointX;
    metadata.WhitePointY = capabilities.WhitePointY;
    metadata.MaxLuminance = capabilities.MaxLuminance;
    metadata.MinLuminance = capabilities.MinLuminance;
    metadata.MaxContentLightLevel = capabilities.MaxLuminance * 0.9f; // set based on preferences. we can add a config for this later
    metadata.MaxFrameAverageLightLevel = capabilities.MaxAverageLuminance;
    ApplyHdrMetadata(metadata);
}

void SwapchainImpl::Destroy()
{
    destroySwapchainImageViews();

    if (Swapchain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(ParentDevice, Swapchain, nullptr);
        Swapchain = VK_NULL_HANDLE;
    }
}

void SwapchainImpl::destroySwapchainImageViews()
{
    for (VkImageView imageView : ImageViews)
    {
        vkDestroyImageView(ParentDevice, imageView, nullptr);
    }
    ImageViews.clear();
    // images are owned by the swapchain, so we don't destroy them directly
    Images.clear();
    ImageCount = 0;
}

void SwapchainImpl::createSwapchainImages()
{
    // Retrieve the swapchain images
    uint32_t imageCount = 0;
    vkGetSwapchainImagesKHR(ParentDevice, Swapchain, &imageCount, nullptr);
    Images.resize(imageCount);
    vkGetSwapchainImagesKHR(ParentDevice, Swapchain, &imageCount, Images.data());
    ImageCount = imageCount;
    assert(ImageCount >= MinImageCount); // should never hit this because we did set this during construction
}

void SwapchainImpl::createSwapchainImageViews()
{
    ImageViews.resize(ImageCount);

    for (size_t i = 0; i < ImageCount; ++i)
    {
        VkImageViewCreateInfo viewCreateInfo{};
        viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewCreateInfo.image = Images[i];
        viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewCreateInfo.format = VulkanFormat.format;
        viewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewCreateInfo.subresourceRange.baseMipLevel = 0;
        viewCreateInfo.subresourceRange.levelCount = 1;
        viewCreateInfo.subresourceRange.baseArrayLayer = 0;
        viewCreateInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(ParentDevice, &viewCreateInfo, nullptr, &ImageViews[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create image views for swapchain images!");
        }
    }
}
