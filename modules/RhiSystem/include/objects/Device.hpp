#pragma once
#ifndef RHI_SYSTEM_DEVICE_HPP
#define RHI_SYSTEM_DEVICE_HPP
#include "DebugUtilFns.hpp"
#include "RhiHandle.hpp"
#include "RhiFlags.hpp"
#include <memory>
#include <span>
#include <vector>
#include <string>
#include <vulkan/vulkan_core.h>

namespace rhi 
{

    class Instance;
    class PhysicalDevice;
    struct DeviceImpl;

    class Device 
    {
    public:
        /** @brief Creates the device on a physical device that RhiSystem::StartDevice chose, with
         *  exactly these extensions and this feature chain (a VkPhysicalDeviceFeatures2). `enabled_features` names
         *  the members that the chain sets ("Struct.member"), sorted. On failure, Handle() is invalid. */
        Device(const Instance* instance,
               uint32_t api_version,
               PhysicalDeviceHandle physical_device,
               std::span<const char* const> extension_names,
               std::vector<std::string> core_extensions,
               const void* feature_chain,
               std::vector<std::string> enabled_features);

        ~Device();
        
        // No copy/move
        Device(const Device&) = delete;
        Device& operator=(const Device&) = delete;
        
        // Core access
        DeviceHandle Handle() const noexcept;
        
        PhysicalDeviceHandle GetPhysicalDevice() const noexcept;
        
        // Queue access
        QueueHandle GetGraphicsQueue(uint32_t index) const noexcept;
        QueueHandle GetComputeQueue(uint32_t index) const noexcept;
        QueueHandle GetTransferQueue(uint32_t index) const noexcept;
        QueueHandle GetSparseBindingQueue(uint32_t index) const noexcept;

        // Get a queue that supports graphics, compute, and transfer (if available)
        QueueHandle GetGeneralQueue() const noexcept;

        // Queue counts
        uint32_t GetGraphicsQueueCount() const noexcept;
        uint32_t GetComputeQueueCount() const noexcept;
        uint32_t GetTransferQueueCount() const noexcept;
        uint32_t GetSparseBindingQueueCount() const noexcept;
        const QueueFamilyIndices& GetQueueFamilyIndices() const noexcept;
    
        
        // Extension queries
        bool HasExtension(std::string_view extension_name) const noexcept;
        const std::vector<std::string>& GetEnabledExtensions() const noexcept;
        /** @brief Returns true when a need named this "Struct.member" feature and the device was created with it, for
         *  example "VkPhysicalDeviceVulkan13Features.subgroupSizeControl". Answers from the enabled set, never from
         *  the physical device. The name must match the need's name: an alias struct and its core struct are two
         *  names. */
        [[nodiscard]] bool IsFeatureEnabled(std::string_view feature_name) const noexcept;
        /** @brief The "Struct.member" features that the device was created with, sorted. */
        [[nodiscard]] std::span<const std::string> EnabledFeatures() const noexcept;
        
        void WaitDeviceIdle() const;

        const VkDebugUtilsFunctions& GetDebugUtilFns() const noexcept;
        /** @brief Names a Vulkan object of this device for validation messages and capture tools. Does nothing, and
         *  returns VK_ERROR_FEATURE_NOT_PRESENT, when the instance has no VK_EXT_debug_utils. Compiles to
         *  VK_SUCCESS without RHI_SYSTEM_VALIDATION_ENABLED and RHI_SYSTEM_USE_DEBUG_INFO. */
        VkResult SetObjectName(VkObjectType object_type, uint64_t object_handle, const char* name) const;

        uint32_t GetMemoryTypeIndex(const uint32_t type_bitfield, const uint32_t property_flags) const;

    private:
        [[nodiscard]] VkResult createLogicalDevice(std::span<const char* const> extension_names, const void* feature_chain);
        void setupQueues();
        void setupDebugUtils();
        void setupDeviceFaultHandler();

        // has the bonus of letting us swap members and contents based on RHI backend
        std::unique_ptr<DeviceImpl> impl;
        // handle stored here means good locality for majority of operations
        DeviceHandle handle;
    };

} // namespace rhi

#endif // RHI_SYSTEM_DEVICE_HPP
