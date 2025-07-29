#include "ResourceTypes.hpp"
#include "entt/entt.hpp"

GraphicsResource::GraphicsResource() noexcept :
    Type{ resource_type::Invalid },
    EntityHandle{ entt::null },
    VkHandle{ 0u },
    VkViewHandle{ 0u },
    VkSamplerHandle{ 0u }
{}

GraphicsResource GraphicsResource::Null() noexcept
{
    return GraphicsResource{ resource_type::Invalid, entt::null, 0u, 0u, 0u };
}

GraphicsResource::operator bool() const noexcept
{
    return Type != resource_type::Invalid && EntityHandle != entt::null;
}

gpu_resource_data_t::gpu_resource_data_t(
    const void* data,
    const size_t data_size,
    const size_t data_alignment,
    const queue_family_flags dest_queue_family) noexcept :
    Data{ data },
    DataSize{ data_size },
    DataAlignment{ data_alignment },
    DestinationQueueFamily{ dest_queue_family }
{
}

gpu_image_resource_data_t::gpu_image_resource_data_t(
    const void* _data,
    const size_t data_size,
    const uint32_t width,
    const uint32_t height,
    const uint32_t array_layer,
    const uint32_t num_layers,
    const uint32_t mip_level,
    const queue_family_flags queue_flags) noexcept :
    Data{ _data },
    DataSize{ data_size },
    Width{ width },
    Height{ height },
    ArrayLayer{ array_layer },
    NumLayers{ num_layers },
    MipLevel{ mip_level },
    DestinationQueueFamily{ queue_flags }
{
}
