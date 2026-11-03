#pragma once
#ifndef DIAMOND_DOGS_RESOURCE_CONTEXT_HPP
#define DIAMOND_DOGS_RESOURCE_CONTEXT_HPP
#include "ResourceTypes.hpp"
#include "ResourceFuture.hpp"
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


/** @brief The default size of a context's future table: 4096 slots of 64 bytes, 256 KiB. One slot is shared by the
 *  futures of a full table, so 4095 futures can be live at once. */
constexpr inline int32_t k_DefaultMaxFutures = 4096;

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
    int32_t MaxFutures{ k_DefaultMaxFutures };  ///< The size of the future table. At least 2
};

enum class ResourceContextError : uint8_t
{
    Invalid = 0,
    Success = 1,
    AlreadyInitialized = 2,
    /** @brief The create info has no instance or no device, or MaxFutures is below 2. */
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
    /** @brief Futures that hold a slot of the future table: alive, or released while their call is still in flight. */
    int64_t FuturesLive{ 0 };
    /** @brief Calls that found the future table full. Each returned a Failed future and did nothing. */
    int64_t FutureTableFull{ 0 };
};

class ResourceContextImpl;
class ResourceContext;

namespace rhi::placement
{
    struct MemoryRequirements;
    struct MemoryHeapCreateInfo;
    struct Placement;

    // declared in ResourcePlacement.hpp. They are declared here only so that ResourceContext can befriend them
    MemoryRequirements GetImageMemoryRequirements(const ResourceContext& context, const ImageCreateInfo& create_info);
    MemoryRequirements GetBufferMemoryRequirements(const ResourceContext& context, const BufferCreateInfo& create_info);
    ResourceFuture<GraphicsResource> CreateMemoryHeap(ResourceContext& context, const MemoryHeapCreateInfo& create_info);
    ResourceFuture<GraphicsResource> CreatePlacedImage(ResourceContext& context, const ImageCreateInfo& create_info, Placement placement);
    ResourceFuture<GraphicsResource> CreatePlacedBuffer(ResourceContext& context, const BufferCreateInfo& create_info, Placement placement);
}

