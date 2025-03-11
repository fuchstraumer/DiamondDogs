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

