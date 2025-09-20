#include "Swapchain.hpp"
#include "SwapchainImpl.hpp"
#include "events/DisplayEvents.hpp"

// Forward declaration for conversion function
extern ColorSpace FromVkColorSpace(const VkColorSpaceKHR space) noexcept;

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

const void* Swapchain::GetNativeHandle() const noexcept
{
    return reinterpret_cast<const void*>(impl->Swapchain);
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

const void* Swapchain::GetImageHandle(uint32_t index) const noexcept
{
    if (index >= impl->ImageCount)
    {
        return nullptr;
    }
    return reinterpret_cast<const void*>(impl->Images[index]);
}

const void* Swapchain::GetImageViewHandle(uint32_t index) const noexcept
{
    if (index >= impl->ImageCount)
    {
        return nullptr;
    }
    return reinterpret_cast<const void*>(impl->ImageViews[index]);
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
