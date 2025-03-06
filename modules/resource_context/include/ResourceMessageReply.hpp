#pragma once
#ifndef RESOURCE_CONTEXT_RESOURCE_MESSAGE_REPLY_HPP
#define RESOURCE_CONTEXT_RESOURCE_MESSAGE_REPLY_HPP
#include "ResourceTypes.hpp"
#include <atomic>
#include <chrono>
#include <thread>
#include <vulkan/vulkan_core.h>

// TODO: need to have some way to make this accept boolean ReplyDataTypes. 
template<typename ReplyDataType>
class ResourceMessageReply
{
public:
    static_assert(std::is_pointer_v<ReplyDataType>, "ReplyDataType must be a pointer type");

    ResourceMessageReply() : replyData(nullptr) {}
    ~ResourceMessageReply() = default;
    
    // Non-copyable
    ResourceMessageReply(const ResourceMessageReply&) = delete;
    ResourceMessageReply& operator=(const ResourceMessageReply&) = delete;
    
    // Movable
    ResourceMessageReply(ResourceMessageReply&& other) noexcept
        : replyData(other.replyData.load(std::memory_order_relaxed))
    {
        other.replyData.store(nullptr, std::memory_order_relaxed);
    }
    
    ResourceMessageReply& operator=(ResourceMessageReply&& other) noexcept
    {
        if (this != &other)
        {
            replyData.store(other.replyData.load(std::memory_order_relaxed), std::memory_order_relaxed);
            other.replyData.store(nullptr, std::memory_order_relaxed);
        }
        return *this;
    }
    
    // Check if resource is ready
    bool IsReady() const
    {
        return replyData.load(std::memory_order_acquire) != nullptr;
    }
    
    ReplyDataType GetReplyData() const
    {
        return replyData.load(std::memory_order_acquire);
    }
    
    // Wait for the resource to be ready (optional timeout in nanoseconds)
    ReplyDataType WaitForResourceOperation(uint64_t timeoutNs = std::numeric_limits<uint64_t>::max()) const
    {
        std::chrono::high_resolution_clock::time_point startTime = std::chrono::high_resolution_clock::now();
        
        while (true)
        {
            ReplyDataType result = replyData.load(std::memory_order_acquire);
            if (result != nullptr)
            {
                return result;
            }
            
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
    }
    
private:
    // Allow ResourceContext to set the resource
    friend class ResourceContext;
    
    // Set the resource (called by worker thread when operation completes)
    void SetResource(VulkanResource* rsrc)
    {
        replyData.store(rsrc, std::memory_order_release);
    }
    
    std::atomic<ReplyDataType> replyData;
    static_assert(std::atomic<ReplyDataType>::is_always_lock_free, "ReplyDataType must be lock-free");
};

// Specialization for operations where we just need to know if it completed, e.g. DestroyResource or a copy/fill
template<>
class ResourceMessageReply<bool>
{
public:
    ResourceMessageReply() : completed(false) {}
    ~ResourceMessageReply() = default;
    
    // Non-copyable
    ResourceMessageReply(const ResourceMessageReply&) = delete;
    ResourceMessageReply& operator=(const ResourceMessageReply&) = delete;
    
    // Movable
    ResourceMessageReply(ResourceMessageReply&& other) noexcept
        : completed(other.completed.load(std::memory_order_relaxed))
    {
        other.completed.store(false, std::memory_order_relaxed);
    }
    
    ResourceMessageReply& operator=(ResourceMessageReply&& other) noexcept
    {
        if (this != &other)
        {
            completed.store(other.completed.load(std::memory_order_relaxed), std::memory_order_relaxed);
            other.completed.store(false, std::memory_order_relaxed);
        }
        return *this;
    }
    
    // Check if operation is completed
    bool IsCompleted() const
    {
        return completed.load(std::memory_order_acquire);
    }
    
    // Wait for the operation to complete
    bool WaitForCompletion(uint64_t timeoutNs = std::numeric_limits<uint64_t>::max()) const
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
    
private:
    // Allow ResourceContext to set completion
    friend class ResourceContext;
    
    // Mark operation as completed
    void SetCompleted()
    {
        completed.store(true, std::memory_order_release);
    }
    
    std::atomic<bool> completed;
    static_assert(std::atomic<bool>::is_always_lock_free, "std::atomic<bool> is not lockfree on this platform/using this compiler");
};


#endif //!RESOURCE_CONTEXT_RESOURCE_MESSAGE_REPLY_HPP