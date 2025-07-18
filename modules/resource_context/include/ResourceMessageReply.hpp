#pragma once
#ifndef RESOURCE_CONTEXT_RESOURCE_MESSAGE_REPLY_HPP
#define RESOURCE_CONTEXT_RESOURCE_MESSAGE_REPLY_HPP
#include "ResourceTypes.hpp"
#include "threading/atomic128.hpp"
#include <atomic>
#include <limits>
#include <vulkan/vulkan_core.h>

namespace vpr
{
    class Device;
}

/**
 * @brief Base class for all asynchronous operation replies
 * 
 * MessageReply provides status tracking for asynchronous operations in the ResourceContext.
 * All operations return reply objects that inherit from this class to allow monitoring
 * of completion status and waiting for results.
 * 
 * @note This class uses lock-free atomic operations for thread-safe status updates
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
        Invalid = 0,     ///< Reply object is in an invalid state
        Pending = 1,     ///< Message has been queued and is actively being processed
        Transferring,    ///< Resource created and transfer data attached, not safe to use yet
        Completed,       ///< Creation, transfer, or both completed - safe to use
        Failed,          ///< Message processing failed, no resource created
        Timeout,         ///< Waiting for completion timed out, operation may still be in progress
    };

    /// Default constructor - initializes status to Invalid
    MessageReply() : status(Status::Invalid) {}
    /// Virtual destructor for proper cleanup of derived classes
    virtual ~MessageReply() = default;
    
    /// Copy constructor deleted - reply objects are not copyable
    MessageReply(const MessageReply&) = delete;
    /// Copy assignment deleted - reply objects are not copyable
    MessageReply& operator=(const MessageReply&) = delete;
    /// Move constructor
    MessageReply(MessageReply&& other) noexcept;
    /// Move assignment operator
    MessageReply& operator=(MessageReply&& other) noexcept;
    
    /**
     * @brief Check if the operation has completed (successfully or with failure)
     * 
     * @return True if status is Completed or Failed, false otherwise
     * 
     * @note This is a non-blocking call that returns immediately
     */
    virtual bool IsCompleted() const noexcept;
    
    /**
     * @brief Get the current status of the operation
     * 
     * @return Current status value
     * 
     * @note This is a non-blocking call using atomic memory operations
     */
    Status GetStatus() const noexcept;
    
    /**
     * @brief Wait for the operation to complete with optional timeout
     * 
     * @param timeoutNs Timeout in nanoseconds (default: no timeout)
     * @return Final status after waiting (Completed, Failed, or Timeout)
     * 
     * @note This call blocks the current thread until completion or timeout
     * @note A timeout value of max() means wait indefinitely
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
 * synchronization with GPU command execution.
 * 
 * @note This is a separate class as sometimes operations involve transfers without creating new resources
 */
class ResourceTransferReply : public MessageReply
{
public:
    /// Default constructor
    ResourceTransferReply();
    /// Constructor with device for semaphore operations
    ResourceTransferReply(vpr::Device* _device);
    /// Virtual destructor
    virtual ~ResourceTransferReply();

    /**
     * @brief Get the timeline semaphore value for this transfer operation
     * 
     * @return Semaphore value that will be signaled when transfer completes
     * 
     * @note Can be used for manual synchronization with other GPU operations
     * @note Returns 0 if no transfer is associated with this reply
     */
    uint64_t SemaphoreValue() const noexcept;
    
    /**
     * @brief Get the handle to the timeline semaphore used for this transfer
     * 
     * @return Vulkan semaphore handle (cast to uint64_t)
     * 
     * @note Can be used for manual synchronization with other GPU operations
     * @note Returns 0 if no transfer is associated with this reply
     */
    uint64_t SemaphoreHandle() const noexcept;

    /**
     * @brief Wait for completion including any associated GPU transfers
     * 
     * @param timeoutNs Timeout in nanoseconds (default: no timeout)
     * @return Final status after waiting
     * 
     * @note Final override - derived classes don't change the waiting behavior
     * @note Waits for both CPU-side completion and GPU-side transfer completion
     */
    Status WaitForCompletion(uint64_t timeoutNs = std::numeric_limits<uint64_t>::max()) noexcept final;

protected:
    friend class ResourceContextImpl;
    friend class ResourceTransferSystem;

    uint64_t semaphoreHandle = 0u;
    const vpr::Device* device = nullptr;
};

