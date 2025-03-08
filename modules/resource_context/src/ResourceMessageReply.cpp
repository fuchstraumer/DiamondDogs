#include "ResourceMessageReply.hpp"
#include "entt/entt.hpp"

constexpr static cas_data128_t null_atomic128 = cas_data128_t{ 0u, 0u };
constexpr static uint64_t vk_null_handle_uint64 = reinterpret_cast<uint64_t>(VK_NULL_HANDLE);
constexpr static uint32_t entt_null_entity_uint32 = static_cast<uint32_t>(entt::null);
constexpr static VulkanResource null_vulkan_resource = VulkanResource{ resource_type::Invalid, entt_null_entity_uint32, vk_null_handle_uint64, vk_null_handle_uint64, vk_null_handle_uint64 };

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

VulkanResourceReply::VulkanResourceReply(resource_type _type) :
    resourceTypeAndEntityHandle{ VkResourceTypeAndEntityHandle(_type, entt::null) },
    vkHandleAndView{ null_atomic128 },
    vkSamplerHandle{ 0u }
{}

bool VulkanResourceReply::IsCompleted() const noexcept
{
    return static_cast<bool>(resourceTypeAndEntityHandle.load(std::memory_order_acquire));
}

VulkanResource VulkanResourceReply::WaitForCompletion(uint64_t timeoutNs) const noexcept
{
    std::chrono::high_resolution_clock::time_point startTime = std::chrono::high_resolution_clock::now();
    // entity handle should be key flag of validity, we publish this last after we publish the handles
    while (!resourceTypeAndEntityHandle.load(std::memory_order_acquire))
    {
        // Check timeout
        if (timeoutNs != std::numeric_limits<uint64_t>::max())
        {
            std::chrono::high_resolution_clock::time_point currentTime = std::chrono::high_resolution_clock::now();
            const uint64_t elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(currentTime - startTime).count();
            if (elapsedNs >= timeoutNs)
            {
                return null_vulkan_resource;
            }
        }

        // Yield to avoid busy waiting
        std::this_thread::yield();
    }

    return GetCompletedResource();
}

VulkanResource VulkanResourceReply::GetResource() const noexcept
{
    VkResourceTypeAndEntityHandle typeAndEntity = resourceTypeAndEntityHandle.load(std::memory_order_acquire);
    if (typeAndEntity)
    {
        cas_data128_t handleAndView = vkHandleAndView.load(std::memory_order_acquire);
        uint64_t samplerHandle = vkSamplerHandle.load(std::memory_order_acquire);
        return VulkanResource(static_cast<resource_type>(typeAndEntity.Type), typeAndEntity.EntityHandle, handleAndView.low, handleAndView.high, samplerHandle);
    }

    return null_vulkan_resource;
}

VulkanResource VulkanResourceReply::GetCompletedResource() const noexcept
{
    // use relaxed load because this is the checked function
    VkResourceTypeAndEntityHandle typeAndEntity = resourceTypeAndEntityHandle.load(std::memory_order_relaxed);
    cas_data128_t handleAndView = vkHandleAndView.load(std::memory_order_relaxed);
    uint64_t samplerHandle = vkSamplerHandle.load(std::memory_order_relaxed);
    return VulkanResource(static_cast<resource_type>(typeAndEntity.Type), typeAndEntity.EntityHandle, handleAndView.low, handleAndView.high, samplerHandle);
}

void VulkanResourceReply::SetVulkanResource(const resource_type _type, const uint32_t entity_handle, const uint64_t vk_handle, const uint64_t vk_view_handle, const uint64_t vk_sampler_handle)
{
    // Set the entity handle last, since we check that to see if the whole thing is valid/completed
    vkSamplerHandle.store(vk_sampler_handle, std::memory_order_release);
    vkHandleAndView.store(cas_data128_t{ vk_handle, vk_view_handle }, std::memory_order_release);
    resourceTypeAndEntityHandle.store(VkResourceTypeAndEntityHandle(_type, entity_handle), std::memory_order_release);
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

bool BooleanMessageReply::IsCompleted() const noexcept
{
    return completed.load(std::memory_order_acquire);
}

bool BooleanMessageReply::WaitForCompletion(uint64_t timeoutNs) const noexcept
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

void BooleanMessageReply::SetCompleted() noexcept
{
    completed.store(true, std::memory_order_release);
}

void* PointerMessageReply::GetPointer() const noexcept
{
    return data.load(std::memory_order_acquire);
}

void* PointerMessageReply::WaitForCompletion(uint64_t timeoutNs /*= std::numeric_limits<uint64_t>::max()*/) const noexcept
{
    std::chrono::high_resolution_clock::time_point startTime = std::chrono::high_resolution_clock::now();

    while (!data.load(std::memory_order_acquire))
    {
        // Check timeout
        if (timeoutNs != std::numeric_limits<uint64_t>::max())
        {
            std::chrono::high_resolution_clock::time_point currentTime = std::chrono::high_resolution_clock::now();
            const uint64_t elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(currentTime - startTime).count();
            if (elapsedNs >= timeoutNs)
            {
                return nullptr;
            }
        }

        // Yield to avoid busy waiting
        std::this_thread::yield();
    }

    // can use relaxed load at this point, since earlier checking load was acquire ordered
    return data.load(std::memory_order_relaxed);
}

void PointerMessageReply::SetPointer(void* ptr) noexcept
{
    data.store(ptr, std::memory_order_release);
}
