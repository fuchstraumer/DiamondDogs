#include "ResourceTypes.hpp"
#include "entt/entt.hpp"

GraphicsResource GraphicsResource::Null() noexcept
{
    return GraphicsResource{ resource_type::Invalid, entt::null, 0u, 0u, 0u };
}

