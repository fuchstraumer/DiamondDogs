#include "ResourceTypes.hpp"
#include "ResourceFlags.hpp"
#include "entt/entt.hpp"

GraphicsResource GraphicsResource::Null() noexcept
{
    return GraphicsResource{ ResourceDomain::Invalid, ResourceType::Invalid, entt::null, 0u, 0u, 0u, 0u };
}

bool GraphicsResource::operator==(const GraphicsResource& other) const noexcept
{
    return Domain == other.Domain &&
           Type == other.Type &&
           EntityHandle == other.EntityHandle &&
           VkHandle == other.VkHandle &&
           VkViewHandle == other.VkViewHandle &&
           VkSamplerHandle == other.VkSamplerHandle &&
           DeviceAddress == other.DeviceAddress;
}

bool GraphicsResource::operator!=(const GraphicsResource& other) const noexcept
{
    return !(*this == other);
}

GraphicsResource::operator bool() const noexcept
{
    return Domain != ResourceDomain::Invalid && 
           Type != ResourceType::Invalid &&
           EntityHandle != entt::null;
}
