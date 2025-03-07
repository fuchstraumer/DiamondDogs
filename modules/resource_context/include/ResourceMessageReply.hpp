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
// and then smaller atomics for the other fields

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
        operator bool() const noexcept;
        
        uint32_t Type;
        uint32_t EntityHandle;
    };


public:
    VulkanResourceReply(resource_type _type);
    ~VulkanResourceReply() = default;
    
    // Non-copyable
    VulkanResourceReply(const VulkanResourceReply&) = delete;
    VulkanResourceReply& operator=(const VulkanResourceReply&) = delete;
    
    // Movable
    VulkanResourceReply(VulkanResourceReply&& other) noexcept;
    VulkanResourceReply& operator=(VulkanResourceReply&& other) noexcept;
    
    // Check if operation is completed
    bool IsCompleted() const;
    
    // Wait for the operation to complete
    bool WaitForCompletion(uint64_t timeoutNs = std::numeric_limits<uint64_t>::max()) const;
    
    // Get the resource
    VulkanResource GetResource() const;

private:
    // Allow ResourceContext to set completion
    friend class ResourceContextImpl;
    
    // Mark operation as completed
    void SetVulkanResource(const resource_type _type,
                           const uint32_t entity_handle,
                           const uint64_t vk_handle,
                           const uint64_t vk_view_handle,
                           const uint64_t vk_sampler_handle);
    
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
    
    bool IsCompleted() const;
    bool WaitForCompletion(uint64_t timeoutNs = std::numeric_limits<uint64_t>::max()) const;
    
private:
    // Allow ResourceContext to set completion
    friend class ResourceContextImpl;
    
    // Mark operation as completed
    void SetCompleted();
    
    std::atomic<bool> completed;
    static_assert(std::atomic<bool>::is_always_lock_free, "std::atomic<bool> is not lockfree on this platform/using this compiler");
};

// Used for the function that maps a buffer/image for copying
class PointerMessageReply
{

};


#endif //!RESOURCE_CONTEXT_RESOURCE_MESSAGE_REPLY_HPP