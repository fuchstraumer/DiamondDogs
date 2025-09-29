#pragma once
#ifndef RESOURCE_CONTEXT_RESOURCE_MESSAGE_REPLY_HPP
#define RESOURCE_CONTEXT_RESOURCE_MESSAGE_REPLY_HPP
#include "ResourceTypes.hpp"
#include "threading/atomic128.hpp"
#include <atomic>
#include <limits>

namespace rhi
{
    class Device;
}

/**
 * @brief Base class for all asynchronous operation replies
 * 
 * MessageReply provides status tracking for asynchronous operations in the ResourceContext.
 * All operations return reply objects that inherit from this class to allow monitoring
 * of completion status and waiting for results. Accessing the status is thread-safe
 * and lock-free since it uses atomic operations.
 */
class MessageReply
{
public:
    /**
     * @brief Status enumeration for tracking operation progress
     * 
     * Status values are listed in order of completion, with failure states at the end.
     */
    enum class Status : uint8_t
    {
        Invalid = 0,     /** Reply object is in an invalid state */
        Pending = 1,     /** Message has been queued and is actively being processed */
        Transferring,    /** Resource created and transfer data attached, not safe to use yet */
        Completed,       /** Creation, transfer, or both completed - safe to use */
        Failed,          /** Message processing failed, no resource created */
        Timeout,         /** Waiting for completion timed out, operation may still be in progress */
    };

    /// Default constructor - initializes status to Invalid
    MessageReply() : status(Status::Invalid) {}
    /// Virtual destructor for proper cleanup of derived classes
    virtual ~MessageReply() = default;
    
    /// Cannot copy or copy-assign MessageReply objects, as that's a fast path to UB
    MessageReply(const MessageReply&) = delete;
    MessageReply& operator=(const MessageReply&) = delete;

    // noexcept move and move-assign as use this for push/popping 
    MessageReply(MessageReply&& other) noexcept;
    MessageReply& operator=(MessageReply&& other) noexcept;
    
    /**
     * @brief Check if the operation has completed (successfully or with failure). Non-blocking.
     * 
     * @return True if status is Completed or Failed, false otherwise
     */
    virtual bool IsCompleted() const noexcept;
    
    /**
     * @brief Get the current status of the operation. Non-blocking.
     * 
     * @return Current status value
     */
    Status GetStatus() const noexcept;
    
    /**
     * @brief Wait for the operation to complete with optional timeout. This blocks the current thread
     * while waiting for the operation to finish.
     * 
     * @param timeoutNs Timeout in nanoseconds (default: no timeout, waits indefinitely)
     * @return Final status after waiting (Completed, Failed, or Timeout)
     * @note Use `std::numeric_limits<uint64_t>::max()` for no timeout
     */
    virtual Status WaitForCompletion(uint64_t timeoutNs = std::numeric_limits<uint64_t>::max()) noexcept;
    
protected:
    friend class ResourceContextImpl;
    friend class ResourceTransferSystem;
    void SetStatus(Status status) noexcept;
    std::atomic<Status> status;
    static_assert(decltype(status)::is_always_lock_free, "std::atomic<Status> is not lock-free on this platform/using this compiler");
};

/**
 * @brief Reply class for operations that involve GPU transfers
 * 
 * ResourceTransferReply extends MessageReply with additional functionality for tracking
 * GPU transfer operations using Vulkan timeline semaphores. This allows for fine-grained
 * synchronization with GPU command execution. This is a separate class as we can (and often
 * will) do resource transfers without creating a new resource.
 */
class ResourceTransferReply : public MessageReply
{
public:
    /// Default constructor
    ResourceTransferReply();
    /// Constructor with device for semaphore operations
    ResourceTransferReply(rhi::Device* _device);
    /// Virtual destructor
    virtual ~ResourceTransferReply();

    /**
     * @brief Get the timeline semaphore value for this transfer operation
     * 
     * @return Semaphore value that will be signaled when transfer completes. 0 if no transfer is associated with this reply
     * 
     * @note This is the value stored in the semaphore object, NOT the Vulkan semaphore handle.
     */
    uint64_t SemaphoreValue() const noexcept;
    
    /**
     * @brief Get the handle to the timeline semaphore used for this transfer
     * 
     * @return Vulkan semaphore handle (cast to uint64_t)
     */
    uint64_t SemaphoreHandle() const noexcept;

    /**
     * @brief Wait for completion including any associated GPU transfers
     * 
     * @param timeoutNs Timeout in nanoseconds (default: no timeout)
     * @return Final status after waiting
     * 
     * @note Waits for both CPU-side completion and GPU-side transfer completion
     */
    Status WaitForCompletion(uint64_t timeoutNs = std::numeric_limits<uint64_t>::max()) noexcept final;

protected:
    friend class ResourceContextImpl;
    friend class ResourceTransferSystem;

    uint64_t semaphoreHandle = 0u;
    const rhi::Device* device = nullptr;
};

