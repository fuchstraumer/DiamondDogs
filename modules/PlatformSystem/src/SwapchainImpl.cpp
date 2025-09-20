#include "SwapchainImpl.hpp"
#include "Swapchain.hpp"
#include "ImageDataFormats.hpp"
#include "PlatformSystem.hpp"
#include "events/DisplayEvents.hpp"
#include <GLFW/glfw3.h>
#include <stdexcept>

struct AppSurfaceFormat
{
    ImageFormat Format;
    ColorSpace Space;
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

bool operator==(const VkSurfaceFormat2KHR& vkSurfaceFormat, const AppSurfaceFormat& appSurfaceFormat) noexcept
{
    const VkFormat appVkFormat = ToVkFormat(appSurfaceFormat.Format);
    const VkColorSpaceKHR appColorSpace = ToVkColorSpace(appSurfaceFormat.Space);
    return (vkSurfaceFormat.surfaceFormat.format == appVkFormat) && (vkSurfaceFormat.surfaceFormat.colorSpace == appColorSpace);
}

SwapchainInfo::SwapchainInfo(const VkPhysicalDevice& dvc, const VkSurfaceKHR& sfc)
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

VkSurfaceFormatKHR SwapchainInfo::GetBestFormat(bool tryEnableHDR, ColorSpace preferredSdrColorSpace, ColorSpace preferredHdrColorSpace) const noexcept
{
    // Find the best format based on the preferred color spaces
    for (const auto& format : formats)
    {
        if (tryEnableHDR && format.colorSpace == ToVkColorSpace(preferredHdrColorSpace))
        {
            return format;
        }
        else if (format.colorSpace == ToVkColorSpace(preferredSdrColorSpace))
        {
            return format;
        }
    }

    // Fallback to the first format if no preferred formats are found
    return formats.empty() ? VkSurfaceFormatKHR{} : formats[0];
}

SwapchainImpl::SwapchainImpl(const SwapchainCreateInfo& createInfo) :
    ParentDevice(reinterpret_cast<VkDevice>(createInfo.VkDeviceHandle)),
    Format(VK_FORMAT_UNDEFINED),
    Extent({ 0, 0 }),
    MinImageCount(createInfo.MinImageCount),
    ImageCount(0),
    Images(),
    ImageViews(),
    PresentMode(VK_PRESENT_MODE_FIFO_KHR),
    Swapchain(VK_NULL_HANDLE),
    OldSwapchain(VK_NULL_HANDLE)
{
    Create(createInfo);
}

SwapchainImpl::~SwapchainImpl()
{
    Destroy();
}


VkSwapchainCreateInfoKHR SwapchainImpl::GetCreateInfo(const SwapchainCreateInfo& createInfo) const noexcept
{
    VkSwapchainCreateInfoKHR swapchainCreateInfo;
    swapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainCreateInfo.pNext = nullptr;
    swapchainCreateInfo.flags = 0;

    swapchainCreateInfo.surface = reinterpret_cast<VkSurfaceKHR>(createInfo.VkSurfaceHandle);
    swapchainCreateInfo.minImageCount = createInfo.MinImageCount;
    swapchainCreateInfo.imageFormat = Format.format;
    swapchainCreateInfo.imageColorSpace = Format.colorSpace;
    swapchainCreateInfo.imageExtent = Extent;
    swapchainCreateInfo.imageArrayLayers = 1; // always 1 unless doing stereoscopic 3D
    // will need color attachment bits because we also write to the swapchain images, not just present them
    swapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; // assume only one queue family for now
    swapchainCreateInfo.queueFamilyIndexCount = 0;
    swapchainCreateInfo.pQueueFamilyIndices = nullptr;
    swapchainCreateInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR; // no pre-transform
    swapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; // ignore alpha channel, for now - no blending with other windows or layers
    swapchainCreateInfo.presentMode = ToVkPresentMode(createInfo.SwapchainPresentMode);
    swapchainCreateInfo.clipped = VK_TRUE; // expected default, we should hopefully never change this
    swapchainCreateInfo.oldSwapchain = OldSwapchain;

    // this will need to be adjusted if we ever start setting pointer parameters....
    assert(swapchainCreateInfo.pNext == nullptr && (swapchainCreateInfo.pQueueFamilyIndices == nullptr));
    return swapchainCreateInfo;
}

void SwapchainImpl::Create(const SwapchainCreateInfo& createInfo)
{
    VkPhysicalDevice physicalDevice = reinterpret_cast<VkPhysicalDevice>(createInfo.VkPhysicalDeviceHandle);
    VkSurfaceKHR surface = reinterpret_cast<VkSurfaceKHR>(createInfo.VkSurfaceHandle);

    Info = std::make_unique<SwapchainInfo>(physicalDevice, surface);

    // recall that VkSurfaceFormatKHR has two members: VkFormat format and VkColorSpaceKHR colorSpace, so that we're aware of both.
    // We'll need the former to set the swapchain image format, and the latter to inform the rendering pipeline and tonemapper about the color space in use.
    Format = Info->GetBestFormat(createInfo.TryEnableHDR, createInfo.SdrColorSpace, createInfo.HdrColorSpace);
    // get info about extents and dimensions from the platform system, since that chooses the primary display for us
    assert(createInfo.PlatformSystemPtr != nullptr && "PlatformSystemPtr must be set in SwapchainCreateInfo");
    const PlatformWindowSystem* platformSystem = reinterpret_cast<const PlatformWindowSystem*>(createInfo.PlatformSystemPtr);
    DisplayInfo displayInfo = platformSystem->GetActiveDisplayInfo();
    // need to add retrieval based on display index later, when we support multiple monitors and use this code more to drive implementation better
    Extent.width = displayInfo.Width;
    Extent.height = displayInfo.Height;

    // internal parameters set, retrieve create info using said parameters now
    const VkSwapchainCreateInfoKHR swapchainCreateInfo = GetCreateInfo(createInfo);

    // Create the swapchain
    VkResult result = vkCreateSwapchainKHR(ParentDevice, &swapchainCreateInfo, nullptr, &Swapchain);
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create swapchain!");
    }

