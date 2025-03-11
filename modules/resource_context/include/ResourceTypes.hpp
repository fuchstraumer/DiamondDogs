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
        UserDataAsString = 0x00000001,
        DedicatedMemory = 0x00000002,
        CreateMapped = 0x00000004,
        PersistentlyMapped = 0x00000008,
        HostWritesLinear = 0x00000010,
        HostWritesRandom = 0x00000020,
        
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

struct GraphicsResource
{
    GraphicsResource() noexcept;
    GraphicsResource(
        const resource_type type,
        const uint32_t resource_handle,
        const uint64_t vk_handle,
        const uint64_t vk_view_handle,
        const uint64_t vk_sampler_handle) :
        Type{ type },
        EntityHandle{ resource_handle },
        VkHandle{ vk_handle },
        VkViewHandle{ vk_view_handle },
        VkSamplerHandle{ vk_sampler_handle }
    {}
    ~GraphicsResource() noexcept = default;
    GraphicsResource(const GraphicsResource&) noexcept = default;
    GraphicsResource& operator=(const GraphicsResource&) noexcept = default;
    GraphicsResource(GraphicsResource&&) noexcept = default;
    GraphicsResource& operator=(GraphicsResource&&) noexcept = default;

    static GraphicsResource Null() noexcept;
    
    constexpr bool operator==(const GraphicsResource& other) const noexcept
    {
        return Type == other.Type &&
               EntityHandle == other.EntityHandle &&
               VkHandle == other.VkHandle &&
               VkViewHandle == other.VkViewHandle &&
               VkSamplerHandle == other.VkSamplerHandle;
    }

    constexpr bool operator!=(const GraphicsResource& other) const noexcept
    {
        return !(*this == other);
    }

    operator bool() const noexcept;

    resource_type Type{ resource_type::Invalid };
    uint32_t EntityHandle{ 0u };
    uint64_t VkHandle{ 0u };
    uint64_t VkViewHandle{ 0u };
    uint64_t VkSamplerHandle{ 0u };
};

#endif //!DIAMOND_DOGS_RESOURCE_CONTEXT_TYPES_HPP
