#pragma once
#ifndef RHI_SYSTEM_PHYSICAL_DEVICE_HPP
#define RHI_SYSTEM_PHYSICAL_DEVICE_HPP
#include "RhiTypes.hpp"
#include "RhiHandle.hpp"
#include <cstdint>

namespace rhi 
{

    class PhysicalDevice 
    {
    public:
        explicit PhysicalDevice(InstanceHandle instance, uint32_t api_version);
        ~PhysicalDevice() = default;
        
        // Move-only
        PhysicalDevice(const PhysicalDevice&) = delete;
        PhysicalDevice& operator=(const PhysicalDevice&) = delete;
        PhysicalDevice(PhysicalDevice&&) noexcept = default;
        PhysicalDevice& operator=(PhysicalDevice&&) noexcept = default;

        PhysicalDeviceHandle Handle() const noexcept;

    private:
        PhysicalDeviceHandle selectBestDevice(InstanceHandle instance, uint32_t api_version);    
        PhysicalDeviceHandle handle;
    };

} // namespace rhi

#endif // RHI_SYSTEM_PHYSICAL_DEVICE_HPP
