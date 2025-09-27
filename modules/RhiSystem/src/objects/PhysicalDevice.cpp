#include "PhysicalDevice.hpp"
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <limits>
#include <iostream>
#include <vulkan/vulkan_core.h>

namespace rhi 
{

    PhysicalDevice::PhysicalDevice(InstanceHandle instance, uint32_t api_version) : handle{ 0u }
    {
        PhysicalDeviceHandle bestDevice = selectBestDevice(instance, api_version);
        handle = bestDevice;
    }

    PhysicalDeviceHandle PhysicalDevice::selectBestDevice(InstanceHandle instance, uint32_t api_version)
    {
        uint32_t device_count = 0;
        vkEnumeratePhysicalDevices(instance.As<VkInstance>(), &device_count, nullptr);
        
        if (device_count == 0)
        {
            throw std::runtime_error("No Vulkan-compatible physical devices found");
        }
        
        std::vector<VkPhysicalDevice> devices(device_count);
        vkEnumeratePhysicalDevices(instance.As<VkInstance>(), &device_count, devices.data());
        
        // Simple scoring system - prefer discrete GPUs
        VkPhysicalDevice best_device = VK_NULL_HANDLE;
        int best_score = -1;
        
        for (VkPhysicalDevice device : devices)
        {
            VkPhysicalDeviceProperties device_props;
            vkGetPhysicalDeviceProperties(device, &device_props);
            
            // Check API version support
            if (device_props.apiVersion < api_version)
            {
                continue;
            }
            
            int score = 0;
            
            // Prefer discrete GPUs
            if (device_props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            {
                score += 1000;
            }
            else if (device_props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
            {
                score += 100;
            }
            
            // Add points for more memory
            VkPhysicalDeviceMemoryProperties mem_props;
            vkGetPhysicalDeviceMemoryProperties(device, &mem_props);
            
            VkDeviceSize total_memory = 0;
            for (uint32_t i = 0; i < mem_props.memoryHeapCount; ++i)
            {
                if (mem_props.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                {
                    total_memory += mem_props.memoryHeaps[i].size;
                }
            }
            
            score += static_cast<int>(total_memory / (1024 * 1024 * 1024)); // GB of memory
            
            if (score > best_score)
            {
                best_score = score;
                best_device = device;
            }
        }
        
        if (best_device == VK_NULL_HANDLE)
        {
            throw std::runtime_error("No suitable physical device found");
        }
        
        return PhysicalDeviceHandle{ reinterpret_cast<uint64_t>(best_device) };
    }

    PhysicalDeviceHandle PhysicalDevice::Handle() const noexcept
    {
        return handle;
    }

}
