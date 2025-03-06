#pragma once
#ifndef DIAMOND_DOGS_RESOURCE_CONTEXT_TYPES_HPP
#define DIAMOND_DOGS_RESOURCE_CONTEXT_TYPES_HPP
#include <cstdint>

enum class resource_type : uint64_t
{
    Invalid = 0,
    Buffer = 1,
    Image = 2,
    Sampler = 3,
    CombinedImageSampler = 4
};

enum class resource_usage : uint32_t
{
    InvalidResourceUsage = 0,
    GPUOnly = 1,
    CPUOnly = 2,
    CPUToGPU = 3,
    GPUToCPU = 4
};

struct queue_family_flag_bits
{
    enum : uint8_t
    {
        None = 0x0,
        Graphics = 0x1,
        Compute = 0x2,
        Transfer = 0x4,
        SparseBinding = 0x8,
        // Used to indicate that we're using VK_SHARING_MODE_CONCURRENT, and that the queue family index is ignored
        Ignored = 0x10
    };
};

using queue_family_flags = uint8_t;

struct resource_creation_flag_bits
{
    enum : uint32_t
    {
        ResourceCreateDedicatedMemory = 0x00000001,
        ResourceCreateNeverAllocate = 0x00000002,
        ResourceCreatePersistentlyMapped = 0x00000004,
        ResourceCreateUserDataAsString = 0x00000020,
        ResourceCreateMemoryStrategyMinMemory = 0x00010000,
        ResourceCreateMemoryStrategyMinTime = 0x00020000,
        ResourceCreateMemoryStrategyMinFragmentation = 0x00040000
    };
};
using resource_creation_flags = uint32_t;

struct gpu_resource_data_t
{
    gpu_resource_data_t() noexcept = default;
    ~gpu_resource_data_t() noexcept = default;
    gpu_resource_data_t(const gpu_resource_data_t&) noexcept = delete;
    gpu_resource_data_t& operator=(const gpu_resource_data_t&) noexcept = delete;
    gpu_resource_data_t(gpu_resource_data_t&&) noexcept = default;
    gpu_resource_data_t& operator=(gpu_resource_data_t&&) noexcept = default;
    const void* Data{ 0u };
    size_t DataSize{ 0u };
    size_t DataAlignment{ 0u };
    queue_family_flags DestinationQueueFamily{ 0x0 };
};

struct gpu_image_resource_data_t
{
    gpu_image_resource_data_t() noexcept = default;
    ~gpu_image_resource_data_t() noexcept = default;
    gpu_image_resource_data_t(const gpu_image_resource_data_t&) noexcept = delete;
    gpu_image_resource_data_t& operator=(const gpu_image_resource_data_t&) noexcept = delete;
    gpu_image_resource_data_t(gpu_image_resource_data_t&&) noexcept = default;
    gpu_image_resource_data_t& operator=(gpu_image_resource_data_t&&) noexcept = default;
    const void* Data{ nullptr };
    size_t DataSize{ 0u };
    uint32_t Width{ 0u };
    uint32_t Height{ 0u };
    uint32_t ArrayLayer{ 0u };
    uint32_t NumLayers{ 1u };
    uint32_t MipLevel{ 0u };
    queue_family_flags DestinationQueueFamily{ 0x0 };
};

struct VulkanResource
{
    resource_type Type{ resource_type::Invalid };
    uint64_t Handle{ 0u };
    void* Info{ nullptr };
    uint64_t ViewHandle{ 0u };
    void* ViewInfo{ nullptr };
    void* UserData{ nullptr };
    VulkanResource* Sampler{ nullptr };
};

#endif //!DIAMOND_DOGS_RESOURCE_CONTEXT_TYPES_HPP
