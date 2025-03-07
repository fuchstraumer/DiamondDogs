#include "ResourceMessageReply.hpp"
#include "entt/entt.hpp"

constexpr static cas_data128_t null_atomic128 = cas_data128_t{ 0u, 0u };

VulkanResourceReply::VkResourceTypeAndEntityHandle::VkResourceTypeAndEntityHandle() noexcept :
    Type{ (uint32_t)resource_type::Invalid }, EntityHandle{ entt::null } {}

VulkanResourceReply::VkResourceTypeAndEntityHandle::VkResourceTypeAndEntityHandle(const resource_type type, const uint32_t entity_handle) noexcept :
    Type{ (uint32_t)type }, EntityHandle{ entity_handle } {}

bool VulkanResourceReply::VkResourceTypeAndEntityHandle::operator==(const VkResourceTypeAndEntityHandle& other) const noexcept
{
    return Type == other.Type && EntityHandle == other.EntityHandle;
}

bool VulkanResourceReply::VkResourceTypeAndEntityHandle::operator!=(const VkResourceTypeAndEntityHandle& other) const noexcept
{
    return !(*this == other);
}

VulkanResourceReply::VkResourceTypeAndEntityHandle::operator bool() const noexcept
{
    return (Type != (uint32_t)resource_type::Invalid) && (EntityHandle != entt::null);
}

VulkanResourceReply::VulkanResourceReply(resource_type _type) : resourceTypeAndEntityHandle{ }, vkHandleAndView{ null_atomic128 }, vkSamplerHandle{ 0u } {}

VulkanResourceReply::VulkanResourceReply(VulkanResourceReply&& other) noexcept :
    resourceTypeAndEntityHandle{ other.resourceTypeAndEntityHandle },
    vkHandleAndView{ std::move(other.vkHandleAndView) },
    vkSamplerHandle{ other.vkSamplerHandle }
{
    other.resourceTypeAndEntityHandle = {};
    other.vkHandleAndView = null_atomic128;
    other.vkSamplerHandle = 0u;
}

bool VulkanResourceReply::IsCompleted() const
{

}

bool VulkanResourceReply::WaitForCompletion(uint64_t timeoutNs) const
{
    return false;
}

VulkanResource VulkanResourceReply::GetResource() const
{
    return VulkanResource();
}

VulkanResourceReply& VulkanResourceReply::operator=(VulkanResourceReply&& other) noexcept
{

}

void VulkanResourceReply::SetVulkanResource(const resource_type _type, const uint32_t entity_handle, const uint64_t vk_handle, const uint64_t vk_view_handle, const uint64_t vk_sampler_handle)
{

}

BooleanMessageReply::BooleanMessageReply(BooleanMessageReply&& other) noexcept
    : completed(other.completed.load(std::memory_order_relaxed))
{
    other.completed.store(false, std::memory_order_relaxed);
}

BooleanMessageReply& BooleanMessageReply::operator=(BooleanMessageReply&& other) noexcept
{
    if (this != &other)
    {
        completed.store(other.completed.load(std::memory_order_relaxed), std::memory_order_relaxed);
        other.completed.store(false, std::memory_order_relaxed);
    }
    return *this;
}

bool BooleanMessageReply::IsCompleted() const
{
    return completed.load(std::memory_order_acquire);
}

bool BooleanMessageReply::WaitForCompletion(uint64_t timeoutNs) const
{
    std::chrono::high_resolution_clock::time_point startTime = std::chrono::high_resolution_clock::now();

    while (!completed.load(std::memory_order_acquire))
    {
        // Check timeout
        if (timeoutNs != std::numeric_limits<uint64_t>::max())
        {
            std::chrono::high_resolution_clock::time_point currentTime = std::chrono::high_resolution_clock::now();
            const uint64_t elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(currentTime - startTime).count();
            if (elapsedNs >= timeoutNs)
            {
                return false;
            }
        }

        // Yield to avoid busy waiting
        std::this_thread::yield();
    }

    return true;
}

void BooleanMessageReply::SetCompleted()
{
    completed.store(true, std::memory_order_release);
}
