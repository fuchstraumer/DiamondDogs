#pragma once
#ifndef DIAMOND_DOGS_RESOURCE_CONTEXT_HPP
#define DIAMOND_DOGS_RESOURCE_CONTEXT_HPP
#include "ResourceTypes.hpp"
#include "ResourceMessageReply.hpp"
#include "RhiHandle.hpp"
#include <cstdint>
#include <cstddef>
#include <memory>
#include <span>
#include <string_view>

namespace rhi
{
    class Device;
    class Instance;
}

/**
 * @brief Configuration structure for initializing a ResourceContext
 * 
 * Chosen so we can expand configuration options in the future without breaking API
 */
struct ResourceContextCreateInfo
{
    const rhi::Instance* Instance{ nullptr };   ///< The instance that created Device, used to create the allocator
    const rhi::Device* Device{ nullptr };       ///< The logical device to use for resource operations
    uint32_t VulkanApiVersion{ 0u };            ///< The packed API version the instance was created with (RhiSystem::GetVulkanApiVersion)
    bool ValidationEnabled{ false };            ///< Whether Vulkan validation layers are enabled
    bool DeviceAddressesEnabled{ false };       ///< Whether this session needs device addresses enabled for buffers
};

enum class ResourceContextError : uint8_t
{
    Invalid = 0,
    Success = 1,
    AlreadyInitialized = 2,
    /** @brief The create info has no instance or no device. */
    InvalidCreateInfo = 3,
    /** @brief VMA could not create an allocator, for the context or for its transfer system. */
    AllocatorCreationFailed = 4,
    /** @brief The transfer system could not create its timeline semaphore or its command pools. */
    TransferSetupFailed = 5,
};

[[nodiscard]] std::string_view ToString(ResourceContextError error) noexcept;

/**
 * @brief A timeline that the owner signals, and the value that its latest submitted or recording work will signal.
 * The context frees a destroyed resource only after every registered timeline passed the value it had at the destroy.
 */
struct ConsumerTimelineValue
{
    rhi::TimelineSemaphoreHandle Timeline;
    uint64_t PendingValue{ 0u };
};

/** @brief Counts for tests and profiling. Each is read with a relaxed load, so together they are not one snapshot. */
struct ResourceContextCounters
{
    int64_t MessagesProcessed{ 0 };
    int64_t TransfersRecorded{ 0 };
    int64_t TransfersFailed{ 0 };
    int64_t TransferBatchesSubmitted{ 0 };
    /** @brief Reads of the transfer timeline's counter. One read completes every transfer at or below the value. */
    int64_t TransferCompletionReads{ 0 };
    /** @brief Reads of consumer timeline counters: one per registered timeline per Tick. */
    int64_t ConsumerTimelineReads{ 0 };
    int64_t BytesStaged{ 0 };
    int64_t ResourcesRetired{ 0 };
    /** @brief Destroyed resources that still wait for a timeline. */
    int64_t RetirementsPending{ 0 };
    /** @brief Calls that found the context's message queue full and blocked. */
    int64_t ContextPushesBlocked{ 0 };
    /** @brief Times the context worker found the transfer queue full and blocked. */
    int64_t TransferPushesBlocked{ 0 };
};

class ResourceContextImpl;

/**
 * @brief Main interface for asynchronous GPU resource management
 * 
 * ResourceContext provides a high-level, thread-safe interface for creating and managing
 * Vulkan resources such as buffers, images, and samplers. All operations are asynchronous
 * and return reply objects that can be used to track completion status, and internal state
 * and resources are managed using an ECS (ENTT, to be specific).
 */
class ResourceContext
{
public:
    /**
     * @brief Default constructor
     * 
     * Creates an uninitialized ResourceContext. You must call Initialize() before using any other methods.
     */
    ResourceContext();
    
    /**
     * @brief Destructor
     * 
     * Automatically waits for all pending operations to complete and cleans up resources.
     */
    ~ResourceContext();
    
    /// Copy constructor deleted - ResourceContext is not copyable
    ResourceContext(const ResourceContext&) = delete;
    /// Copy assignment deleted - ResourceContext is not copyable
    ResourceContext& operator=(const ResourceContext&) = delete;

