#include "Swapchain.hpp"
#include "ImageDataFormats.hpp"
#include "ImageDataFormatsVulkan.hpp"
#include "math/Math.hpp"
#include "PlatformTypes.hpp"
#include "SwapchainImpl.hpp"
#include "vulkan/vulkan_core.h"
#include <chrono>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>

// Forward declaration for conversion function
extern ColorSpace FromVkColorSpace(const VkColorSpaceKHR space) noexcept;

static const std::unordered_map<VkPresentModeKHR, PresentMode> k_SPresentModeFromVkPresentModeMap
{
    { VK_PRESENT_MODE_IMMEDIATE_KHR, PresentMode::Immediate },
    { VK_PRESENT_MODE_FIFO_KHR, PresentMode::VerticalSync },
    { VK_PRESENT_MODE_FIFO_RELAXED_KHR, PresentMode::VerticalSyncRelaxed },
    { VK_PRESENT_MODE_MAILBOX_KHR, PresentMode::VerticalSyncMailbox },
    { VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR, PresentMode::SharedDemandRefresh },
    { VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR, PresentMode::SharedContinuousRefresh },
    { VK_PRESENT_MODE_FIFO_LATEST_READY_KHR, PresentMode::VerticalSyncLatestReady },
    { VK_PRESENT_MODE_FIFO_LATEST_READY_EXT, PresentMode::VerticalSyncLatestReady },
    { VK_PRESENT_MODE_MAX_ENUM_KHR, PresentMode::Invalid } // just to have a max enum value
};

Swapchain::Swapchain(const SwapchainCreateInfo& create_info) :
    impl(std::make_unique<SwapchainImpl>(create_info)),
    platformSystem(reinterpret_cast<PlatformWindowSystem*>(create_info.PlatformSystemPtr))
{
    // impl ctor calls create, now we just register for the resize event
}
    
Swapchain::Swapchain(Swapchain&& other) noexcept : impl(std::move(other.impl)), platformSystem(other.platformSystem)
{
}

std::vector<SurfaceFormatPair> Swapchain::QuerySurfaceFormats(const SwapchainCreateInfo& create_info)
{
    return QuerySurfaceFormatPairs(create_info);
}

SwapchainRecreateResult Swapchain::Recreate(const SwapchainCreateInfo& create_info)
{
    return impl->Recreate(create_info);
}

SwapchainRecreateResult Swapchain::Recreate()
{
    const SwapchainCreateInfo lastCreateInfo = impl->CreateInfo;
    return impl->Recreate(lastCreateInfo);
}

void Swapchain::Destroy()
{
    impl.reset();
}

void* Swapchain::GetNativeHandle() const noexcept
{
    return reinterpret_cast<void*>(impl->Swapchain);
}

math::Float2 Swapchain::GetExtent() const noexcept
{
    return math::Float2{ static_cast<float>(impl->Extent.width), static_cast<float>(impl->Extent.height) };
}

uint32_t Swapchain::ImageCount() const noexcept
{
    return impl->ImageCount;
}

ImageFormat Swapchain::ImageFormat() const noexcept
{
    return FromVkFormat(impl->VulkanFormat.format);
}

ColorSpace Swapchain::ColorSpace() const noexcept
{
    return FromVkColorSpace(impl->VulkanFormat.colorSpace);
}

PresentMode Swapchain::PresentMode() const noexcept
{
    auto iter = k_SPresentModeFromVkPresentModeMap.find(impl->PresentMode);
    if (iter != k_SPresentModeFromVkPresentModeMap.end())
    {
        return iter->second;
    }
    return PresentMode::Invalid;
}

float Swapchain::RefreshRate() const noexcept
{
    return impl ? impl->RefreshRate : 60.0f;
}

std::chrono::duration<double, std::milli> Swapchain::TargetFrameInterval() const noexcept
{
    const double fps = static_cast<double>(RefreshRate());
    const double validFps = (fps > 1.0) ? fps : 60.0;
    return std::chrono::duration<double, std::milli>(1000.0 / validFps);
}

void* Swapchain::ImageHandle(uint32_t index) const noexcept
{
    if (index >= impl->ImageCount)
    {
        return nullptr;
    }
    return reinterpret_cast<void*>(impl->Images[index]);
}

void* Swapchain::ImageViewHandle(uint32_t index) const noexcept
{
    if (index >= impl->ImageCount)
    {
        return nullptr;
    }
    return reinterpret_cast<void*>(impl->ImageViews[index]);
}

Swapchain& Swapchain::operator=(Swapchain&& other) noexcept
{
    if (this != &other)
    {
        impl = std::move(other.impl);
        platformSystem = other.platformSystem;
        other.platformSystem = nullptr;
        other.impl = nullptr;
    }
    return *this;
}

Swapchain::~Swapchain()
{
    Destroy();
}

bool Swapchain::SupportsTransferSource() const noexcept
{
    return impl->Info != nullptr &&
        (impl->Info->capabilities.surfaceCapabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0u;
}

void Swapchain::SetHdrMetadata(const SwapchainHdrMetadata& metadata)
{
    impl->ApplyHdrMetadata(metadata);
}

