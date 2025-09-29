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
            handle = BinarySemaphoreHandle{ 0 };
        }
    }

    BinarySemaphore::BinarySemaphore(BinarySemaphore&& other) noexcept
        : device{ other.device }, handle{ std::move(other.handle) }
    {
        other.handle = BinarySemaphoreHandle{ 0 };
        other.device = DeviceHandle{ 0u };
    }

    BinarySemaphore& BinarySemaphore::operator=(BinarySemaphore&& other) noexcept
    {
        if (this != &other)
        {
            device = other.device;
            handle = std::move(other.handle);
            other.handle = BinarySemaphoreHandle{ 0 };
            other.device = DeviceHandle{ 0u };
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
        VkResult result = vkCreateSemaphore(device.As<VkDevice>(), &createInfo, nullptr, &vkSemaphore);
        if (result == VK_SUCCESS)
        {
            handle.Set<VkSemaphore>(vkSemaphore);
        }
        else
        {
            handle = TimelineSemaphoreHandle{ 0 };
        }
    }

    TimelineSemaphore::TimelineSemaphore(TimelineSemaphore&& other) noexcept
        : device{ other.device }, handle{ std::move(other.handle) }, currentValue{ other.currentValue }
    {
        other.handle = TimelineSemaphoreHandle{ 0 };
        other.device = DeviceHandle{ 0u };
        other.currentValue = 0u;
    }

    TimelineSemaphore& TimelineSemaphore::operator=(TimelineSemaphore&& other) noexcept
    {
        if (this != &other)
        {
            device = other.device;
            handle = std::move(other.handle);
            currentValue = other.currentValue;
            other.handle = TimelineSemaphoreHandle{ 0 };
            other.device = DeviceHandle{ 0u };
            other.currentValue = 0u;
        }
        return *this;
    }

    TimelineSemaphore::~TimelineSemaphore()
    {
        assert(device.IsValid() && "Device must be valid when destroying a timeline semaphore");
        if (handle.IsValid() && device)
        {
            vkDestroySemaphore(device.As<VkDevice>(), handle.As<VkSemaphore>(), nullptr);
            handle = TimelineSemaphoreHandle{ 0 };
            currentValue = 0u;
        }
    }

    TimelineSemaphoreHandle TimelineSemaphore::Handle() const noexcept
    {
        return handle;
    }

    Result TimelineSemaphore::GetValue(uint64_t& value) const noexcept
    {
        VkResult vk_result = vkGetSemaphoreCounterValue(device.As<VkDevice>(), handle.As<VkSemaphore>(), &value);
        if (vk_result == VK_SUCCESS)
        {
            // might want to log if value == currentValue, saying that the value hasn't changed since last checked?
            currentValue = value;
            return Result::Success();
        }
        else
        {
            return Result(vk_result);
        }
    }

    Result TimelineSemaphore::SetValue(uint64_t value) noexcept
    {
        VkSemaphoreSignalInfo signalInfo{};
        signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
        signalInfo.semaphore = handle.As<VkSemaphore>();
        signalInfo.value = value;
        VkResult vk_result = vkSignalSemaphore(device.As<VkDevice>(), &signalInfo);
        if (vk_result == VK_SUCCESS)
        {
            currentValue = value;
            return Result::Success();
        }
        else
        {
            return Result(vk_result);
        }
    }

    Result TimelineSemaphore::Increment(uint64_t newValue) noexcept
    {
        const uint64_t new_value = currentValue + 1;
        Result result = SetValue(new_value);
        if (result.IsSuccess())
        {
            newValue = new_value;
            return Result::Success();
        }
        else
        {
            return result;
        }
    }
#endif // RHI_SYSTEM_USE_VULKAN

}