    /**
     * @brief Initializes the ResourceContext with the given configuration, and starts its worker threads. Must be
     * called before any other operation. On failure, no thread is started and the context stays uninitialized.
     *
     * @param create_info Configuration structure containing device pointers and options
     */
    [[nodiscard]] ResourceContextError Initialize(const ResourceContextCreateInfo& create_info);

    /**
     * @brief Create a buffer, with a buffer view when `createInfo.ViewFormat` is set. If initial_data is provided,
     * the buffer is created in a transferring state and is not complete until the upload finishes. Host-visible
     * buffers are written directly; device-local buffers go through the transfer system.
     *
     * @param create_info Domain, usage, size, creation flags and an optional debug name.
     * @param initial_data Initial data regions, written one after another from offset 0. Empty for none.
     * @return Shared pointer to reply object for tracking creation progress
     */
    [[nodiscard]] std::shared_ptr<GraphicsResourceReply> CreateBuffer(
        const BufferCreateInfo& create_info,
        std::span<const RhiBufferResourceData> initial_data);

    /**
     * @brief Create an image, with a view when `createInfo.ViewInfo` is set and a sampler when
     * `createInfo.SamplerInfo` is set (the resource type is then CombinedImageSampler). With initialData, the
     * transfer system uploads the data and moves the image to the layout that its usage asks for.
     *
     * @param create_info Domain, usage, format, extent and optional view and sampler
     * @param initial_data Initial data, one element per mip level and array layer range. Empty for none.
     * @return Shared pointer to reply object for tracking creation progress
     */
    [[nodiscard]] std::shared_ptr<GraphicsResourceReply> CreateImage(
        const ImageCreateInfo& create_info,
        std::span<const RhiImageResourceData> initial_data);

    /**
     * @brief Create a sampler
     *
     * @param create_info Sampler creation parameters
     * @param sampler_name Optional name for the sampler resource
     * @return Shared pointer to reply object for tracking creation progress
     *
     * @note Sampler creation is typically fast as it doesn't involve memory allocation
     */
    [[nodiscard]] std::shared_ptr<GraphicsResourceReply> CreateSampler(
        const SamplerCreateInfo& create_info,
        std::string_view sampler_name = {});

    /**
     * @brief Upload data to an existing buffer.
     * This operation may use staging buffers for GPU-only resources, briefly inflating memory usage. For
     * CPU-accessible buffers, data is written directly to mapped memory. Multiple data regions can be 
     * uploaded in a single operation.
     * 
     * @param buffer The target buffer resource to update
     * @param data The data regions to upload
     * @return Shared pointer to reply object for tracking transfer progress
     */
    [[nodiscard]] std::shared_ptr<ResourceTransferReply> SetBufferData(
        GraphicsResource buffer,
        std::span<const RhiBufferResourceData> data);

    /**
     * @brief Upload data to an existing image, including specific mip levels and array layers.
     * Uses staging buffers and performs layout transitions as needed.
     * 
     * @param image The target image resource to update
     * @param data The data regions to upload
     * @return Shared pointer to reply object for tracking transfer progress
     */
    [[nodiscard]] std::shared_ptr<ResourceTransferReply> SetImageData(
        GraphicsResource image,
        std::span<const RhiImageResourceData> data);
        
    /**
     * @brief Fill a buffer region with a repeating 32-bit value
     * 
     * @param buffer The target buffer resource to fill
     * @param value The 32-bit value to repeat throughout the region
     * @param offset Byte offset from the start of the buffer
     * @param size Number of bytes to fill (must be multiple of 4)
     * @return Shared pointer to reply object for tracking transfer progress
     * 
     * @note This is implemented using vkCmdFillBuffer for optimal performance, so the size parameter must be a multiple of 4 bytes
     */
    [[nodiscard]] std::shared_ptr<ResourceTransferReply> FillBuffer(
        GraphicsResource buffer,
        uint32_t value,
        size_t offset,
        size_t size);
        
    /**
     * @brief Map a buffer region into CPU-accessible memory
     * 
     * @param buffer The buffer resource to map
     * @param size The size of the region to map
     * @param offset The byte offset from the start of the buffer
     * @return Shared pointer to reply object containing the mapped pointer
     * 
     * @note Only works with CPU-accessible buffers (CPUOnly, CPUToGPU, GPUToCPU usage) and multiple overlapping map operations are not supported
     */
    [[nodiscard]] std::shared_ptr<PointerMessageReply> MapBuffer(
        GraphicsResource buffer,
        size_t size,
        size_t offset);
        
