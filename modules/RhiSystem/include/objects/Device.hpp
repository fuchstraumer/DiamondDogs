#pragma once
#ifndef RHI_SYSTEM_DEVICE_HPP
#define RHI_SYSTEM_DEVICE_HPP
#include "DebugUtilFns.hpp"
#include <vulkan/vulkan_core.h>
#include <vector>
#include <string>
#include <cstdint>

namespace rhi 
{

    class Instance;
    class PhysicalDevice;
    class ExtensionPack;

    class Device 
    {
    public:
        Device(const Instance* instance, 
               const PhysicalDevice* physical_device,
               const ExtensionPack& extensions);
        
        ~Device();
        
        // No copy/move
        Device(const Device&) = delete;
        Device& operator=(const Device&) = delete;
        
        // Core access
        VkDevice vkHandle() const noexcept;
        
        const PhysicalDevice& GetPhysicalDevice() const noexcept;
        
        // Queue access
        VkQueue GetGraphicsQueue(uint32_t index) const noexcept;
        VkQueue GetComputeQueue(uint32_t index) const noexcept;
        VkQueue GetTransferQueue(uint32_t index) const noexcept;
        VkQueue GetSparseBindingQueue(uint32_t index) const noexcept;
        
        // Get a queue that supports graphics, compute, and transfer (if available)
        VkQueue GetGeneralQueue() const noexcept;
        
        // Queue counts
        uint32_t GetGraphicsQueueCount() const noexcept;
        uint32_t GetComputeQueueCount() const noexcept;
        uint32_t GetTransferQueueCount() const noexcept;
        uint32_t GetSparseBindingQueueCount() const noexcept;
        
        // Extension queries
        bool HasExtension(std::string_view extension_name) const noexcept;
        const std::vector<std::string>& GetEnabledExtensions() const noexcept;
        
        // Memory utilities
        uint32_t FindMemoryType(uint32_t type_filter, VkMemoryPropertyFlags properties) const;
        
        void WaitDeviceIdle() const;

        const VkDebugUtilsFunctions& GetDebugUtilFns() const noexcept;

    private:
        void createDevice(const ExtensionPack& extensions);
        void setupQueues();
        void setupDebugUtils();
        
        VkDevice handle;
        const Instance* parentInstance;
        const PhysicalDevice* physicalDevice;
        
        // Queue handles
        std::vector<VkQueue> graphicsQueues;
        std::vector<VkQueue> computeQueues;
        std::vector<VkQueue> transferQueues;
        std::vector<VkQueue> sparseBindingQueues;
        
        // Queue counts
        uint32_t numGraphicsQueues;
        uint32_t numComputeQueues;
        uint32_t numTransferQueues;
        uint32_t numSparseBindingQueues;
        
        std::vector<std::string> enabledExtensions;
        VkDebugUtilsFunctions debugUtilsHandler;
    };

} // namespace rhi

#endif // RHI_SYSTEM_DEVICE_HPP
