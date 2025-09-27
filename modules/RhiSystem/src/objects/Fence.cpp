#include "Fence.hpp"
#include "Device.hpp"
#include <vulkan/vulkan_core.h>
#include <utility>
#include <stdexcept>

namespace rhi
{
#ifdef RHI_SYSTEM_USE_VULKAN

    Fence::Fence(const Device* device, CreateFenceSignaled signaled) 
        : device{ device }, handle{ FenceHandle{0} }
    {
        VkFenceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        createInfo.flags = signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0;
        VkFence vkFence = VK_NULL_HANDLE;
        VkResult result = vkCreateFence(device->vkHandle(), &createInfo, nullptr, &vkFence);
        if (result == VK_SUCCESS)
        {
            handle.Set<VkFence>(vkFence);
        }
        else
        {
            handle = FenceHandle{ 0 };
        }
    }

    Fence::Fence(Fence&& other) noexcept
        : device{ other.device }, handle{ std::move(other.handle) }
    {
        other.handle = FenceHandle{ 0 };
        other.device = nullptr;
    }

    Fence& Fence::operator=(Fence&& other) noexcept
    {
        if (this != &other)
        {
            device = other.device;
            handle = std::move(other.handle);
            other.handle = FenceHandle{ 0 };
            other.device = nullptr;
        }
        return *this;
    }

    Fence::~Fence()
    {
        if (handle.IsValid() && device)
        {
            vkDestroyFence(device->vkHandle(), handle.As<VkFence>(), nullptr);
            handle = FenceHandle{ 0 };
        }
    }

    bool Fence::IsSignaled() const
    {
        if (!handle.IsValid() || !device)
        {
            throw std::runtime_error("Attempted to check status of an invalid fence!");
        }

        VkResult result = vkGetFenceStatus(device->vkHandle(), handle.As<VkFence>());
        if (result == VK_SUCCESS)
        {
            return true;
        }
        else if (result == VK_NOT_READY)
        {
            return false;
        }
        else
        {
            throw std::runtime_error("Failed to get fence status!");
        }
    }

#endif

}
