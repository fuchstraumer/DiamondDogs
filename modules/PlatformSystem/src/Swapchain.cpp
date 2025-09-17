#include "Swapchain.hpp"
#include "SwapchainImpl.hpp"
#include "events/DisplayEvents.hpp"

Swapchain::Swapchain(const SwapchainCreateInfo& createInfo) :
    impl(std::make_unique<SwapchainImpl>(createInfo)),
    PlatformSystem(reinterpret_cast<PlatformWindowSystem*>(createInfo.PlatformSystemPtr))
{
    // impl ctor calls create, now we just register for the resize event
}

Swapchain::~Swapchain()
{
    // impl dtor calls destroy
}
