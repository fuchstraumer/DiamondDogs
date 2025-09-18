#include "Device.hpp"
#include "Instance.hpp"
#include "PhysicalDevice.hpp"
#include "ExtensionPack.hpp"
#include <stdexcept>
#include <vector>
#include <set>
#include <algorithm>
#include <iostream>

namespace rhi 
{

Device::Device(const Instance& instance, 
               const PhysicalDevice& physical_device,
               const ExtensionPack& extensions) :
    handle{ VK_NULL_HANDLE },
    instance{ instance },
    physicalDevice{ physical_device },
    numGraphicsQueues{ 0 },
    numComputeQueues{ 0 },
    numTransferQueues{ 0 },
    numSparseBindingQueues{ 0 }
{
    createDevice(extensions);
    setupQueues();
}

Device::~Device()
{
    if (handle != VK_NULL_HANDLE)
    {
        vkDestroyDevice(handle, nullptr);
    }
}

VkQueue Device::GetGraphicsQueue(uint32_t index) const
{
    if (index >= numGraphicsQueues)
    {
        throw std::out_of_range("Graphics queue index out of range");
    }
    return graphicsQueues[index];
}

VkQueue Device::GetComputeQueue(uint32_t index) const
{
    if (index >= numComputeQueues)
    {
        throw std::out_of_range("Compute queue index out of range");
    }
    return computeQueues[index];
}

VkQueue Device::GetTransferQueue(uint32_t index) const
{
    if (index >= numTransferQueues)
    {
        throw std::out_of_range("Transfer queue index out of range");
    }
    return transferQueues[index];
}

VkQueue Device::GetSparseBindingQueue(uint32_t index) const
{
    if (index >= numSparseBindingQueues)
    {
        throw std::out_of_range("Sparse binding queue index out of range");
    }
    return sparseBindingQueues[index];
}

VkQueue Device::GetGeneralQueue(uint32_t index) const
{
    // Prefer graphics queue as it supports all operations
    if (index < numGraphicsQueues)
    {
        return graphicsQueues[index];
    }
    
    // Fallback to compute queue
    if (index < numComputeQueues)
    {
        return computeQueues[index];
    }
    
    throw std::out_of_range("No general queue available at index");
}

bool Device::HasExtension(std::string_view extension_name) const noexcept
{
    return std::find(enabledExtensions.begin(), enabledExtensions.end(), extension_name) != enabledExtensions.end();
}

uint32_t Device::FindMemoryType(uint32_t type_filter, VkMemoryPropertyFlags properties) const
{
    const VkPhysicalDeviceMemoryProperties& mem_props = physicalDevice.GetMemoryProperties();
    
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i)
    {
        if ((type_filter & (1 << i)) && (mem_props.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }
    
    throw std::runtime_error("Failed to find suitable memory type");
}

void Device::WaitDeviceIdle() const
{
    vkDeviceWaitIdle(handle);
}

void Device::createDevice(const ExtensionPack& extensions)
{
    const QueueFamilyIndices& queue_indices = physicalDevice.GetQueueFamilyIndices();
    
    // Collect unique queue families
    std::set<uint32_t> unique_queue_families = 
    {
        queue_indices.graphics,
        queue_indices.compute,
        queue_indices.transfer,
        queue_indices.sparseBinding
    };
    
    // Create queue create info for each unique family
    std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
    std::vector<std::vector<float>> queue_priorities_storage;
    
    const std::vector<VkQueueFamilyProperties>& queue_props = physicalDevice.GetQueueFamilyProperties();
    
    for (uint32_t queue_family : unique_queue_families)
    {
        if (queue_family == VK_QUEUE_FAMILY_IGNORED)
        {
            continue;
        }
        
        const uint32_t queue_count = std::min(queue_props[queue_family].queueCount, 4u); // Limit to 4 queues max
        
        queue_priorities_storage.emplace_back(queue_count, 1.0f);
        
        VkDeviceQueueCreateInfo queue_create_info = 
        {
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            nullptr,
            0,
            queue_family,
            queue_count,
            queue_priorities_storage.back().data()
        };
        
        queue_create_infos.push_back(queue_create_info);
    }
    
    // Get device extensions
    const std::vector<const char*>& device_extensions = extensions.GetDeviceExtensions();
    
    // Store enabled extensions
    enabledExtensions.reserve(device_extensions.size());
    for (const char* ext : device_extensions)
    {
        enabledExtensions.emplace_back(ext);
    }
    
    // Device features
    const VkPhysicalDeviceFeatures2* device_features = extensions.GetDeviceFeatures();
    
    VkDeviceCreateInfo create_info = 
    {
        VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        device_features, // pNext chain for extended features
        0,
        static_cast<uint32_t>(queue_create_infos.size()),
        queue_create_infos.data(),
        0, // Layer count (deprecated at device level)
        nullptr, // Layer names (deprecated at device level)
        static_cast<uint32_t>(device_extensions.size()),
        device_extensions.data(),
        &device_features->features // Basic features
    };
    
    VkResult result = vkCreateDevice(physicalDevice.GetHandle(), &create_info, nullptr, &handle);
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create logical device");
    }
}

void Device::setupQueues()
{
    const QueueFamilyIndices& queue_indices = physicalDevice.GetQueueFamilyIndices();
    const std::vector<VkQueueFamilyProperties>& queue_props = physicalDevice.GetQueueFamilyProperties();
    
    // Setup graphics queues
    if (queue_indices.graphics != VK_QUEUE_FAMILY_IGNORED)
    {
        numGraphicsQueues = std::min(queue_props[queue_indices.graphics].queueCount, 4u);
        graphicsQueues.resize(numGraphicsQueues);
        
        for (uint32_t i = 0; i < numGraphicsQueues; ++i)
        {
            vkGetDeviceQueue(handle, queue_indices.graphics, i, &graphicsQueues[i]);
        }
    }
    
    // Setup compute queues
    if (queue_indices.compute != VK_QUEUE_FAMILY_IGNORED)
    {
        numComputeQueues = std::min(queue_props[queue_indices.compute].queueCount, 4u);
        computeQueues.resize(numComputeQueues);
        
        for (uint32_t i = 0; i < numComputeQueues; ++i)
        {
            vkGetDeviceQueue(handle, queue_indices.compute, i, &computeQueues[i]);
        }
    }
    
    // Setup transfer queues  
    if (queue_indices.transfer != VK_QUEUE_FAMILY_IGNORED)
    {
        numTransferQueues = std::min(queue_props[queue_indices.transfer].queueCount, 4u);
        transferQueues.resize(numTransferQueues);
        
        for (uint32_t i = 0; i < numTransferQueues; ++i)
        {
            vkGetDeviceQueue(handle, queue_indices.transfer, i, &transferQueues[i]);
        }
    }
    
    // Setup sparse binding queues
    if (queue_indices.sparseBinding != VK_QUEUE_FAMILY_IGNORED)
    {
        numSparseBindingQueues = std::min(queue_props[queue_indices.sparseBinding].queueCount, 4u);
        sparseBindingQueues.resize(numSparseBindingQueues);
        
        for (uint32_t i = 0; i < numSparseBindingQueues; ++i)
        {
            vkGetDeviceQueue(handle, queue_indices.sparseBinding, i, &sparseBindingQueues[i]);
        }
    }
    
    std::cout << "Device queues setup - Graphics: " << numGraphicsQueues 
              << ", Compute: " << numComputeQueues 
              << ", Transfer: " << numTransferQueues 
              << ", Sparse: " << numSparseBindingQueues << std::endl;
}

VkDevice Device::GetHandle() const noexcept
{
    return handle;
}

const PhysicalDevice& Device::GetPhysicalDevice() const noexcept
{
    return physicalDevice;
}

uint32_t Device::GetGraphicsQueueCount() const noexcept
{
    return numGraphicsQueues;
}

uint32_t Device::GetComputeQueueCount() const noexcept
{
    return numComputeQueues;
}

uint32_t Device::GetTransferQueueCount() const noexcept
{
    return numTransferQueues;
}

uint32_t Device::GetSparseBindingQueueCount() const noexcept
{
    return numSparseBindingQueues;
}

const std::vector<std::string>& Device::GetEnabledExtensions() const noexcept
{
    return enabledExtensions;
}

} // namespace rhi