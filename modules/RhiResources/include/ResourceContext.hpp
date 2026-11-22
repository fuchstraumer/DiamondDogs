#pragma once
#ifndef DIAMOND_DOGS_RESOURCE_CONTEXT_HPP
#define DIAMOND_DOGS_RESOURCE_CONTEXT_HPP
#include "ResourceTypes.hpp"
#include "ResourceFuture.hpp"
#include "RhiHandle.hpp"
#include <array>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <span>
#include <string_view>

namespace rhi
{
    class RhiContext;
}


/** @brief The default size of a context's future table: 4096 slots of 64 bytes, 256 KiB. One slot is shared by the
 *  futures of a full table, so 4095 futures can be live at once. */
constexpr inline int32_t k_DefaultMaxFutures = 4096;

/** @brief The default size of the staging ring in bytes: 64 MiB of host-visible memory, mapped for the life of the context. */
constexpr inline uint64_t k_DefaultStagingRingSize = 64ull * 1024ull * 1024ull;

/** @brief The smallest staging ring that Initialize accepts: 1 MiB. */
constexpr inline uint64_t k_MinStagingRingSize = 1024ull * 1024ull;

/**
 * @brief Configuration structure for initializing a ResourceContext
 * 
 * Chosen so we can expand configuration options in the future without breaking API
 */
struct ResourceContextCreateInfo
{
    const rhi::RhiContext* Context{ nullptr };  ///< The initialized RHI context (Vulkan or WebGPU)
    bool DeviceAddressesEnabled{ false };       ///< Whether this session needs device addresses enabled for buffers
    int32_t MaxFutures{ k_DefaultMaxFutures };  ///< The size of the future table. At least 2
    uint64_t StagingRingSize{ k_DefaultStagingRingSize };  ///< The bytes of the staging ring. At least k_MinStagingRingSize
};

enum class ResourceContextError : uint8_t
{
    Invalid = 0,
    Success = 1,
    AlreadyInitialized = 2,
    /** @brief The create info has no instance or no device, MaxFutures is below 2, or StagingRingSize is below
     *  k_MinStagingRingSize. */
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
    /** @brief Transfers that found the staging ring full and waited for the GPU to free a range. */
    int64_t StagingRingStalls{ 0 };
    /** @brief Uploads that were larger than the staging ring and got a staging buffer of their own. */
    int64_t StagingDedicatedUploads{ 0 };
    /** @brief The bytes of the staging ring that were in use at the last poll of the transfer timeline. */
    int64_t StagingRingBytesInUse{ 0 };
    /** @brief Transfer sets that the transfer thread recorded. Each is also one of TransfersRecorded. */
    int64_t TransferSetsRecorded{ 0 };
    /** @brief Barrier calls (`vkCmdPipelineBarrier2`) that the transfer thread recorded inside transfer sets, between
     *  ranges and around them. The ordering barrier before each transfer is not counted. */
    int64_t TransferBarriersRecorded{ 0 };
};

/** @brief The largest number of memory heaps that a Vulkan device reports (VK_MAX_MEMORY_HEAPS). */
constexpr inline int32_t k_MaxMemoryHeaps = 16;

/** @brief The budget and the usage of one memory heap, in bytes. DeviceLocal is true for a heap of device-local memory. */
struct MemoryHeapBudget
{
    uint64_t BudgetBytes{ 0u };
    uint64_t UsageBytes{ 0u };
    bool DeviceLocal{ false };
};

/**
 * @brief The memory budget that the last Tick published. Each heap's values are read one after another with relaxed
 * loads, so they are not one consistent snapshot across heaps. HeapCount is 0 before the first Tick. FromExtension is
 * true when the usage comes from VK_EXT_memory_budget, and false when VMA estimates it. TickSequence counts the Ticks
 * that published a budget.
 */
