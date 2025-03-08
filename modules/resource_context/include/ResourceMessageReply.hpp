#pragma once
#ifndef RESOURCE_CONTEXT_RESOURCE_MESSAGE_REPLY_HPP
#define RESOURCE_CONTEXT_RESOURCE_MESSAGE_REPLY_HPP
#include "ResourceTypes.hpp"
#include <atomic>
#include <chrono>
#include <thread>
#include <vulkan/vulkan_core.h>
#include "threading/atomic128.hpp"

// Status message reply is used for basic operations like destroy, fill, copy, etc - but also as base class for the vulkan resource reply,
// since even if we create the handles for that we still may be waiting on a potential transfer to the GPU to complete.
class StatusMessageReply
{
public:
    enum class Status : uint8_t
    {
        Invalid = 0,
        Pending = 1,
        Transferring,
        Completed,
        Failed,
        Timeout
    };

    StatusMessageReply() : status(Status::Invalid) {}
    virtual ~StatusMessageReply() = default;
    
    StatusMessageReply(const StatusMessageReply&) = delete;
    StatusMessageReply& operator=(const StatusMessageReply&) = delete;
    StatusMessageReply(StatusMessageReply&& other) noexcept;
    StatusMessageReply& operator=(StatusMessageReply&& other) noexcept;
    
    virtual bool IsCompleted() const noexcept;
    Status GetStatus() const noexcept;
    // Waits for completion, including waiting for transfer to complete if applicable
    Status WaitForCompletion(uint64_t timeoutNs = std::numeric_limits<uint64_t>::max()) const noexcept;
    
private:
    friend class ResourceContextImpl;
    void SetStatus(Status status) noexcept;
    
    std::atomic<Status> status;
    static_assert(std::atomic<decltype(status)>::is_always_lock_free, "std::atomic<Status> is not lock-free on this platform/using this compiler");
};

// Vulkan resource (as of latest updates) has become a 256 bit struct, so we can use an atomic128 for the handle and view handle,
// and then smaller atomics for the other fields. Might be worth testing to see if we can skip the atomic loads and stores
// and just use relaxed loads and stores for the other fields after the entity handle is set.
class GraphicsResourceReply final : public StatusMessageReply
{
    
    struct VkResourceTypeAndEntityHandle
    {
        // in .cpp because I'm not exposing entt here just to get null entity
        VkResourceTypeAndEntityHandle() noexcept;
        VkResourceTypeAndEntityHandle(const resource_type type, const uint32_t entity_handle) noexcept;
        VkResourceTypeAndEntityHandle(const VkResourceTypeAndEntityHandle& other) noexcept = default;
        VkResourceTypeAndEntityHandle& operator=(const VkResourceTypeAndEntityHandle& other) noexcept = default;
        VkResourceTypeAndEntityHandle(VkResourceTypeAndEntityHandle&& other) noexcept = default;
        VkResourceTypeAndEntityHandle& operator=(VkResourceTypeAndEntityHandle&& other) noexcept = default;
        ~VkResourceTypeAndEntityHandle() noexcept = default;
        bool operator==(const VkResourceTypeAndEntityHandle& other) const noexcept;
        bool operator!=(const VkResourceTypeAndEntityHandle& other) const noexcept;
        // resource is valid if both type AND entity handle are valid, and entity handle is set last
        operator bool() const noexcept;
        
        uint32_t Type;
        uint32_t EntityHandle;
    };


public:

    enum class WaitFor : uint8_t
    {
        Invalid = 0,
        // Only wait for API creation of VkBuffer/VkImage/VkSampler handles to complete, backing data may be pending
        Creation = 1,
        // Wait for creation *AND* transfer of data to the GPU to complete
        Transfer
    };

    GraphicsResourceReply(resource_type _type);
    ~GraphicsResourceReply() = default;

    GraphicsResourceReply(const GraphicsResourceReply&) = delete;
    GraphicsResourceReply& operator=(const GraphicsResourceReply&) = delete;

    bool IsCompleted() const noexcept final;
    Status WaitForCompletion(WaitFor wait_for, uint64_t timeoutNs = std::numeric_limits<uint64_t>::max()) const noexcept;
    
    // get resource using std::memory_order_acquire ordering, getting it's current potentially pending state
    VulkanResource GetResource() const noexcept;

private:

    friend class ResourceContextImpl;
    friend class TransferSystemImpl;
    void SetVulkanResource(const resource_type _type,
                           const uint32_t entity_handle,
                           const uint64_t vk_handle,
                           const uint64_t vk_view_handle,
                           const uint64_t vk_sampler_handle) noexcept;
    
    std::atomic<VkResourceTypeAndEntityHandle> resourceTypeAndEntityHandle;
    static_assert(decltype(resourceTypeAndEntityHandle)::is_always_lock_free, "std::atomic<VkResourceTypeAndEntityHandle> is not lock-free on this platform/using this compiler");
    atomic128 vkHandleAndView;
    std::atomic<uint64_t> vkSamplerHandle;

};

// Used for the function that maps a buffer/image for copying
class PointerMessageReply final : public StatusMessageReply
{
public:
    PointerMessageReply() : data(nullptr) {}
    ~PointerMessageReply() = default;
    
    PointerMessageReply(const PointerMessageReply&) = delete;
    PointerMessageReply& operator=(const PointerMessageReply&) = delete;
    PointerMessageReply(PointerMessageReply&& other) noexcept;
    PointerMessageReply& operator=(PointerMessageReply&& other) noexcept;

    bool IsCompleted() const noexcept final;
    void* GetPointer() const noexcept;
    void* WaitForCompletion(uint64_t timeoutNs = std::numeric_limits<uint64_t>::max()) const noexcept;
    
private:
    friend class ResourceContextImpl;
    void SetPointer(void* ptr) noexcept;

    std::atomic<void*> data;
};


#endif //!RESOURCE_CONTEXT_RESOURCE_MESSAGE_REPLY_HPP