#pragma once
#ifndef RESOURCE_CONTEXT_RESOURCE_MESSAGE_REPLY_HPP
#define RESOURCE_CONTEXT_RESOURCE_MESSAGE_REPLY_HPP
#include "ResourceTypes.hpp"
#include <atomic>
#include <chrono>
#include <thread>
#include <vulkan/vulkan_core.h>
#include "threading/atomic128.hpp"

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
    friend class ResourceContextImpl;
    
    // Set the resource (called by worker thread when operation completes)
    void SetResource(VulkanResource* rsrc)
    {
        replyData.store(rsrc, std::memory_order_release);
    }
    
    std::atomic<ReplyDataType> replyData;
    static_assert(std::atomic<ReplyDataType>::is_always_lock_free, "ReplyDataType must be lock-free");
};

struct BufferAndViewReply
{
    VkBuffer Buffer{ VK_NULL_HANDLE};
    VkBufferView View{ VK_NULL_HANDLE };
};

struct ImageAndViewReply
{
    VkImage Image{ VK_NULL_HANDLE };
    VkImageView View{ VK_NULL_HANDLE };
};

// Specialize on atomic128, as (for now at least) it just gives us two 64 bit variables we can use to hold the resource handle and view handle
template<>
class ResourceMessageReply<BufferAndViewReply>
{
    // implicitly equal to VK_NULL_HANDLE, VK_NULL_HANDLE
    constexpr static cas_data128_t null_data{ 0, 0 };
public:
    ResourceMessageReply() noexcept : replyData{} {}
    ~ResourceMessageReply() noexcept = default;

    // Non-copyable
    ResourceMessageReply(const ResourceMessageReply&) = delete;
    ResourceMessageReply& operator=(const ResourceMessageReply&) = delete;
    
    // Movable
    ResourceMessageReply(ResourceMessageReply&& other) noexcept
        : replyData(other.replyData.load(std::memory_order_relaxed))
    {
        other.replyData.store(null_data, std::memory_order_relaxed);
    }
    
    ResourceMessageReply& operator=(ResourceMessageReply&& other) noexcept
    {
        if (this != &other)
        {
            replyData.store(other.replyData.load(std::memory_order_relaxed), std::memory_order_relaxed);
            other.replyData.store(null_data, std::memory_order_relaxed);  
        }
        return *this;
    }
    
    // Check if resource is ready
    bool IsReady() const
    {
        return replyData.load(std::memory_order_acquire) != null_data;
    }

    BufferAndViewReply GetReplyData() const
    {
        const cas_data128_t data = replyData.load(std::memory_order_acquire);
        return BufferAndViewReply
        {
            reinterpret_cast<VkBuffer>(data.low),
            reinterpret_cast<VkBufferView>(data.high)
        };
    }

    BufferAndViewReply WaitForResourceOperation(uint64_t timeoutNs = std::numeric_limits<uint64_t>::max()) const
    {
        std::chrono::high_resolution_clock::time_point startTime = std::chrono::high_resolution_clock::now();
        
        while (true)
        {
            const cas_data128_t data = replyData.load(std::memory_order_acquire);
            if (data != null_data)
            {
                return BufferAndViewReply
                {
                    reinterpret_cast<VkBuffer>(data.low),
                    reinterpret_cast<VkBufferView>(data.high)
                };
            }
            
            // Check timeout
            if (timeoutNs != std::numeric_limits<uint64_t>::max())
            {
                std::chrono::high_resolution_clock::time_point currentTime = std::chrono::high_resolution_clock::now();
                const uint64_t elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(currentTime - startTime).count();
                if (elapsedNs >= timeoutNs)
                {
                    return BufferAndViewReply{ VK_NULL_HANDLE, VK_NULL_HANDLE };
                }
            }
            
            // Yield to avoid busy waiting
            std::this_thread::yield();
        }
    }

private:

    friend class ResourceContextImpl;

    void SetResource(BufferAndViewReply rsrc)
    {
        const cas_data128_t data
        { 
            reinterpret_cast<uint64_t>(rsrc.Buffer),
            reinterpret_cast<uint64_t>(rsrc.View)
        };
        replyData.store(data, std::memory_order_release);
    }

    atomic128 replyData;
};

template<>
class ResourceMessageReply<ImageAndViewReply>
{
    constexpr static cas_data128_t null_data{ 0, 0 };
public:
    ResourceMessageReply() noexcept : replyData{} {}
    ~ResourceMessageReply() noexcept = default;

    // Non-copyable
    ResourceMessageReply(const ResourceMessageReply&) = delete;
    ResourceMessageReply& operator=(const ResourceMessageReply&) = delete;

    // Movable
    ResourceMessageReply(ResourceMessageReply&& other) noexcept
        : replyData(other.replyData.load(std::memory_order_relaxed))
    {
        other.replyData.store(null_data, std::memory_order_relaxed);
    }

    ResourceMessageReply& operator=(ResourceMessageReply&& other) noexcept
    {
        if (this != &other)
        {
            replyData.store(other.replyData.load(std::memory_order_relaxed), std::memory_order_relaxed);
            other.replyData.store(null_data, std::memory_order_relaxed);
        }
        return *this;
    }

    bool IsReady() const
    {
        return replyData.load(std::memory_order_acquire) != null_data;
    }
    
    ImageAndViewReply GetReplyData() const
    {
        const cas_data128_t data = replyData.load(std::memory_order_acquire);
        return ImageAndViewReply
        {
            reinterpret_cast<VkImage>(data.low),
            reinterpret_cast<VkImageView>(data.high)
        };
    }

    ImageAndViewReply WaitForResourceOperation(uint64_t timeoutNs = std::numeric_limits<uint64_t>::max()) const
    {
        std::chrono::high_resolution_clock::time_point startTime = std::chrono::high_resolution_clock::now();
        
        while (true)
        {
            const cas_data128_t data = replyData.load(std::memory_order_acquire);
            if (data != null_data)
            {
                return ImageAndViewReply
                {
                    reinterpret_cast<VkImage>(data.low),
                    reinterpret_cast<VkImageView>(data.high)
                };
            }
            
            // Check timeout
            if (timeoutNs != std::numeric_limits<uint64_t>::max())
            {
                std::chrono::high_resolution_clock::time_point currentTime = std::chrono::high_resolution_clock::now();
                const uint64_t elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(currentTime - startTime).count();
                if (elapsedNs >= timeoutNs)
                {
                    return ImageAndViewReply{ VK_NULL_HANDLE, VK_NULL_HANDLE };
                }   
            }
            
            // Yield to avoid busy waiting
            std::this_thread::yield();
        }
    }

private:

    friend class ResourceContextImpl;

    void SetResource(ImageAndViewReply rsrc)
    {
        const cas_data128_t data
        {
            reinterpret_cast<uint64_t>(rsrc.Image),
            reinterpret_cast<uint64_t>(rsrc.View)
        };
        replyData.store(data, std::memory_order_release);
    }

    atomic128 replyData;
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
    friend class ResourceContextImpl;
    
    // Mark operation as completed
    void SetCompleted()
    {
        completed.store(true, std::memory_order_release);
    }
    
    std::atomic<bool> completed;
    static_assert(std::atomic<bool>::is_always_lock_free, "std::atomic<bool> is not lockfree on this platform/using this compiler");
};


#endif //!RESOURCE_CONTEXT_RESOURCE_MESSAGE_REPLY_HPP