#include "ResourceMessageReply.hpp"
#include "entt/entt.hpp"
#include "LogicalDevice.hpp"
#include "vkAssert.hpp"

constexpr static cas_data128_t null_atomic128 = cas_data128_t{ 0u, 0u };
constexpr static uint64_t vk_null_handle_uint64 = reinterpret_cast<uint64_t>(VK_NULL_HANDLE);
constexpr static uint32_t entt_null_entity_uint32 = static_cast<uint32_t>(entt::null);
constexpr static GraphicsResource null_graphics_resource = GraphicsResource{ resource_type::Invalid, entt_null_entity_uint32, vk_null_handle_uint64, vk_null_handle_uint64, vk_null_handle_uint64 };

MessageReply::MessageReply(MessageReply&& other) noexcept
    : status(other.status.load(std::memory_order_relaxed))
{
    other.status.store(Status::Invalid, std::memory_order_relaxed);
}

MessageReply& MessageReply::operator=(MessageReply&& other) noexcept
{
    if (this != &other)
    {
        status.store(other.status.load(std::memory_order_relaxed), std::memory_order_relaxed);
        other.status.store(Status::Invalid, std::memory_order_relaxed);
    }
    return *this;
}

bool MessageReply::IsCompleted() const noexcept
{
    return status.load(std::memory_order_acquire) != Status::Invalid;
}

MessageReply::Status MessageReply::WaitForCompletion(Status wait_for_status, uint64_t timeoutNs) noexcept
{
    std::chrono::high_resolution_clock::time_point startTime = std::chrono::high_resolution_clock::now();
    // less than wait_for_status because statuses are arranged in order of completion
    while (status.load(std::memory_order_acquire) < wait_for_status)
    {
        // Check timeout
        if (timeoutNs != std::numeric_limits<uint64_t>::max())
        {
            std::chrono::high_resolution_clock::time_point currentTime = std::chrono::high_resolution_clock::now();
            const uint64_t elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(currentTime - startTime).count();
            if (elapsedNs >= timeoutNs)
            {
                return Status::Timeout;
            }
        }

        // Yield to avoid busy waiting
        std::this_thread::yield();
    }

    return status.load(std::memory_order_relaxed);
}

void MessageReply::SetStatus(Status _status) noexcept
{
    status.store(_status, std::memory_order_release);
}

ResourceTransferReply::ResourceTransferReply(vpr::Device* _device) : device{ _device }, semaphoreHandle{ 0u }
{
    VkSemaphoreTypeCreateInfo semaphore_type_info
    {
        VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
        nullptr,
        VK_SEMAPHORE_TYPE_TIMELINE,
        0u
    };
    VkSemaphoreCreateInfo semaphore_info
    {
        VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        &semaphore_type_info,
        0u
    };
    VkSemaphore semaphore = VK_NULL_HANDLE;
    VkResult result = vkCreateSemaphore(device->vkHandle(), &semaphore_info, nullptr, &semaphore);
    VkAssert(result);
    semaphoreHandle = reinterpret_cast<uint64_t>(semaphore);
}

ResourceTransferReply::~ResourceTransferReply()
{
    if (semaphoreHandle != 0u)
    {
        vkDestroySemaphore(device->vkHandle(), reinterpret_cast<VkSemaphore>(semaphoreHandle), nullptr);
    }
}

uint64_t ResourceTransferReply::SemaphoreHandle() const noexcept
{
    return semaphoreHandle;
}

uint64_t ResourceTransferReply::SemaphoreValue() const noexcept
{
    VkSemaphore semaphore = reinterpret_cast<VkSemaphore>(semaphoreHandle);
    uint64_t semaphoreValue = 0u;
    VkResult result = vkGetSemaphoreCounterValue(device->vkHandle(), semaphore, &semaphoreValue);
    VkAssert(result);
    return semaphoreValue;
}

