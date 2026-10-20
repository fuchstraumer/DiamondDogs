#include "ResourceMessageReply.hpp"
#include "ResourceFlags.hpp"
#include "ResourceTypes.hpp"
#include "entt/entt.hpp"
#include "Device.hpp"
#include "ResourceContext.hpp"
#include "threading/atomic128.hpp"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <limits>
#include <vulkan/vulkan_core.h>
#include <chrono>
#include <thread>
#include <utility>

const static CasData128 k_NullAtomic128 = CasData128{ 0u, 0u };
constexpr static uint64_t k_VkNullHandleUi64 = 0u;
constexpr static uint32_t k_EnttNullEntityUi32 = static_cast<uint32_t>(entt::null);
const static GraphicsResource k_NullGraphicsRsrc = GraphicsResource{ ResourceDomain::Invalid,
                                                                     ResourceType::Invalid,
                                                                     k_EnttNullEntityUi32,
                                                                     k_VkNullHandleUi64,
                                                                     k_VkNullHandleUi64,
                                                                     k_VkNullHandleUi64 };

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
    const Status current = status.load(std::memory_order_acquire);
    return current == Status::Completed || current == Status::Failed;
}

MessageReply::Status MessageReply::GetStatus() const noexcept
{
    return status.load(std::memory_order_acquire);
}

MessageReply::Status MessageReply::WaitForCompletion(uint64_t timeoutNs) noexcept
{
    std::chrono::high_resolution_clock::time_point startTime = std::chrono::high_resolution_clock::now();
    while (!IsCompleted())
    {
        // Check timeout
        if (timeoutNs != std::numeric_limits<uint64_t>::max())
        {
            std::chrono::high_resolution_clock::time_point currentTime = std::chrono::high_resolution_clock::now();
            const int64_t elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(currentTime - startTime).count();
            if (std::cmp_greater_equal(elapsedNs , timeoutNs))
            {
                return Status::Timeout;
            }
        }

        // Yield to avoid busy waiting
        std::this_thread::yield();
    }

    return status.load(std::memory_order_relaxed);
}

void MessageReply::setStatus(Status _status) noexcept
{
    status.store(_status, std::memory_order_release);
}

ResourceTransferReply::ResourceTransferReply() : device{ nullptr }
{}

ResourceTransferReply::ResourceTransferReply(const rhi::Device* _device) : device{ _device }
{
    VkSemaphoreTypeCreateInfo semaphoreTypeInfo
    {
        VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
        nullptr,
        VK_SEMAPHORE_TYPE_TIMELINE,
        0u
    };
    VkSemaphoreCreateInfo semaphoreInfo
    {
        VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        &semaphoreTypeInfo,
        0u
    };
    VkSemaphore semaphore = VK_NULL_HANDLE;
    VkResult result = vkCreateSemaphore(device->Handle().As<VkDevice>(), &semaphoreInfo, nullptr, &semaphore);
    assert(result == VK_SUCCESS);
    semaphoreHandle = reinterpret_cast<uint64_t>(semaphore);
}

ResourceTransferReply::~ResourceTransferReply()
{
    if (semaphoreHandle != 0u)
    {
        vkDestroySemaphore(device->Handle().As<VkDevice>(),
                           reinterpret_cast<VkSemaphore>(semaphoreHandle),
                           nullptr);
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
    VkResult result = vkGetSemaphoreCounterValue(device->Handle().As<VkDevice>(), semaphore, &semaphoreValue);
    assert(result == VK_SUCCESS);
    return semaphoreValue;
}

MessageReply::Status ResourceTransferReply::WaitForCompletion(uint64_t timeout_ns) noexcept
{
    // this is not actually a transfer reply, so waiting for completion is just using base class implementation
    if (device == nullptr && semaphoreHandle == 0u)
    {
        return MessageReply::WaitForCompletion(timeout_ns);
    }
    
    // fixed: we always check the CPU-side status and wait on that, since many resources don't actually need
    // to use the transfer system. if the transfer system sets the status, then we will wait on the GPU semaphore as needed.
    const std::chrono::high_resolution_clock::time_point startTime = std::chrono::high_resolution_clock::now();
    while (status.load(std::memory_order_acquire) == Status::Invalid || status.load(std::memory_order_acquire) == Status::Pending)
    {
        if (timeout_ns != std::numeric_limits<uint64_t>::max())
        {
            const int64_t elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - startTime).count();
            if (std::cmp_greater_equal(elapsedNs , timeout_ns))
            {
                return Status::Timeout;
            }
        }
        std::this_thread::yield();
    }

    if (status.load(std::memory_order_acquire) != Status::Transferring)
    {
        return status.load(std::memory_order_acquire);
    }
    else
    {
        // In short, the reason we go through this song and dance is so that the transfer system
        // does not have to block to submit it's work. It just submits transfer commands, and then eventually
        // the completed transfer will update the semaphore value indicating that the transfer is complete.
        VkSemaphore semaphore = reinterpret_cast<VkSemaphore>(semaphoreHandle);
        const uint64_t semaphoreWaitValue = k_TransferCompleteSemaphoreValue;
        VkSemaphoreWaitInfo waitInfo
        {
            VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
            nullptr,
            0u,
            1u,
            &semaphore,
            &semaphoreWaitValue
        };

        VkResult result = vkWaitSemaphores(device->Handle().As<VkDevice>(),
                                           &waitInfo,
                                           timeout_ns);

        if (result == VK_TIMEOUT) [[unlikely]]
        {
            return Status::Timeout;
        }
        else if (result != VK_SUCCESS) [[unlikely]]
        {
            return Status::Failed;
        }
        else
        {
            setStatus(Status::Completed);
            return Status::Completed;
        }
    }

}

