#pragma once
#ifndef RESOURCE_CONTEXT_RESOURCE_MESSAGE_REPLY_HPP
#define RESOURCE_CONTEXT_RESOURCE_MESSAGE_REPLY_HPP
#include "ResourceTypes.hpp"
#include <atomic>
#include <chrono>
#include <thread>
#include <vulkan/vulkan_core.h>
#include "threading/atomic128.hpp"

// Vulkan resource (as of latest updates) has become a 256 bit struct, so we can use an atomic128 for the handle and view handle,
// and then smaller atomics for the other fields. Might be worth testing to see if we can skip the atomic loads and stores
// and just use relaxed loads and stores for the other fields after the entity handle is set.
class VulkanResourceReply
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
    VulkanResourceReply(resource_type _type);
    ~VulkanResourceReply() = default;

    VulkanResourceReply(const VulkanResourceReply&) = delete;
    VulkanResourceReply& operator=(const VulkanResourceReply&) = delete;

    bool IsCompleted() const noexcept;
    VulkanResource WaitForCompletion(uint64_t timeoutNs = std::numeric_limits<uint64_t>::max()) const noexcept;
    
    // get resource using std::memory_order_acquire ordering, getting it's current potentially pending state
    VulkanResource GetResource() const noexcept;
    // get resource using relaxed loads, mostly intended for internal usage after validity checks
    VulkanResource GetCompletedResource() const noexcept;

private:

    friend class ResourceContextImpl;
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

// Specialization for operations where we just need to know if it completed, e.g. DestroyResource or a copy/fill
class BooleanMessageReply
{
public:
    BooleanMessageReply() : completed(false) {}
    ~BooleanMessageReply() = default;
    
    BooleanMessageReply(const BooleanMessageReply&) = delete;
    BooleanMessageReply& operator=(const BooleanMessageReply&) = delete;
    BooleanMessageReply(BooleanMessageReply&& other) noexcept;
    BooleanMessageReply& operator=(BooleanMessageReply&& other) noexcept;
    
    bool IsCompleted() const noexcept;
    bool WaitForCompletion(uint64_t timeoutNs = std::numeric_limits<uint64_t>::max()) const noexcept;
    
private:
    friend class ResourceContextImpl;
    void SetCompleted() noexcept;
    
    std::atomic<bool> completed;
    static_assert(std::atomic<bool>::is_always_lock_free, "std::atomic<bool> is not lockfree on this platform/using this compiler");
};

// Used for the function that maps a buffer/image for copying
class PointerMessageReply
{
public:
    PointerMessageReply() : data(nullptr) {}
    ~PointerMessageReply() = default;
    
    PointerMessageReply(const PointerMessageReply&) = delete;
    PointerMessageReply& operator=(const PointerMessageReply&) = delete;
    PointerMessageReply(PointerMessageReply&& other) noexcept;
    PointerMessageReply& operator=(PointerMessageReply&& other) noexcept;

    bool IsCompleted() const noexcept;
    void* GetPointer() const noexcept;
    void* WaitForCompletion(uint64_t timeoutNs = std::numeric_limits<uint64_t>::max()) const noexcept;
    
private:
    friend class ResourceContextImpl;
    void SetPointer(void* ptr) noexcept;

    std::atomic<void*> data;
};


#endif //!RESOURCE_CONTEXT_RESOURCE_MESSAGE_REPLY_HPP