/**
 * @brief Reply class for operations that create new graphics resources
 * 
 * GraphicsResourceReply extends ResourceTransferReply to include the actual resource handle
 * that gets created. The resource data is stored using atomic operations to ensure thread-safe
 * access even while the resource is still being created or transferred.
 * 
 * @note Uses atomic operations and 128-bit atomics for lock-free resource handle storage
 */
class GraphicsResourceReply final : public ResourceTransferReply
{
    
    struct VkResourceTypeAndEntityHandle
    {
        /// Default constructor (implementation in .cpp to avoid exposing entt)
        VkResourceTypeAndEntityHandle() noexcept;
        /// Constructor with type and entity handle
        VkResourceTypeAndEntityHandle(const resource_type type, const uint32_t entity_handle) noexcept;
        VkResourceTypeAndEntityHandle(const VkResourceTypeAndEntityHandle& other) noexcept = default;
        VkResourceTypeAndEntityHandle& operator=(const VkResourceTypeAndEntityHandle& other) noexcept = default;
        VkResourceTypeAndEntityHandle(VkResourceTypeAndEntityHandle&& other) noexcept = default;
        VkResourceTypeAndEntityHandle& operator=(VkResourceTypeAndEntityHandle&& other) noexcept = default;
        ~VkResourceTypeAndEntityHandle() noexcept = default;
        bool operator==(const VkResourceTypeAndEntityHandle& other) const noexcept;
        bool operator!=(const VkResourceTypeAndEntityHandle& other) const noexcept;
        
        /**
         * @brief Check if the resource handle is valid
         * 
         * @return True if both type and entity handle are valid
         * 
         * @note Resource is considered valid only after entity handle is set (happens last)
         */
        operator bool() const noexcept;
        
        uint32_t Type;         ///< Resource type from resource_type enum
        uint32_t EntityHandle; ///< ECS entity handle for internal tracking
    };


public:

    /**
     * @brief Constructor for resource replies of a specific type
     * 
     * @param _type The type of resource that will be created
     */
    GraphicsResourceReply(resource_type _type);
    
    /**
     * @brief Constructor with device for transfer operations
     * 
     * @param _type The type of resource that will be created
     * @param _device Device pointer for semaphore operations
     */
    GraphicsResourceReply(resource_type _type, vpr::Device* _device);
    
    /// Destructor
    ~GraphicsResourceReply();

    /// Copy constructor deleted
    GraphicsResourceReply(const GraphicsResourceReply&) = delete;
    /// Copy assignment deleted
    GraphicsResourceReply& operator=(const GraphicsResourceReply&) = delete;
    
    /**
     * @brief Get the graphics resource handle
     * 
     * @return GraphicsResource containing all Vulkan handles and metadata
     * 
     * @note Uses acquire memory ordering to ensure visibility of resource data
     * @note Returns a potentially incomplete resource if called before completion
     * @note Check reply status before using the returned resource
     */
    GraphicsResource GetResource() const noexcept;

private:

    friend class ResourceContextImpl;
    friend class TransferSystem;
    void SetGraphicsResource(
        const resource_type _type,
        const uint32_t entity_handle,
        const uint64_t vk_handle,
        const uint64_t vk_view_handle,
        const uint64_t vk_sampler_handle) noexcept;

    void SetGraphicsResourceRelaxed(const GraphicsResource& resource) noexcept;
    
    std::atomic<VkResourceTypeAndEntityHandle> resourceTypeAndEntityHandle;
    std::atomic<uint64_t> vkSamplerHandle;
    static_assert(decltype(resourceTypeAndEntityHandle)::is_always_lock_free, "std::atomic<VkResourceTypeAndEntityHandle> is not lock-free on this platform/using this compiler");
    atomic128 vkHandleAndView;
};

/**
 * @brief Reply class for operations that return a CPU pointer
 * 
 * PointerMessageReply is used specifically for buffer mapping operations that return
 * a CPU-accessible pointer to buffer memory. The pointer is stored atomically to
 * ensure thread-safe access.
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
     * @brief Get the CPU pointer returned by the operation
     * 
     * @return CPU-accessible pointer to buffer memory, or nullptr if not ready/failed
     * 
     * @note Check reply status before using the returned pointer
     * @note The pointer remains valid until the corresponding unmap operation
     */
    void* GetPointer() const noexcept;
    
private:
    friend class ResourceContextImpl;
    void SetPointer(void* ptr) noexcept;

    std::atomic<void*> data;
};


#endif //!RESOURCE_CONTEXT_RESOURCE_MESSAGE_REPLY_HPP