    /**
     * @brief Unmap a previously mapped buffer region
     * 
     * @param buffer The buffer resource to unmap
     * @param size Number of bytes that were mapped
     * @param offset Byte offset that was mapped
     * @return Shared pointer to reply object for tracking completion
     */
    [[nodiscard]] std::shared_ptr<MessageReply> UnmapBuffer(
        GraphicsResource buffer, 
        size_t size,
        size_t offset);

    /**
     * @brief Create a copy of an existing buffer
     * 
     * @param src The source buffer to copy from
     * @param copy_contents Whether to copy the buffer's contents or just create an empty buffer with the same properties
     * @return Shared pointer to reply object containing the new buffer resource
     */
    [[nodiscard]] std::shared_ptr<GraphicsResourceReply> CopyBuffer(GraphicsResource src, bool copy_contents);

    /**
     * @brief Create a copy of an existing image
     * 
     * @param src The source image to copy from
     * @return Shared pointer to reply object containing the new image resource
     * 
     * @note Proper image layout transitions are handled automatically
     */
    [[nodiscard]] std::shared_ptr<GraphicsResourceReply> CopyImage(GraphicsResource src);
    
    /**
     * @brief Copy contents from one buffer to another existing buffer
     * 
     * @param src The source buffer to copy from
     * @param dst The destination buffer to copy to
     * @return Shared pointer to reply object for tracking transfer progress
     * 
     * @note The entire source buffer is copied to the destination buffer
     * @note Uses GPU-side copy operations for optimal performance
     * @note Either side may be an image: image to buffer copies mip 0 of every layer, tightly packed from offset 0
     *       (the readback path; the image needs TransferSource usage and the buffer CopyDestination), and buffer to
     *       image is the reverse
     */
    [[nodiscard]] std::shared_ptr<ResourceTransferReply> CopyBufferContents(
        GraphicsResource src,
        GraphicsResource dst);

    /**
     * @brief Destroy a graphics resource and free its memory, once the GPU is done with it.
     *
     * The destroy records the pending value of every registered consumer timeline, and the last transfer that touches
     * the resource. The objects are freed, and the reply completes, once all of those have passed. With nothing to wait
     * for, that happens at once. From the moment the context processes the destroy, every other call that names the
     * resource fails.
     *
     * @param resource The resource to destroy
     * @return Shared pointer to reply object, which completes when the objects are freed
     *
     * @note All associated Vulkan objects (buffer/image, view, sampler) are destroyed, with memory returned to the allocator
     */
    [[nodiscard]] std::shared_ptr<MessageReply> DestroyResource(
        GraphicsResource resource);

    /**
     * @brief Adds a timeline that destroys wait for, such as the renderer's frame timeline. At most
     * k_MaxConsumerTimelines at once. The reply fails when the timeline is already registered or no slot is free.
     */
    [[nodiscard]] std::shared_ptr<MessageReply> RegisterConsumerTimeline(rhi::TimelineSemaphoreHandle timeline);

    /**
     * @brief Removes a timeline. Call it only after the GPU passed every value you reported for it: resources waiting
     * on it no longer wait. The semaphore may be destroyed once the reply completes.
     */
    [[nodiscard]] std::shared_ptr<MessageReply> UnregisterConsumerTimeline(rhi::TimelineSemaphoreHandle timeline);

    /**
     * @brief The owner's per-frame call. Reports each registered timeline's pending value: the value that the work
     * recorded from now on will signal at the latest. The context reads each registered timeline's counter once, and
     * frees the resources whose waits passed. Call it before recording a frame, so that a destroy issued while
     * recording waits for that frame. Values for timelines that are not registered are ignored, and so is every value
     * after the first k_MaxConsumerTimelines.
     */
    void Tick(std::span<const ConsumerTimelineValue> pending_values);

    /** @brief Returns the counters. Safe from any thread. */
    [[nodiscard]] ResourceContextCounters Counters() const noexcept;

private:
    std::unique_ptr<ResourceContextImpl> impl;
};

#endif //!DIAMOND_DOGS_RESOURCE_CONTEXT_HPP