    CreateSwapchainImages();
    CreateSwapchainImageViews();
}

void SwapchainImpl::Destroy()
{
    
    for (auto imageView : ImageViews)
    {
        if (imageView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(ParentDevice, imageView, nullptr);
        }
    }

    ImageViews.clear();

    for (auto image : Images)
    {
        image = VK_NULL_HANDLE; // images are owned by the swapchain, so we don't destroy them directly
    }

    Images.clear();
    ImageCount = 0;

    if (Swapchain != VK_NULL_HANDLE)
    {
        // persist old handle in case this is a recreation call
        OldSwapchain = Swapchain;
        vkDestroySwapchainKHR(ParentDevice, Swapchain, nullptr);
        Swapchain = VK_NULL_HANDLE;
    }

}

void SwapchainImpl::CreateSwapchainImages()
{
    // Retrieve the swapchain images
    uint32_t imageCount = 0;
    vkGetSwapchainImagesKHR(ParentDevice, Swapchain, &imageCount, nullptr);
    Images.resize(imageCount);
    vkGetSwapchainImagesKHR(ParentDevice, Swapchain, &imageCount, Images.data());
    ImageCount = imageCount;
    assert(ImageCount >= MinImageCount); // should never hit this because we did set this during construction
}

void SwapchainImpl::CreateSwapchainImageViews()
{
    ImageViews.resize(ImageCount);

    for (size_t i = 0; i < ImageCount; ++i)
    {
        VkImageViewCreateInfo viewCreateInfo{};
        viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewCreateInfo.image = Images[i];
        viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewCreateInfo.format = Format.format;
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

#pragma warning(push)
#pragma warning(disable: 4302)
#pragma warning(disable: 4311)
void RecreateSwapchain(const Events::SwapchainRecreateEventData& event, void* userData)
{
    int width = 0;
    int height = 0;

    while (width == 0 || height == 0)
    {
        glfwGetFramebufferSize(reinterpret_cast<GLFWwindow*>(event.WindowHandle), &width, &height);
        glfwWaitEvents();
    }
}
#pragma warning(pop)