struct MemoryBudgetSnapshot
{
    std::array<MemoryHeapBudget, k_MaxMemoryHeaps> Heaps{};
    int32_t HeapCount{ 0 };
    bool FromExtension{ false };
    int64_t TickSequence{ 0 };
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
     * @param initial_data Initial data pieces. Each piece is written at its own `DestinationOffset`. Empty for none.
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
     * @brief Create an image and upload data to it. The transfer system moves the subresources that the pieces touch
     * from UNDEFINED to the layout that `usage_after_upload` needs. The other subresources stay UNDEFINED. The context
     * adds TransferDestination to the create usage.
     *
     * @param create_info Domain, usage, format, extent and optional view and sampler
     * @param initial_data Initial data, one element per box of texels. A bad piece fails the future with InvalidRegion,
     *        as in SetImageData, and no resource is published.
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
     * CPU-accessible buffers, data is written directly to mapped memory. Multiple data pieces can be
     * uploaded in a single operation. Each piece is written at its own `DestinationOffset`, and the bytes between
     * pieces do not change.
     *
     * @param buffer The target buffer resource to update
     * @param data The data pieces to upload. A `DestinationOffset` or a piece size that is not a multiple of 4, a piece
     *        that ends outside the buffer, or two pieces that overlap, fails the whole call with InvalidRegion and
     *        writes nothing.
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
     * @brief Upload data to an existing image: boxes of texels in specific mip levels and array layers. Uses staging
     * buffers. The transfer moves the subresources that the pieces touch from the layout of `usage.Before` to the layout
     * of `usage.After`. The caller knows the last GPU use of those subresources, so the caller gives `Before`. The
     * other mip levels and layers of the image keep their layout.
     *
     * @param image The target image resource to update
     * @param usage The usage of the touched subresources before the transfer, and the usage they must have after it.
     *        `Discard` as `Before` needs every piece to cover its whole mip level: the transfer throws away the old
     *        contents of the subresources that it touches.
     * @param data The pieces to upload. Each piece writes the box that its `X`, `Y`, `Z`, `Width`, `Height` and
     *        `Depth` give, in its mip level and layers. A piece whose box is outside the image, whose `Data` has fewer
     *        bytes than the box needs, or that is off the block edges of a block-compressed format fails the whole call
     *        with InvalidRegion, and nothing is written.
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
     * @brief Copy the contents of one buffer to another existing buffer. The entire source is copied, from offset 0 of
     * the source to offset 0 of the destination. This is CopyBufferRegions with the one region that covers the source.
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
     * @brief Copy byte ranges of one buffer to another existing buffer, or to other places in the same buffer. All
     * regions go on one transfer, so the call has one ticket.
     *
     * @param src The source buffer to copy from
     * @param dst The destination buffer to copy to. It can be `src`, when no source range overlaps a destination range.
     * @param regions From 1 to k_MaxTransferRegions regions. Every offset and size must be a multiple of 4, every size
     *        must be above 0, and every range must be inside its buffer. A bad region fails the whole call with
     *        InvalidRegion, and nothing is copied. A call with no region, or with more than k_MaxTransferRegions
     *        regions, fails at once on the calling thread. The context copies the regions before the call returns.
     * @param waits Up to k_MaxTransferWaits timeline values that the transfer waits for on the GPU. Empty for none.
     *        **The caller must make sure that a submit already signals each value, or will signal it without waiting
     *        for this transfer.** Otherwise the GPU can deadlock. The context does not check this. A call with more
     *        waits fails at once with TooManyWaits. A wait with a null semaphore or a value of 0 fails with
     *        InvalidWait.
     * @return Future for tracking transfer progress. Fails with WrongResourceKind when either side is an image. A
     *         host-visible destination still goes through the transfer queue, because the source is on the GPU.
     */
    [[nodiscard]] rhi::OperationFuture CopyBufferRegions(
        GraphicsResource src,
        GraphicsResource dst,
        std::span<const BufferCopyRegion> regions,
        std::span<const rhi::TransferWait> waits);

