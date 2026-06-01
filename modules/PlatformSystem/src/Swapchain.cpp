#include "Swapchain.hpp"
#include "SwapchainImpl.hpp"
#include "events/DisplayEvents.hpp"
#include <unordered_map>

// Forward declaration for conversion function
extern ColorSpace FromVkColorSpace(const VkColorSpaceKHR space) noexcept;

static const std::unordered_map<VkPresentModeKHR, PresentMode> s_PresentModeFromVkPresentModeMap
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

Swapchain::Swapchain(const SwapchainCreateInfo& createInfo) :
    impl(std::make_unique<SwapchainImpl>(createInfo)),
    PlatformSystem(reinterpret_cast<PlatformWindowSystem*>(createInfo.PlatformSystemPtr))
{
    // impl ctor calls create, now we just register for the resize event
}

Swapchain::Swapchain(Swapchain&& other) noexcept : impl(std::move(other.impl)), PlatformSystem(other.PlatformSystem)
{
}

void Swapchain::Recreate(const SwapchainCreateInfo& createInfo)
{

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

ImageFormat Swapchain::GetImageFormat() const noexcept
{
    return FromVkFormat(impl->VulkanFormat.format);
}

ColorSpace Swapchain::ColorSpace() const noexcept
{
    return FromVkColorSpace(impl->VulkanFormat.colorSpace);
}

PresentMode Swapchain::GetPresentMode() const noexcept
{
    auto iter = s_PresentModeFromVkPresentModeMap.find(impl->PresentMode);
    if (iter != s_PresentModeFromVkPresentModeMap.end())
    {
        return iter->second;
    }
    return PresentMode::Invalid;
}

void* Swapchain::GetImageHandle(uint32_t index) const noexcept
{
    if (index >= impl->ImageCount)
    {
        return nullptr;
    }
    return reinterpret_cast<void*>(impl->Images[index]);
}

void* Swapchain::GetImageViewHandle(uint32_t index) const noexcept
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
        PlatformSystem = other.PlatformSystem;
        other.impl = nullptr;
    }
    return *this;
}

Swapchain::~Swapchain()
{
    Destroy();
}
