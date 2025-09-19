#pragma once
#ifndef RHI_SYSTEM_RHI_TYPES_HPP
#define RHI_SYSTEM_RHI_TYPES_HPP
#include <cstdint>

namespace rhi
{
    enum class ApiVersion : uint8_t 
    {
        None = 0,
        Vulkan10 = 1,
        Vulkan11 = 2,
        Vulkan12 = 3,
        Vulkan13 = 4,
        Vulkan14 = 5,
        Latest = 254
    };

    struct QueueFamilyIndices 
    {
        uint32_t Graphics{ ~0u };
        uint32_t Compute{ ~0u };
        uint32_t Transfer{ ~0u };
        uint32_t SparseBinding{ ~0u };
    };

    enum class ValidationLayers : uint8_t 
    {
        None = 0,
        BaseOnly = 1,               // Basic validation
        WithSynchronization = 2,    // Base + synchronization validation
        Full = 3                    // All available layers
    };

}

#endif //!RHI_SYSTEM_RHI_TYPES_HPP
