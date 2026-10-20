#include "Fence.hpp"
#include "Device.hpp"
#include <vulkan/vulkan_core.h>
#include <utility>
#include <stdexcept>

namespace rhi
{
#ifdef RHI_SYSTEM_USE_VULKAN
    Fence::Fence(DeviceHandle device, CreateFenceSignaled signaled) 
        : device{ device }, handle{ FenceHandle{0} }
    {
        VkFenceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        createInfo.flags = signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0;
        VkFence vkFence = VK_NULL_HANDLE;
        VkResult result = vkCreateFence(device.As<VkDevice>(), &createInfo, nullptr, &vkFence);
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
        other.device = DeviceHandle{ 0 };
    }

    Fence& Fence::operator=(Fence&& other) noexcept
    {
        if (this != &other)
        {
            device = other.device;
            handle = std::move(other.handle);
            other.handle = FenceHandle{ 0 };
            other.device = DeviceHandle{ 0 };
        }
        return *this;
    }

    Fence::~Fence()
    {
        if (handle.IsValid() && device)
        {
            vkDestroyFence(device.As<VkDevice>(), handle.As<VkFence>(), nullptr);
            handle = FenceHandle{ 0 };
        }
    }

    bool Fence::IsSignaled() const
    {
        if (!handle.IsValid() || !device)
        {
            throw std::runtime_error("Attempted to check status of an invalid fence!");
        }

        VkResult result = vkGetFenceStatus(device.As<VkDevice>(), handle.As<VkFence>());
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

    // Handle, Reset, and Wait were declared and never defined, so any caller failed to link
    FenceHandle Fence::Handle() const noexcept
    {
        return handle;
    }

    void Fence::Reset()
    {
        const VkFence vkFence = handle.As<VkFence>();
        if (vkResetFences(device.As<VkDevice>(), 1, &vkFence) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to reset fence!");
        }
    }

    bool Fence::Wait(uint64_t timeout_ns) const
    {
        const VkFence vkFence = handle.As<VkFence>();
        const VkResult result = vkWaitForFences(device.As<VkDevice>(), 1, &vkFence, VK_TRUE, timeout_ns);
        if (result == VK_SUCCESS)
        {
            return true;
        }
        else if (result == VK_TIMEOUT)
        {
            return false;
        }
        else
        {
            throw std::runtime_error("Failed to wait on fence!");
        }
    }
#endif

}
