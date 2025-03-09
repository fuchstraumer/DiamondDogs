#pragma once
#ifndef RESOURCE_CONTEXT_RESOURCE_MESSAGE_REPLY_HPP
#define RESOURCE_CONTEXT_RESOURCE_MESSAGE_REPLY_HPP
#include "ResourceTypes.hpp"
#include <atomic>
#include <chrono>
#include <thread>
#include <vulkan/vulkan_core.h>
#include "threading/atomic128.hpp"

namespace vpr
{
    class Device;
}

// Status message reply is used for basic operations like destroy, fill, copy, etc - but also as base class for the vulkan resource reply,
// since even if we create the handles for that we still may be waiting on a potential transfer to the GPU to complete.
class MessageReply
{
public:
    // Listed in order of completion, with fail states at the end. 
    enum class Status : uint8_t
    {
        Invalid = 0,
        Pending = 1, // message popped out of queue and actively being processed
        Transferring, // resource created and transfer data attached, not safe to use
        Completed, // creation, transfer, or both completed depending on the message type. safe to use.
        Failed, // message processing failed, no resource created
        Timeout, // waiting for completion timed out, if status is "Transferring" then it's still just transferring
    };

    MessageReply() : status(Status::Invalid) {}
    virtual ~MessageReply() = default;
    
    MessageReply(const MessageReply&) = delete;
    MessageReply& operator=(const MessageReply&) = delete;
    MessageReply(MessageReply&& other) noexcept;
    MessageReply& operator=(MessageReply&& other) noexcept;
    
    virtual bool IsCompleted() const noexcept;
    Status GetStatus() const noexcept;
    // Waits for completion, including waiting for transfer to complete if applicable
    virtual Status WaitForCompletion(Status wait_for_status, uint64_t timeoutNs = std::numeric_limits<uint64_t>::max()) noexcept;
    
protected:
    friend class ResourceContextImpl;
    friend class TransferSystem;
    void SetStatus(Status status) noexcept;
    std::atomic<Status> status;
    static_assert(std::atomic<decltype(status)>::is_always_lock_free, "std::atomic<Status> is not lock-free on this platform/using this compiler");
};

// This is a separate class as sometimes we'll be doing a transfer or mutate operation, but not creating a new resource.
// GraphicsResourceReply just builds on this and the status reply. Virtual classes make me sad though :(
class ResourceTransferReply : public MessageReply
{
public:
    ResourceTransferReply(vpr::Device* _device);
    virtual ~ResourceTransferReply();

    uint64_t SemaphoreValue() const noexcept;
    uint64_t SemaphoreHandle() const noexcept;

    // Final override because GraphicsResourceReply may also need to wait for transfers, but doesn't change behavior
    Status WaitForCompletion(Status wait_for_status, uint64_t timeoutNs = std::numeric_limits<uint64_t>::max()) noexcept final;

protected:
    friend class ResourceContextImpl;
    friend class TransferSystem;

    uint64_t semaphoreHandle = 0u;
    const vpr::Device* device = nullptr;
};

// Vulkan resource (as of latest updates) has become a 256 bit struct, so we can use an atomic128 for the handle and view handle,
// and then smaller atomics for the other fields. Might be worth testing to see if we can skip the atomic loads and stores
// and just use relaxed loads and stores for the other fields after the entity handle is set.
class GraphicsResourceReply final : public ResourceTransferReply
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

    GraphicsResourceReply(resource_type _type);
    // this ctor used when we do have resources attached to the reply
    GraphicsResourceReply(resource_type _type, vpr::Device* _device);
    ~GraphicsResourceReply();

    GraphicsResourceReply(const GraphicsResourceReply&) = delete;
    GraphicsResourceReply& operator=(const GraphicsResourceReply&) = delete;
    
    // get resource using std::memory_order_acquire ordering, getting it's current potentially pending state
    GraphicsResource GetResource() const noexcept;

private:

    friend class ResourceContextImpl;
    friend class TransferSystemImpl;
    void SetGraphicsResource(
        const resource_type _type,
        const uint32_t entity_handle,
        const uint64_t vk_handle,
        const uint64_t vk_view_handle,
        const uint64_t vk_sampler_handle) noexcept;
    // used internally before a call that transfers to the transfer system, because the user shouldn't be using this resource
    // anyways (and if they do, and if they get garbage data, that's on them)
    void SetGraphicsResourceRelaxed(GraphicsResource resource) noexcept;
    
    std::atomic<VkResourceTypeAndEntityHandle> resourceTypeAndEntityHandle;
    std::atomic<uint64_t> vkSamplerHandle;
    static_assert(decltype(resourceTypeAndEntityHandle)::is_always_lock_free, "std::atomic<VkResourceTypeAndEntityHandle> is not lock-free on this platform/using this compiler");
    atomic128 vkHandleAndView;
};

// Used for the function that maps a buffer/image for copying
class PointerMessageReply final : public MessageReply
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