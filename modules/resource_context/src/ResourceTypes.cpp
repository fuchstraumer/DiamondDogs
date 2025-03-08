#include "ResourceTypes.hpp"
#include "entt/entt.hpp"

VulkanResource VulkanResource::Null() noexcept
{
    return VulkanResource{ resource_type::Invalid, entt::null, 0u, 0u, 0u };
}