    /**
     * @brief Copy mip 0 of every layer of an image into a buffer, tightly packed from offset 0 (the readback path).
     * The image needs TransferSource usage and the buffer TransferDestination usage. This is CopyImageToBufferRegions
     * with the one region that covers mip 0 of every layer. It does not check that the buffer is large enough.
     *
     * @param image The source image
     * @param image_usage The usage of mip 0 before the copy, and the usage it must have after the copy. The other mip
     *        levels keep their layout.
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
     * @brief Copy a buffer into mip 0 of every layer of an image, tightly packed from offset 0. The buffer needs
     * TransferSource usage and the image TransferDestination usage. This is CopyBufferToImageRegions with the one
     * region that covers mip 0 of every layer. It does not check that the buffer is large enough.
     *
     * @param buffer The source buffer
     * @param image The destination image
     * @param image_usage The usage of mip 0 before the copy, and the usage it must have after the copy. The other mip
     *        levels keep their layout.
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
     * @brief Copy every mip level and layer of one image into another existing image of the same shape. This is
     * CopyImageRegions with one region for each mip level.
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
     * @brief Copy boxes of texels from a buffer into an image, all on one transfer, so the call has one ticket. The
     * buffer needs TransferSource usage and the image TransferDestination usage.
     *
     * @param buffer The source buffer
     * @param image The destination image
     * @param image_usage The usage of the subresources that the regions touch, before the copy, and the usage they must
     *        have after it. The other mip levels and layers keep their layout. `Discard` as `Before` needs every
     *        region to cover its whole mip level, because the transfer throws away the old contents of the touched
     *        subresources.
     * @param regions From 1 to k_MaxTransferRegions regions. A bad region fails the whole call with InvalidRegion, and
     *        nothing is copied. The causes are listed with ResourceError::InvalidRegion. A call with no region, or with
     *        more than k_MaxTransferRegions regions, fails at once on the calling thread. The context copies the regions
     *        before the call returns.
     * @param waits Up to k_MaxTransferWaits timeline values that the transfer waits for on the GPU. Empty for none.
     *        **The caller must make sure that a submit already signals each value, or will signal it without waiting
     *        for this transfer.** Otherwise the GPU can deadlock. The context does not check this. A call with more
     *        waits fails at once with TooManyWaits. A wait with a null semaphore or a value of 0 fails with
     *        InvalidWait.
     * @return Future for tracking transfer progress
     */
    [[nodiscard]] rhi::OperationFuture CopyBufferToImageRegions(
        GraphicsResource buffer,
        GraphicsResource image,
        rhi::ImageUsageTransition image_usage,
        std::span<const BufferImageRegion> regions,
        std::span<const rhi::TransferWait> waits);

    /**
     * @brief Copy boxes of texels from an image into a buffer, all on one transfer, so the call has one ticket. The
     * image needs TransferSource usage and the buffer TransferDestination usage. A depth and stencil image copies its
     * depth aspect.
     *
     * @param image The source image
     * @param image_usage The usage of the subresources that the regions touch, before the copy, and the usage they must
     *        have after it. The other mip levels and layers keep their layout.
     * @param buffer The destination buffer
     * @param regions From 1 to k_MaxTransferRegions regions. A bad region fails the whole call with InvalidRegion, and
     *        nothing is copied. A region of a mip level that the image does not have, or whose texels do not fit in
     *        the buffer, is bad. The context copies the regions before the call returns.
     * @param waits Up to k_MaxTransferWaits timeline values that the transfer waits for on the GPU. Empty for none.
     *        **The caller must make sure that a submit already signals each value, or will signal it without waiting
     *        for this transfer.** Otherwise the GPU can deadlock. The context does not check this. A call with more
     *        waits fails at once with TooManyWaits. A wait with a null semaphore or a value of 0 fails with
     *        InvalidWait.
     * @return Future for tracking transfer progress
     */
    [[nodiscard]] rhi::OperationFuture CopyImageToBufferRegions(
        GraphicsResource image,
        rhi::ImageUsageTransition image_usage,
        GraphicsResource buffer,
        std::span<const BufferImageRegion> regions,
        std::span<const rhi::TransferWait> waits);

