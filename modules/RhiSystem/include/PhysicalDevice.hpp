#pragma once
#ifndef RHI_SYSTEM_PHYSICAL_DEVICE_HPP
#define RHI_SYSTEM_PHYSICAL_DEVICE_HPP
#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>

namespace rhi 
{

struct QueueFamilyIndices 
{
    uint32_t graphics;
    uint32_t compute;
    uint32_t transfer;
    uint32_t sparseBinding;
    
    QueueFamilyIndices() :
        graphics{ VK_QUEUE_FAMILY_IGNORED },
        compute{ VK_QUEUE_FAMILY_IGNORED },
        transfer{ VK_QUEUE_FAMILY_IGNORED },
        sparseBinding{ VK_QUEUE_FAMILY_IGNORED }
    {
    }
    
    bool IsValid() const noexcept;
};

class PhysicalDevice 
{
public:
    explicit PhysicalDevice(VkInstance instance, uint32_t api_version);
    ~PhysicalDevice() = default;
    
    // Move-only
    PhysicalDevice(const PhysicalDevice&) = delete;
    PhysicalDevice& operator=(const PhysicalDevice&) = delete;
    PhysicalDevice(PhysicalDevice&&) noexcept = default;
    PhysicalDevice& operator=(PhysicalDevice&&) noexcept = default;
    
    // Core access
    VkPhysicalDevice GetHandle() const noexcept;
    
    // Properties
    const VkPhysicalDeviceProperties& GetProperties() const noexcept;
    
    const VkPhysicalDeviceFeatures& GetFeatures() const noexcept;
    
    const VkPhysicalDeviceMemoryProperties& GetMemoryProperties() const noexcept;
    
    // Queue families
    const QueueFamilyIndices& GetQueueFamilyIndices() const noexcept;
    
    const std::vector<VkQueueFamilyProperties>& GetQueueFamilyProperties() const noexcept;
    
    // Format support
    VkFormatProperties GetFormatProperties(VkFormat format) const;
    bool SupportsFormat(VkFormat format, VkImageTiling tiling, VkFormatFeatureFlags features) const;
    VkFormat FindSupportedFormat(const std::vector<VkFormat>& candidates, 
                                VkImageTiling tiling, 
                                VkFormatFeatureFlags features) const;

private:
    VkPhysicalDevice selectBestDevice(VkInstance instance, uint32_t api_version);
    void queryProperties();
    void findQueueFamilies();
    
    VkPhysicalDevice handle;
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceFeatures features;
    VkPhysicalDeviceMemoryProperties memoryProperties;
    QueueFamilyIndices queueFamilyIndices;
    std::vector<VkQueueFamilyProperties> queueFamilyProperties;
};

} // namespace rhi

#endif // RHI_SYSTEM_PHYSICAL_DEVICE_HPP