MessageReply::Status ResourceTransferReply::WaitForCompletion(Status wait_for_status, uint64_t timeoutNs) noexcept
{
    // this is not actually a transfer reply, so waiting for completion is just using base class implementation
    if (device == nullptr && semaphoreHandle == 0u)
    {
        return MessageReply::WaitForCompletion(wait_for_status, timeoutNs);
    }
    else
    {
        // In short, the reason we go through this song and dance is so that the transfer system
        // does not have to block to submit it's work. It just submits transfer commands, and then eventually
        // the completed transfer will update the semaphore value indicating that the transfer is complete.
        VkSemaphore semaphore = reinterpret_cast<VkSemaphore>(semaphoreHandle);
        const uint64_t semaphoreWaitValue = 2;
        VkSemaphoreWaitInfo wait_info
        {
            VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
            nullptr,
            0u,
            1u,
            &semaphore,
            &semaphoreWaitValue
        };
        VkResult result = vkWaitSemaphores(device->vkHandle(), &wait_info, timeoutNs);
        if (result == VK_TIMEOUT)
        {
            return Status::Timeout;
        }
        else if (result != VK_SUCCESS)
        {
            return Status::Failed;
        }
        else
        {
            SetStatus(Status::Completed);
            return Status::Completed;
        }
    }

}

GraphicsResourceReply::VkResourceTypeAndEntityHandle::VkResourceTypeAndEntityHandle() noexcept :
    Type{ (uint32_t)resource_type::Invalid }, EntityHandle{ entt::null }
{
}

GraphicsResourceReply::VkResourceTypeAndEntityHandle::VkResourceTypeAndEntityHandle(const resource_type type, const uint32_t entity_handle) noexcept :
    Type{ (uint32_t)type }, EntityHandle{ entity_handle }
{
}

bool GraphicsResourceReply::VkResourceTypeAndEntityHandle::operator==(const VkResourceTypeAndEntityHandle& other) const noexcept
{
    return Type == other.Type && EntityHandle == other.EntityHandle;
}

bool GraphicsResourceReply::VkResourceTypeAndEntityHandle::operator!=(const VkResourceTypeAndEntityHandle& other) const noexcept
{
    return !(*this == other);
}

GraphicsResourceReply::VkResourceTypeAndEntityHandle::operator bool() const noexcept
{
    return (Type != (uint32_t)resource_type::Invalid) && (EntityHandle != entt::null);
}

GraphicsResourceReply::GraphicsResourceReply(resource_type _type, vpr::Device* _device) :
    resourceTypeAndEntityHandle{ VkResourceTypeAndEntityHandle(_type, entt::null) },
    vkHandleAndView{ null_atomic128 },
    vkSamplerHandle{ 0u },
    ResourceTransferReply(_device)
{
}

GraphicsResourceReply::~GraphicsResourceReply()
{
}

GraphicsResource GraphicsResourceReply::GetResource() const noexcept
{
    VkResourceTypeAndEntityHandle typeAndEntity = resourceTypeAndEntityHandle.load(std::memory_order_acquire);
    if (typeAndEntity)
    {
        cas_data128_t handleAndView = vkHandleAndView.load(std::memory_order_acquire);
        uint64_t samplerHandle = vkSamplerHandle.load(std::memory_order_acquire);
        return GraphicsResource(static_cast<resource_type>(typeAndEntity.Type), typeAndEntity.EntityHandle, handleAndView.low, handleAndView.high, samplerHandle);
    }

    return null_graphics_resource;
}

void GraphicsResourceReply::SetGraphicsResource(
    const resource_type _type,
    const uint32_t entity_handle,
    const uint64_t vk_handle,
    const uint64_t vk_view_handle,
    const uint64_t vk_sampler_handle)
{
    // Set the entity handle last, since we check that to see if the whole thing is valid/completed
    // if checks just to save atomic stores if not needed
    if (vk_sampler_handle != 0u)
    {
        vkSamplerHandle.store(vk_sampler_handle, std::memory_order_release);
    }

    if (vk_handle != 0u || vk_view_handle != 0u)
    {
        vkHandleAndView.store(cas_data128_t{ vk_handle, vk_view_handle }, std::memory_order_release);
    }

    resourceTypeAndEntityHandle.store(VkResourceTypeAndEntityHandle(_type, entity_handle), std::memory_order_release);
    SetStatus(Status::Completed);
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
    SetStatus(Status::Completed);
}