    /**
     * @brief Copy boxes of texels from one image to another, all on one transfer, so the call has one ticket. Both
     * images need the same aspect and the same texel size. A copy of a compressed format needs a compressed format on
     * both sides.
     *
     * @param src The source image
     * @param src_usage The usage of the subresources that the regions read, before the copy, and the usage they must
     *        have after it
     * @param dst The destination image. It can be `src`, when no two regions touch the same texels of the same
     *        subresource, and `src_usage` equals `dst_usage`. The copy then runs in the GENERAL layout.
     * @param dst_usage The usage of the subresources that the regions write, before the copy, and the usage they must
     *        have after it. `Discard` as `Before` needs every region to cover its whole mip level.
     * @param regions From 1 to k_MaxTransferRegions regions. A bad region fails the whole call with InvalidRegion, and
     *        nothing is copied. A pair of images with a different aspect or texel size fails with CopyShapeMismatch.
     *        A call with no region, or with more than k_MaxTransferRegions regions, fails at once on the calling
     *        thread. The context copies the regions before the call returns.
     * @param waits Up to k_MaxTransferWaits timeline values that the transfer waits for on the GPU. Empty for none.
     *        **The caller must make sure that a submit already signals each value, or will signal it without waiting
     *        for this transfer.** Otherwise the GPU can deadlock. The context does not check this. A call with more
     *        waits fails at once with TooManyWaits. A wait with a null semaphore or a value of 0 fails with
     *        InvalidWait.
     * @return Future for tracking transfer progress
     */
    [[nodiscard]] rhi::OperationFuture CopyImageRegions(
        GraphicsResource src,
        rhi::ImageUsageTransition src_usage,
        GraphicsResource dst,
        rhi::ImageUsageTransition dst_usage,
        std::span<const ImageCopyRegion> regions,
        std::span<const rhi::TransferWait> waits);

    /**
     * @brief Submits many transfers that the caller already ordered, as one transfer: one message, one future, one
     * ticket, one copy of the data, and a barrier before and after each range instead of one for each op. Every
     * resource that an op touches gets the ticket of the set, so a destroy waits for the whole set.
     *
     * @param set The ops, the range ends and the bytes of the uploads. The context copies all of it before the call
     *        returns. A set with no op, or with more than k_MaxTransferSetOps ops, or with range ends that are empty,
     *        that do not rise, or whose last value is not the op count, fails at once on the calling thread with
     *        InvalidTransferSet.
     * @param waits Up to k_MaxTransferWaits timeline values that the set waits for on the GPU. Empty for none. A set
     *        with waits takes a batch of its own, as a single transfer with waits does, and a set without waits shares
     *        the open batch. **The caller must make sure that a submit already signals each value, or will signal it
     *        without waiting for this set.** Otherwise the GPU can deadlock. The context does not check this. A call
     *        with more waits fails at once with TooManyWaits. A wait with a null semaphore or a value of 0 fails with
     *        InvalidWait.
     * @return Future for tracking the set. The worker checks every op before it issues the ticket, with the rules of
     *         the single call for that op. One bad op fails the whole set with the error of the first bad op, and
     *         nothing is recorded. Sets are device side: a destination in host-visible memory fails with
     *         InvalidTransferSet, and host data goes through SetBufferData. In a debug build, two ops of one range that
     *         write one resource, or that read and write one resource, fail with RangeHazard.
     */
    [[nodiscard]] rhi::OperationFuture SubmitTransferSet(
        const TransferSetDesc& set,
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
     * @brief As DestroyResource(resource), and the resource is also not freed before `after` passes on the GPU. Use it
     * when the last GPU use is on a timeline that is not registered, or a submit that Tick has not reported yet.
     *
     * @param resource The resource to destroy
     * @param after A timeline and a value. The resource is freed once the timeline counter is at least the value. A
     *        null semaphore or a value of 0 fails the future with InvalidWait, and the resource is not destroyed. The
     *        semaphore must live until the future completes. A value that no submit will signal keeps the resource
     *        until the context shuts down.
     * @return Future that completes when the objects are freed
     */
    [[nodiscard]] rhi::OperationFuture DestroyResource(
        GraphicsResource resource,
        rhi::GpuWait after);

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

    /**
     * @brief Returns the memory budget that the last Tick published. Safe from any thread, with no lock and no blocking
     * call. The worker publishes it once per Tick, because the allocator is externally synchronized and only the worker
     * may query it.
     */
    [[nodiscard]] MemoryBudgetSnapshot MemoryBudgets() const noexcept;

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
