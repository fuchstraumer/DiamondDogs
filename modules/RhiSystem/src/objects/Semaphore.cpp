#include "Semaphore.hpp"
#include "Device.hpp"
#include <vulkan/vulkan_core.h>
#include <cassert>

namespace rhi
{
#ifdef RHI_SYSTEM_USE_VULKAN

    BinarySemaphore::BinarySemaphore(DeviceHandle device) 
        : device{ device }, handle{ BinarySemaphoreHandle{0} }
    {
        VkSemaphoreCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkSemaphore vkSemaphore = VK_NULL_HANDLE;
        VkResult result = vkCreateSemaphore(device.As<VkDevice>(), &createInfo, nullptr, &vkSemaphore);
        if (result == VK_SUCCESS)
        {
            handle.Set<VkSemaphore>(vkSemaphore);
        }
        else
        {
            handle = SemaphoreHandle{ 0 };
        }
    }

    BinarySemaphore::BinarySemaphore(BinarySemaphore&& other) noexcept
        : device{ other.device }, handle{ std::move(other.handle) }
    {
        other.handle = SemaphoreHandle{ 0 };
        other.device = nullptr;
    }

    BinarySemaphore& BinarySemaphore::operator=(BinarySemaphore&& other) noexcept
    {
        if (this != &other)
        {
            device = other.device;
            handle = std::move(other.handle);
            other.handle = SemaphoreHandle{ 0 };
            other.device = nullptr;
        }
        return *this;
    }

    BinarySemaphore::~BinarySemaphore()
    {
        assert(device.IsValid() && "Device must be valid when destroying a binary semaphore");
        if (handle.IsValid() && device)
        {
            vkDestroySemaphore(device.As<VkDevice>(), handle.As<VkSemaphore>(), nullptr);
            handle = BinarySemaphoreHandle{ 0 };
        }
    }

    BinarySemaphoreHandle BinarySemaphore::Handle() const noexcept
    {
        return handle;
    }

    TimelineSemaphore::TimelineSemaphore(DeviceHandle device) 
        : device{ device }, handle{ TimelineSemaphoreHandle{0} }, currentValue{ 0u }
    {
        VkSemaphoreTypeCreateInfo timelineCreateInfo{};
        timelineCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        timelineCreateInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        timelineCreateInfo.initialValue = currentValue;

        VkSemaphoreCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        createInfo.pNext = &timelineCreateInfo;

        VkSemaphore vkSemaphore = VK_NULL_HANDLE;
        VkResult result = vkCreateSemaphore(device->vkHandle(), &createInfo, nullptr, &vkSemaphore);
        if (result == VK_SUCCESS)
        {
            handle.Set<VkSemaphore>(vkSemaphore);
        }
        else
        {
            handle = SemaphoreHandle{ 0 };
        }
    }

    TimelineSemaphore::TimelineSemaphore(TimelineSemaphore&& other) noexcept
        : device{ other.device }, handle{ std::move(other.handle) }, currentValue{ other.currentValue }
    {
        other.handle = SemaphoreHandle{ 0 };
        other.device = nullptr;
        other.currentValue = 0u;
    }

    TimelineSemaphore& TimelineSemaphore::operator=(TimelineSemaphore&& other) noexcept
    {
        if (this != &other)
        {
            device = other.device;
            handle = std::move(other.handle);
            currentValue = other.currentValue;
            other.handle = SemaphoreHandle{ 0 };
            other.device = nullptr;
            other.currentValue = 0u;
        }
        return *this;
    }

    TimelineSemaphore::~TimelineSemaphore()
    {
        assert(device.IsValid() && "Device must be valid when destroying a timeline semaphore");
        if (handle.IsValid() && device)
        {
            vkDestroySemaphore(device->vkHandle(), handle.As<VkSemaphore>(), nullptr);
            handle = SemaphoreHandle{ 0 };
            currentValue = 0u;
        }
    }

    TimelineSemaphoreHandle TimelineSemaphore::Handle() const noexcept
    {
        return handle;
    }

    uint64_t TimelineSemaphore::GetValue() const noexcept
    {
        uint64_t value = 0;
        VkResult = vkGetSemaphoreCounterValue(device.As<VkDevice>(), handle.As<VkSemaphore>(), &value);
        if (result == VK_SUCCESS)
        {
            // might want to log if value == currentValue, saying that the value hasn't changed since last checked?
            currentValue = value;
            return value;
        }
        else
        {
            assert(false && "Failed to get timeline semaphore value");
            return 0u;
        }
    }

    void TimelineSemaphore::SetValue(uint64_t value) noexcept
    {
        VkSemaphoreSignalInfo signalInfo{};
        signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
        signalInfo.semaphore = handle.As<VkSemaphore>();
        signalInfo.value = value;
        VkResult result = vkSignalSemaphore(device->vkHandle(), &signalInfo);
        if (result == VK_SUCCESS)
        {
            currentValue = value;
        }
        else
        {
            assert(false && "Failed to signal timeline semaphore");
        }
    }

    uint64_t TimelineSemaphore::Increment() noexcept
    {
        const uint64_t newValue = currentValue + 1;
        SetValue(newValue);
        return newValue;
    }
#endif // RHI_SYSTEM_USE_VULKAN

}