/**
 * @brief Main interface for asynchronous GPU resource management
 *
 * ResourceContext provides a high-level, thread-safe interface for creating and managing
 * Vulkan resources such as buffers, images, and samplers. All operations are asynchronous
 * and return futures that can be polled for their state, and internal state and resources
 * are managed using an ECS (ENTT, to be specific). When the future table is full, a call
 * does nothing and returns a future that is already Failed with FutureTableFull.
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
     * @return Future for tracking creation progress
     */
    [[nodiscard]] rhi::ResourceFuture<GraphicsResource> CreateBuffer(
        const BufferCreateInfo& create_info,
        std::span<const RhiBufferResourceData> initial_data);

    /**
     * @brief Create an image with no data, with a view when `createInfo.ViewInfo` is set and a sampler when
     * `createInfo.SamplerInfo` is set (the resource type is then CombinedImageSampler). The image has no defined
     * layout until the first transfer or the first barrier on the GPU.
     *
     * @param create_info Domain, usage, format, extent and optional view and sampler
     * @return Future for tracking creation progress
     */
    [[nodiscard]] rhi::ResourceFuture<GraphicsResource> CreateImage(const ImageCreateInfo& create_info);

    /**
     * @brief Create an image and upload data to it. The transfer system moves the image from UNDEFINED to the layout
     * that `usage_after_upload` needs. The context adds TransferDestination to the create usage.
     *
     * @param create_info Domain, usage, format, extent and optional view and sampler
     * @param initial_data Initial data, one element per mip level and array layer range
     * @param usage_after_upload The usage of the image's first GPU use. It must be non-zero and a subset of the usage
     *        of the image, or the future fails with InvalidUsageTransition.
     * @return Future for tracking creation progress
     */
    [[nodiscard]] rhi::ResourceFuture<GraphicsResource> CreateImage(
        const ImageCreateInfo& create_info,
        std::span<const RhiImageResourceData> initial_data,
        ImageUsageBits usage_after_upload);

    /**
     * @brief Create a sampler
     *
     * @param create_info Sampler creation parameters, and an optional debug name
     * @return Future for tracking creation progress
     *
     * @note Sampler creation is typically fast as it doesn't involve memory allocation
     */
    [[nodiscard]] rhi::ResourceFuture<GraphicsResource> CreateSampler(const SamplerCreateInfo& create_info);

    /**
     * @brief Upload data to an existing buffer.
     * This operation may use staging buffers for GPU-only resources, briefly inflating memory usage. For
     * CPU-accessible buffers, data is written directly to mapped memory. Multiple data regions can be 
     * uploaded in a single operation.
     * 
     * @param buffer The target buffer resource to update
     * @param data The data regions to upload
     * @param waits Up to k_MaxTransferWaits timeline values that the transfer waits for on the GPU. Empty for none.
     *        **The caller must make sure that a submit already signals each value, or will signal it without waiting
     *        for this transfer.** Otherwise the GPU can deadlock. The context does not check this. A call with more
     *        waits fails at once with TooManyWaits. A wait with a null semaphore or a value of 0 fails with
     *        InvalidWait.
     *        A host-visible buffer is written at once, so a wait there fails with WaitOnHostWrite.
     * @return Future for tracking transfer progress
     */
    [[nodiscard]] rhi::OperationFuture SetBufferData(
        GraphicsResource buffer,
        std::span<const RhiBufferResourceData> data,
        std::span<const rhi::TransferWait> waits);

    /**
     * @brief Upload data to an existing image, including specific mip levels and array layers. Uses staging buffers.
     * The transfer moves the image from the layout of `usage.Before` to the layout of `usage.After`. The caller knows
     * the image's last GPU use, so the caller gives `Before`.
     *
     * @param image The target image resource to update
     * @param usage The usage of the image before the transfer, and the usage it must have after it
     * @param data The data regions to upload
     * @param waits Up to k_MaxTransferWaits timeline values that the transfer waits for on the GPU. Empty for none.
     *        **The caller must make sure that a submit already signals each value, or will signal it without waiting
     *        for this transfer.** Otherwise the GPU can deadlock. The context does not check this. A call with more
     *        waits fails at once with TooManyWaits. A wait with a null semaphore or a value of 0 fails with
     *        InvalidWait.
     * @return Future for tracking transfer progress. Fails with InvalidUsageTransition for a usage that is not valid.
     */
    [[nodiscard]] rhi::OperationFuture SetImageData(
        GraphicsResource image,
        rhi::ImageUsageTransition usage,
        std::span<const RhiImageResourceData> data,
        std::span<const rhi::TransferWait> waits);

    /**
     * @brief Fill a buffer region with a repeating 32-bit value
     * 
     * @param buffer The target buffer resource to fill
     * @param value The 32-bit value to repeat throughout the region
     * @param offset Byte offset from the start of the buffer
     * @param size Number of bytes to fill (must be multiple of 4)
     * @param waits Up to k_MaxTransferWaits timeline values that the transfer waits for on the GPU. Empty for none.
     *        **The caller must make sure that a submit already signals each value, or will signal it without waiting
     *        for this transfer.** Otherwise the GPU can deadlock. The context does not check this. A call with more
     *        waits fails at once with TooManyWaits. A wait with a null semaphore or a value of 0 fails with
     *        InvalidWait.
     * @return Future for tracking transfer progress
     * 
     * @note This is implemented using vkCmdFillBuffer for optimal performance, so the size parameter must be a multiple of 4 bytes
     */
    [[nodiscard]] rhi::OperationFuture FillBuffer(
        GraphicsResource buffer,
        uint32_t value,
        size_t offset,
        size_t size,
        std::span<const rhi::TransferWait> waits);
        
    /**
     * @brief Map a buffer region into CPU-accessible memory
     * 
     * @param buffer The buffer resource to map
     * @param size The size of the region to map
     * @param offset The byte offset from the start of the buffer
     * @return Future of the mapped pointer
     * 
     * @note Only works with CPU-accessible buffers (CPUOnly, CPUToGPU, GPUToCPU usage) and multiple overlapping map operations are not supported
     */
    [[nodiscard]] rhi::ResourceFuture<void*> MapBuffer(
        GraphicsResource buffer,
        size_t size,
        size_t offset);
        
    /**
     * @brief Unmap a previously mapped buffer region
     * 
     * @param buffer The buffer resource to unmap
     * @param size Number of bytes that were mapped
     * @param offset Byte offset that was mapped
     * @return Future for tracking completion
     */
    [[nodiscard]] rhi::OperationFuture UnmapBuffer(
        GraphicsResource buffer, 
        size_t size,
        size_t offset);

    /**
     * @brief Create a copy of an existing buffer
     * 
     * @param src The source buffer to copy from
     * @param copy_contents Whether to copy the buffer's contents or just create an empty buffer with the same properties
     * @param waits Up to k_MaxTransferWaits timeline values that the transfer waits for on the GPU. Empty for none.
     *        **The caller must make sure that a submit already signals each value, or will signal it without waiting
     *        for this transfer.** Otherwise the GPU can deadlock. The context does not check this. A call with more
     *        waits fails at once with TooManyWaits. A wait with a null semaphore or a value of 0 fails with
     *        InvalidWait.
     *        The waits apply to the copy of the contents. Without contents, they are ignored.
     * @return Future of the new buffer resource. Fails with WrongResourceKind when `src` is an image.
     */
    [[nodiscard]] rhi::ResourceFuture<GraphicsResource> CopyBuffer(
        GraphicsResource src,
        bool copy_contents,
        std::span<const rhi::TransferWait> waits);

    /**
     * @brief Create a copy of an existing image, with its contents. The new image starts from UNDEFINED and ends in
     * the layout that `copy_usage_after` needs.
     *
     * @param src The source image to copy from
     * @param src_usage The usage of the source before the copy, and the usage it must have after the copy
     * @param copy_usage_after The usage of the new image after the copy. It must be non-zero and a subset of the usage
     *        of the source plus TransferDestination, which the new image gets.
     * @param waits Up to k_MaxTransferWaits timeline values that the transfer waits for on the GPU. Empty for none.
     *        **The caller must make sure that a submit already signals each value, or will signal it without waiting
     *        for this transfer.** Otherwise the GPU can deadlock. The context does not check this. A call with more
     *        waits fails at once with TooManyWaits. A wait with a null semaphore or a value of 0 fails with
     *        InvalidWait.
     * @return Future of the new image resource. Fails with WrongResourceKind when `src` is a buffer, and with
     *         InvalidUsageTransition for a usage that is not valid.
     */
    [[nodiscard]] rhi::ResourceFuture<GraphicsResource> CopyImage(
        GraphicsResource src,
        rhi::ImageUsageTransition src_usage,
        ImageUsageBits copy_usage_after,
        std::span<const rhi::TransferWait> waits);

    /**
     * @brief Copy the contents of one buffer to another existing buffer. The entire source is copied.
     *
     * @param src The source buffer to copy from
     * @param dst The destination buffer to copy to
     * @param waits Up to k_MaxTransferWaits timeline values that the transfer waits for on the GPU. Empty for none.
     *        **The caller must make sure that a submit already signals each value, or will signal it without waiting
     *        for this transfer.** Otherwise the GPU can deadlock. The context does not check this. A call with more
     *        waits fails at once with TooManyWaits. A wait with a null semaphore or a value of 0 fails with
     *        InvalidWait.
     * @return Future for tracking transfer progress. Fails with WrongResourceKind when either side is an image.
     */
    [[nodiscard]] rhi::OperationFuture CopyBufferContents(
        GraphicsResource src,
        GraphicsResource dst,
        std::span<const rhi::TransferWait> waits);

    /**
     * @brief Copy mip 0 of every layer of an image into a buffer, tightly packed from offset 0 (the readback path).
     * The image needs TransferSource usage and the buffer CopyDestination usage.
     *
     * @param image The source image
     * @param image_usage The usage of the image before the copy, and the usage it must have after the copy
     * @param buffer The destination buffer
     * @param waits Up to k_MaxTransferWaits timeline values that the transfer waits for on the GPU. Empty for none.
     *        **The caller must make sure that a submit already signals each value, or will signal it without waiting
     *        for this transfer.** Otherwise the GPU can deadlock. The context does not check this. A call with more
     *        waits fails at once with TooManyWaits. A wait with a null semaphore or a value of 0 fails with
     *        InvalidWait.
     * @return Future for tracking transfer progress
     */
    [[nodiscard]] rhi::OperationFuture CopyImageToBuffer(
        GraphicsResource image,
        rhi::ImageUsageTransition image_usage,
        GraphicsResource buffer,
        std::span<const rhi::TransferWait> waits);

    /**
     * @brief Copy a whole buffer into mip 0 of every layer of an image, tightly packed from offset 0. The buffer needs
     * CopySource usage and the image TransferDestination usage.
     *
     * @param buffer The source buffer
     * @param image The destination image
     * @param image_usage The usage of the image before the copy, and the usage it must have after the copy
     * @param waits Up to k_MaxTransferWaits timeline values that the transfer waits for on the GPU. Empty for none.
     *        **The caller must make sure that a submit already signals each value, or will signal it without waiting
     *        for this transfer.** Otherwise the GPU can deadlock. The context does not check this. A call with more
     *        waits fails at once with TooManyWaits. A wait with a null semaphore or a value of 0 fails with
     *        InvalidWait.
     * @return Future for tracking transfer progress
     */
    [[nodiscard]] rhi::OperationFuture CopyBufferToImage(
        GraphicsResource buffer,
        GraphicsResource image,
        rhi::ImageUsageTransition image_usage,
        std::span<const rhi::TransferWait> waits);

    /**
     * @brief Copy every mip level and layer of one image into another existing image of the same shape.
     *
     * @param src The source image
     * @param src_usage The usage of the source before the copy, and the usage it must have after the copy
     * @param dst The destination image
     * @param dst_usage The usage of the destination before the copy, and the usage it must have after the copy
     * @param waits Up to k_MaxTransferWaits timeline values that the transfer waits for on the GPU. Empty for none.
     *        **The caller must make sure that a submit already signals each value, or will signal it without waiting
     *        for this transfer.** Otherwise the GPU can deadlock. The context does not check this. A call with more
     *        waits fails at once with TooManyWaits. A wait with a null semaphore or a value of 0 fails with
     *        InvalidWait.
     * @return Future for tracking transfer progress
     */
    [[nodiscard]] rhi::OperationFuture CopyImageContents(
        GraphicsResource src,
        rhi::ImageUsageTransition src_usage,
        GraphicsResource dst,
        rhi::ImageUsageTransition dst_usage,
        std::span<const rhi::TransferWait> waits);

    /**
     * @brief Destroy a graphics resource and free its memory, once the GPU is done with it.
     *
     * The destroy records the pending value of every registered consumer timeline, and the last transfer that touches
     * the resource. The objects are freed, and the future completes, once all of those have passed. With nothing to wait
     * for, that happens at once. From the moment the context processes the destroy, every other call that names the
     * resource fails.
     *
     * @param resource The resource to destroy
     * @return Future that completes when the objects are freed
     *
     * @note All associated Vulkan objects (buffer/image, view, sampler) are destroyed, with memory returned to the allocator
     */
    [[nodiscard]] rhi::OperationFuture DestroyResource(
        GraphicsResource resource);

    /**
     * @brief Adds a timeline that destroys wait for, such as the renderer's frame timeline. At most
     * k_MaxConsumerTimelines at once. The future fails when the timeline is already registered or no slot is free.
     */
    [[nodiscard]] rhi::OperationFuture RegisterConsumerTimeline(rhi::TimelineSemaphoreHandle timeline);

    /**
     * @brief Removes a timeline. Call it only after the GPU passed every value you reported for it: resources waiting
     * on it no longer wait. The semaphore may be destroyed once the future completes.
     */
    [[nodiscard]] rhi::OperationFuture UnregisterConsumerTimeline(rhi::TimelineSemaphoreHandle timeline);

    /**
     * @brief The owner's per-frame call. Reports each registered timeline's pending value: the value that the work
     * recorded from now on will signal at the latest. The context reads each registered timeline's counter once, and
     * frees the resources whose waits passed. Call it before recording a frame, so that a destroy issued while
     * recording waits for that frame. Values for timelines that are not registered are ignored, and so is every value
     * after the first k_MaxConsumerTimelines.
     */
    void Tick(std::span<const ConsumerTimelineValue> pending_values);

    /**
     * @brief Blocks until the future is Complete or Failed, or until `timeout_ns` passes. Returns the state at that
     * moment, so any state that is not Complete or Failed means a timeout. Pass rhi::k_WaitForever for no limit. A
     * future that is Created sleeps on the transfer timeline, not in a spin.
     */
    [[nodiscard]] rhi::FutureState Wait(const rhi::OperationFuture& future, uint64_t timeout_ns) const;

    /** @brief Returns the counters. Safe from any thread. */
    [[nodiscard]] ResourceContextCounters Counters() const noexcept;

private:
    // the Vulkan-only placement calls reach the impl through these. see ResourcePlacement.hpp
    friend rhi::placement::MemoryRequirements rhi::placement::GetImageMemoryRequirements(const ResourceContext& context, const ImageCreateInfo& create_info);
    friend rhi::placement::MemoryRequirements rhi::placement::GetBufferMemoryRequirements(const ResourceContext& context, const BufferCreateInfo& create_info);
    friend rhi::ResourceFuture<GraphicsResource> rhi::placement::CreateMemoryHeap(ResourceContext& context, const rhi::placement::MemoryHeapCreateInfo& create_info);
    friend rhi::ResourceFuture<GraphicsResource> rhi::placement::CreatePlacedImage(ResourceContext& context, const ImageCreateInfo& create_info, rhi::placement::Placement placement);
    friend rhi::ResourceFuture<GraphicsResource> rhi::placement::CreatePlacedBuffer(ResourceContext& context, const BufferCreateInfo& create_info, rhi::placement::Placement placement);

    std::unique_ptr<ResourceContextImpl> impl;
};

#endif //!DIAMOND_DOGS_RESOURCE_CONTEXT_HPP
