#include "PhysicalDevice.hpp"
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <limits>
#include <iostream>

namespace rhi 
{

PhysicalDevice::PhysicalDevice(VkInstance instance, uint32_t api_version) :
    handle{ VK_NULL_HANDLE },
    properties{},
    features{},
    memoryProperties{},
    queueFamilyIndices{}
{
    handle = selectBestDevice(instance, api_version);
    queryProperties();
    findQueueFamilies();
}

VkFormatProperties PhysicalDevice::GetFormatProperties(VkFormat format) const
{
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(handle, format, &props);
    return props;
}

bool PhysicalDevice::SupportsFormat(VkFormat format, VkImageTiling tiling, VkFormatFeatureFlags features) const
{
    VkFormatProperties props = GetFormatProperties(format);
    
    if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features)
    {
        return true;
    }
    else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features)
    {
        return true;
    }
    
    return false;
}

VkFormat PhysicalDevice::FindSupportedFormat(const std::vector<VkFormat>& candidates, 
                                             VkImageTiling tiling, 
                                             VkFormatFeatureFlags features) const
{
    for (VkFormat format : candidates)
    {
        if (SupportsFormat(format, tiling, features))
        {
            return format;
        }
    }
    
    return VK_FORMAT_UNDEFINED;
}

VkPhysicalDevice PhysicalDevice::selectBestDevice(VkInstance instance, uint32_t api_version)
{
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
    
    if (device_count == 0)
    {
        throw std::runtime_error("No Vulkan-compatible physical devices found");
    }
    
    std::vector<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(instance, &device_count, devices.data());
    
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
    
    return best_device;
}

void PhysicalDevice::queryProperties()
{
    vkGetPhysicalDeviceProperties(handle, &properties);
    vkGetPhysicalDeviceFeatures(handle, &features);
    vkGetPhysicalDeviceMemoryProperties(handle, &memoryProperties);
    
    std::cout << "Selected physical device: " << properties.deviceName << std::endl;
}

void PhysicalDevice::findQueueFamilies()
{
    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(handle, &queue_family_count, nullptr);
    
    queueFamilyProperties.resize(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(handle, &queue_family_count, queueFamilyProperties.data());
    
    // Find queue families
    for (uint32_t i = 0; i < queue_family_count; ++i)
    {
        const VkQueueFamilyProperties& props = queueFamilyProperties[i];
        
        // Graphics queue (also supports compute and transfer)
        if (props.queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            if (queueFamilyIndices.graphics == VK_QUEUE_FAMILY_IGNORED)
            {
                queueFamilyIndices.graphics = i;
            }
        }
        
        // Dedicated compute queue (compute but not graphics)
        if ((props.queueFlags & VK_QUEUE_COMPUTE_BIT) && !(props.queueFlags & VK_QUEUE_GRAPHICS_BIT))
        {
            if (queueFamilyIndices.compute == VK_QUEUE_FAMILY_IGNORED)
            {
                queueFamilyIndices.compute = i;
            }
        }
        
        // Dedicated transfer queue (transfer but not graphics or compute)
        if ((props.queueFlags & VK_QUEUE_TRANSFER_BIT) && 
            !(props.queueFlags & VK_QUEUE_GRAPHICS_BIT) && 
            !(props.queueFlags & VK_QUEUE_COMPUTE_BIT))
        {
            if (queueFamilyIndices.transfer == VK_QUEUE_FAMILY_IGNORED)
            {
                queueFamilyIndices.transfer = i;
            }
        }
        
        // Sparse binding queue
        if (props.queueFlags & VK_QUEUE_SPARSE_BINDING_BIT)
        {
            if (queueFamilyIndices.sparseBinding == VK_QUEUE_FAMILY_IGNORED)
            {
                queueFamilyIndices.sparseBinding = i;
            }
        }
    }
    
    // Fallback: if no dedicated compute queue, use graphics queue
    if (queueFamilyIndices.compute == VK_QUEUE_FAMILY_IGNORED)
    {
        queueFamilyIndices.compute = queueFamilyIndices.graphics;
    }
    
    // Fallback: if no dedicated transfer queue, use graphics queue
    if (queueFamilyIndices.transfer == VK_QUEUE_FAMILY_IGNORED)
    {
        queueFamilyIndices.transfer = queueFamilyIndices.graphics;
    }
    
    // Fallback: if no sparse binding queue, use graphics queue
    if (queueFamilyIndices.sparseBinding == VK_QUEUE_FAMILY_IGNORED)
    {
        queueFamilyIndices.sparseBinding = queueFamilyIndices.graphics;
    }
    
    if (!queueFamilyIndices.IsValid())
    {
        throw std::runtime_error("Failed to find required queue families");
    }
}

bool QueueFamilyIndices::IsValid() const noexcept
{
    return graphics != VK_QUEUE_FAMILY_IGNORED;
}

VkPhysicalDevice PhysicalDevice::GetHandle() const noexcept
{
    return handle;
}

const VkPhysicalDeviceProperties& PhysicalDevice::GetProperties() const noexcept
{
    return properties;
}

const VkPhysicalDeviceFeatures& PhysicalDevice::GetFeatures() const noexcept
{
    return features;
}

const VkPhysicalDeviceMemoryProperties& PhysicalDevice::GetMemoryProperties() const noexcept
{
    return memoryProperties;
}

const QueueFamilyIndices& PhysicalDevice::GetQueueFamilyIndices() const noexcept
{
    return queueFamilyIndices;
}

const std::vector<VkQueueFamilyProperties>& PhysicalDevice::GetQueueFamilyProperties() const noexcept
{
    return queueFamilyProperties;
}

} // namespace rhi