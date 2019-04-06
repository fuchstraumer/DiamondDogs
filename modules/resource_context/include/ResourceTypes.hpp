#pragma once
#ifndef DIAMOND_DOGS_RESOURCE_CONTEXT_TYPES_HPP
#define DIAMOND_DOGS_RESOURCE_CONTEXT_TYPES_HPP
#include <cstdint>

enum class resource_type : uint64_t {
    INVALID = 0,
    BUFFER = 1,
    IMAGE = 2,
    SAMPLER = 3,
    COMBINED_IMAGE_SAMPLER = 4
};

enum class resource_usage : uint32_t {
    INVALID_RESOURCE_USAGE = 0,
    GPU_ONLY = 1,
    CPU_ONLY = 2,
    CPU_TO_GPU = 3,
    GPU_TO_CPU = 4
};

enum resource_creation_flags_bits : uint32_t {
    ResourceCreateDedicatedMemory = 0x00000001,
    ResourceCreateNeverAllocate = 0x00000002,
    ResourceCreatePersistentlyMapped = 0x00000004,
    ResourceCreateUserDataAsString = 0x00000020,
    ResourceCreateMemoryStrategyMinMemory = 0x00010000,
    ResourceCreateMemoryStrategyMinTime = 0x00020000,
    ResourceCreateMemoryStrategyMinFragmentation = 0x00040000
};
using resource_creation_flags = uint32_t;

struct gpu_resource_data_t {
    const void* Data;
    size_t DataSize;
    size_t DataAlignment;
    uint32_t Pitch;
    uint32_t SlicePitch;
};

struct gpu_image_resource_data_t {
    const void* Data = nullptr;
    size_t DataSize{ 0 };
    uint32_t Width{ 0 };
    uint32_t Height{ 0 };
    uint32_t ArrayLayer{ 0 };
    uint32_t NumLayers{ 1 };
    uint32_t MipLevel{ 0 };
};

struct VulkanResource {
    resource_type Type{ resource_type::INVALID };
    uint64_t Handle{ 0u };
    void* Info{ nullptr };
    uint64_t ViewHandle{ 0u };
    void* ViewInfo{ nullptr };
    void* UserData{ nullptr };
    VulkanResource* Sampler{ nullptr };
};

#endif //!DIAMOND_DOGS_RESOURCE_CONTEXT_TYPES_HPP