/**
 * @brief Reply class for operations that create new graphics resources
 * 
 * GraphicsResourceReply extends ResourceTransferReply to include the actual resource handle
 * that gets created. The resource data is stored using atomic operations to ensure thread-safe
 * access even while the resource is still being created or transferred.
 */
class GraphicsResourceReply final : public ResourceTransferReply
{
    
    struct RhiResourceTypeAndEntityHandle
    {
        RhiResourceTypeAndEntityHandle() noexcept;
        RhiResourceTypeAndEntityHandle(const ResourceDomain domain, const ResourceType type, const uint32_t entity_handle) noexcept;
        RhiResourceTypeAndEntityHandle(const RhiResourceTypeAndEntityHandle& other) noexcept = default;
        RhiResourceTypeAndEntityHandle& operator=(const RhiResourceTypeAndEntityHandle& other) noexcept = default;
        RhiResourceTypeAndEntityHandle(RhiResourceTypeAndEntityHandle&& other) noexcept = default;
        RhiResourceTypeAndEntityHandle& operator=(RhiResourceTypeAndEntityHandle&& other) noexcept = default;
        ~RhiResourceTypeAndEntityHandle() noexcept = default;
        bool operator==(const RhiResourceTypeAndEntityHandle& other) const noexcept;
        bool operator!=(const RhiResourceTypeAndEntityHandle& other) const noexcept;
        
        /**
         * @brief Check if the resource handle is valid
         * 
         * @return True if both type and entity handle are valid
         * 
         * @note Resource is considered valid only after entity handle is set (happens last)
         */
        operator bool() const noexcept;
        
        uint16_t Domain;       ///< Resource domain from resource_domain enum
        uint16_t Type;         ///< Resource type from resource_type enum
        uint32_t EntityHandle; ///< ECS entity handle for internal tracking
    };


public:

    GraphicsResourceReply(ResourceDomain _domain, ResourceType _type);
    GraphicsResourceReply(ResourceDomain _domain, ResourceType _type, rhi::Device* _device);

    ~GraphicsResourceReply();
    GraphicsResourceReply(const GraphicsResourceReply&) = delete;
    GraphicsResourceReply& operator=(const GraphicsResourceReply&) = delete;
    
    /**
     * @brief Get the graphics resource handle. Uses acquire memory ordering
     * to ensure visibility of resource data, but can return a potentially
     * incomplete resource if called before completion. Check reply status
     * before using the returned resource.
     * 
     * @return GraphicsResource containing all Vulkan handles and metadata
     */
    GraphicsResource GetResource() const noexcept;

private:

    friend class ResourceContextImpl;
    friend class TransferSystem;
    /** @brief Called by internal systems to update the internal data atomically. */
    void SetGraphicsResource(
        const ResourceDomain _domain,
        const ResourceType _type,
        const uint32_t entity_handle,
        const uint64_t vk_handle,
        const uint64_t vk_view_handle,
        const uint64_t vk_sampler_handle) noexcept;
    /** @brief Relaxed version of SetGraphicsResource for use when already synchronized, or to push piecemeal updates without atomicity before a final atomic commit. */
    void SetGraphicsResourceRelaxed(const GraphicsResource& resource) noexcept;
    
    std::atomic<RhiResourceTypeAndEntityHandle> resourceTypeAndEntityHandle;
    static_assert(decltype(resourceTypeAndEntityHandle)::is_always_lock_free, "std::atomic<RhiResourceTypeAndEntityHandle> is not lock-free on this platform/using this compiler");
    std::atomic<uint64_t> vkSamplerHandle;
    atomic128 vkHandleAndView;
};

/**
 * @brief Reply class for operations that return a CPU pointer
 * 
 * PointerMessageReply is used specifically for buffer mapping operations that return
 * a CPU-accessible pointer to buffer memory. The pointer is stored atomically.
 */
class PointerMessageReply final : public MessageReply
{
public:
    /// Default constructor - initializes pointer to nullptr
    PointerMessageReply() : data(nullptr) {}
    /// Destructor
    ~PointerMessageReply() = default;
    
    /// Copy constructor deleted
    PointerMessageReply(const PointerMessageReply&) = delete;
    /// Copy assignment deleted
    PointerMessageReply& operator=(const PointerMessageReply&) = delete;
    /// Move constructor
    PointerMessageReply(PointerMessageReply&& other) noexcept;
    /// Move assignment operator
    PointerMessageReply& operator=(PointerMessageReply&& other) noexcept;

    /**
     * @brief Get the CPU pointer returned by the operation. Check reply status
     * before using the returned pointer to be sure of validity.
     * 
     * @return CPU-accessible pointer to buffer memory, or nullptr if not ready/failed
     */
    void* GetPointer() const noexcept;
    
private:
    friend class ResourceContextImpl;
    void SetPointer(void* ptr) noexcept;

    std::atomic<void*> data;
};


#endif //!RESOURCE_CONTEXT_RESOURCE_MESSAGE_REPLY_HPP