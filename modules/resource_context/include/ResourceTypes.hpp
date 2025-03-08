#pragma once
#ifndef DIAMOND_DOGS_RESOURCE_CONTEXT_TYPES_HPP
#define DIAMOND_DOGS_RESOURCE_CONTEXT_TYPES_HPP
#include <cstdint>

enum class resource_type : uint64_t
{
    Invalid = 0,
    Buffer,
    BufferView,
    Image,
    ImageView,
    Sampler,
    CombinedImageSampler
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
    constexpr VulkanResource() noexcept : Type{ resource_type::Invalid }, ResourceHandle{ 0u }, VkHandle{ 0u }, VkViewHandle{ 0u }, VkSamplerHandle{ 0u } {}
    constexpr VulkanResource(
        const resource_type type,
        const uint32_t resource_handle,
        const uint64_t vk_handle,
        const uint64_t vk_view_handle,
        const uint64_t vk_sampler_handle) :
        Type{ type },
        ResourceHandle{ resource_handle },
        VkHandle{ vk_handle },
        VkViewHandle{ vk_view_handle },
        VkSamplerHandle{ vk_sampler_handle }
    {}
    constexpr ~VulkanResource() noexcept = default;
    constexpr VulkanResource(const VulkanResource&) noexcept = default;
    constexpr VulkanResource& operator=(const VulkanResource&) noexcept = default;
    constexpr VulkanResource(VulkanResource&&) noexcept = default;
    constexpr VulkanResource& operator=(VulkanResource&&) noexcept = default;

    static VulkanResource Null() noexcept;
    
    constexpr bool operator==(const VulkanResource& other) const noexcept
    {
        return Type == other.Type &&
               ResourceHandle == other.ResourceHandle &&
               VkHandle == other.VkHandle &&
               VkViewHandle == other.VkViewHandle &&
               VkSamplerHandle == other.VkSamplerHandle;
    }

    constexpr bool operator!=(const VulkanResource& other) const noexcept
    {
        return !(*this == other);
    }

    resource_type Type{ resource_type::Invalid };
    uint32_t ResourceHandle{ 0u };
    uint64_t VkHandle{ 0u };
    uint64_t VkViewHandle{ 0u };
    uint64_t VkSamplerHandle{ 0u };
};

#endif //!DIAMOND_DOGS_RESOURCE_CONTEXT_TYPES_HPP