GraphicsResourceReply::RhiResourceTypeAndEntityHandle::RhiResourceTypeAndEntityHandle() noexcept :
    Domain{ static_cast<uint16_t>(ResourceDomain::Invalid) },
    Type{ static_cast<uint16_t>(ResourceType::Invalid) },
    EntityHandle{ entt::null }
{
}

GraphicsResourceReply::RhiResourceTypeAndEntityHandle::RhiResourceTypeAndEntityHandle(
    const ResourceDomain domain,
    const ResourceType type,
    const uint32_t entity_handle) noexcept :
    Domain{ static_cast<uint16_t>(domain) },
    Type{ static_cast<uint16_t>(type) },
    EntityHandle{ entity_handle }
{}

bool GraphicsResourceReply::RhiResourceTypeAndEntityHandle::operator==(const RhiResourceTypeAndEntityHandle& other) const noexcept
{
    return Domain == other.Domain && Type == other.Type && EntityHandle == other.EntityHandle;
}

bool GraphicsResourceReply::RhiResourceTypeAndEntityHandle::operator!=(const RhiResourceTypeAndEntityHandle& other) const noexcept
{
    return !(*this == other);
}

GraphicsResourceReply::RhiResourceTypeAndEntityHandle::operator bool() const noexcept
{
    return (Type != static_cast<uint16_t>(ResourceType::Invalid)) &&
           (Domain != static_cast<uint16_t>(ResourceDomain::Invalid)) &&
           (EntityHandle != entt::null);
}

GraphicsResourceReply::GraphicsResourceReply(ResourceDomain _domain, ResourceType _type) :
    resourceTypeAndEntityHandle{ RhiResourceTypeAndEntityHandle(_domain, _type, entt::null) },
    vkSamplerHandle{ 0u },
    vkHandleAndView{ k_NullAtomic128 }
{}

GraphicsResourceReply::GraphicsResourceReply(ResourceDomain _domain, ResourceType _type, const rhi::Device* _device) :
    ResourceTransferReply(_device),
    resourceTypeAndEntityHandle{ RhiResourceTypeAndEntityHandle(_domain, _type, entt::null) },
    vkSamplerHandle{ 0u },
    vkHandleAndView{ k_NullAtomic128 }
{
}

GraphicsResourceReply::~GraphicsResourceReply()
{
}

GraphicsResource GraphicsResourceReply::GetResource() const noexcept
{
    RhiResourceTypeAndEntityHandle typeAndEntity = resourceTypeAndEntityHandle.load(std::memory_order_acquire);
    if (typeAndEntity)
    {
        CasData128 handleAndView = vkHandleAndView.load(std::memory_order_acquire);
        uint64_t samplerHandle = vkSamplerHandle.load(std::memory_order_acquire);
        return GraphicsResource(static_cast<ResourceDomain>(typeAndEntity.Domain),
                                static_cast<ResourceType>(typeAndEntity.Type),
                                typeAndEntity.EntityHandle,
                                handleAndView.low,
                                handleAndView.high,
                                samplerHandle);
    }

    return k_NullGraphicsRsrc;
}

void GraphicsResourceReply::setGraphicsResource(
    const ResourceDomain _domain,
    const ResourceType _type,
    const uint32_t entity_handle,
    const uint64_t vk_handle,
    const uint64_t vk_view_handle,
    const uint64_t vk_sampler_handle) noexcept
{
    // Set the entity handle last, since we check that to see if the whole thing is valid/completed
    // if checks just to save atomic stores if not needed
    if (vk_sampler_handle != 0u)
    {
        vkSamplerHandle.store(vk_sampler_handle, std::memory_order_release);
    }

    if (vk_handle != 0u || vk_view_handle != 0u)
    {
        vkHandleAndView.store(CasData128{ vk_handle, vk_view_handle }, std::memory_order_release);
    }

    resourceTypeAndEntityHandle.store(RhiResourceTypeAndEntityHandle(_domain, _type, entity_handle), std::memory_order_release);
    setStatus(Status::Completed);
}

void GraphicsResourceReply::setGraphicsResourceRelaxed(const GraphicsResource& resource) noexcept
{
    // doesn't set status as this usually means there are dependent operations following this, which is what the user is actually waiting
    // on and why we can use relaxed stores
    vkHandleAndView.store(CasData128{ resource.VkHandle, resource.VkViewHandle }, std::memory_order_relaxed);
    vkSamplerHandle.store(resource.VkSamplerHandle, std::memory_order_relaxed);
    resourceTypeAndEntityHandle.store(RhiResourceTypeAndEntityHandle(resource.Domain, resource.Type, resource.EntityHandle), std::memory_order_relaxed);
}


PointerMessageReply::PointerMessageReply(PointerMessageReply&& other) noexcept : data{ other.data.load(std::memory_order_relaxed) }
{
    other.data.store(nullptr, std::memory_order_relaxed);
}

PointerMessageReply& PointerMessageReply::operator=(PointerMessageReply&& other) noexcept
{
    if (this != &other)
    {
        data.store(other.data.load(std::memory_order_relaxed), std::memory_order_relaxed);
        other.data.store(nullptr, std::memory_order_relaxed);
    }
    return *this;
}

void* PointerMessageReply::GetPointer() const noexcept
{
    return data.load(std::memory_order_acquire);
}

void PointerMessageReply::setPointer(void* ptr) noexcept
{
    data.store(ptr, std::memory_order_release);
    setStatus(Status::Completed);
}
