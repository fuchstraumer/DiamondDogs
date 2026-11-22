#include "ResourceContextImpl.hpp"
#include "ImageDataFormatsVulkan.hpp"

#include "ResourceContext.hpp"
#include "Device.hpp"
#include "Instance.hpp"
#include "RhiResourceFlags.hpp"
#include "FutureTable.hpp"
#include "ResourceFuture.hpp"
#include "ResourceMessageTypesInternal.hpp"
#include "ResourceTypes.hpp"
#include "RhiFlags.hpp"
#include "RhiSystem.hpp"
#include "UploadBuffer.hpp"
#include "VulkanConversions.hpp"
#include "entt/entity/fwd.hpp"
#include "vulkan/vulkan_core.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <span>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wnullability-completeness"
#pragma clang diagnostic ignored "-Wunused-private-field"
#endif
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

static_assert(static_cast<uint32_t>(k_MaxMemoryHeaps) == VK_MAX_MEMORY_HEAPS, "k_MaxMemoryHeaps must match the Vulkan limit");

namespace
{
    [[nodiscard]] bool HasFlag(ResourceCreationFlags flags, ResourceCreationFlags flag) noexcept;
    /** @brief The description builders take the create info that the worker used, after it added any usage bits. */
    [[nodiscard]] ResourceDescription DescribeBuffer(const BufferCreateInfo& info) noexcept;
    [[nodiscard]] ResourceDescription DescribeImage(const ImageCreateInfo& info, ResourceType type, ImageUsageBits initial_usage) noexcept;
    [[nodiscard]] ResourceDescription DescribeSampler() noexcept;
    [[nodiscard]] ResourceDescription DescribeHeap(uint64_t byte_size) noexcept;
    // transfer usage is opt-in (TransferSource/TransferDestination), so every operation
    // that records a transfer checks it here, at the surface, and fails the reply instead of recording invalid commands
    [[nodiscard]] bool CanBeTransferSource(const entt::registry& registry, entt::entity entity) noexcept;
    [[nodiscard]] bool CanBeTransferDestination(const entt::registry& registry, entt::entity entity) noexcept;
    /** @brief Image to image copies need the same aspect, extent, layers and mips (for now). */
    [[nodiscard]] bool ImagesHaveTheSameShape(const ImageCreateInfo& src, const ImageCreateInfo& dst) noexcept;
    /** @brief Returns true when `usage` is non-zero and every bit of it is in the usage that the image was created with. */
    [[nodiscard]] bool UsageIsValidForImage(ImageUsageBits image_usage, ImageUsageBits usage) noexcept;
    /** @brief Returns true when both usages are valid for the image. Used for an image that a transfer reads, whose
     *  contents must be defined. */
    [[nodiscard]] bool TransitionIsValidForImage(ImageUsageBits image_usage, rhi::ImageUsageTransition transition) noexcept;
    /** @brief As TransitionIsValidForImage, but Before can also be Discard alone, because the transfer writes the image. */
    [[nodiscard]] bool DestinationTransitionIsValidForImage(ImageUsageBits image_usage, rhi::ImageUsageTransition transition) noexcept;
    /** @brief Returns the layout that an image is in when a transfer starts, from the usage the caller gave as Before.
     *  This is the only place that derives it. Discard gives UNDEFINED. */
    [[nodiscard]] VkImageLayout LayoutBeforeTransfer(ImageUsageBits before) noexcept;
    /** @brief Returns true when every wait has a semaphore and a non-zero value. */
    [[nodiscard]] bool WaitsAreValid(const TransferWaitSet& waits) noexcept;
    /** @brief Returns true when the copy kind matches the kinds of the two resources. */
    [[nodiscard]] bool CopyKindMatches(CopyKind kind, bool src_is_buffer, bool dst_is_buffer) noexcept;
    /** @brief Returns true when the range lies inside a buffer of `buffer_size` bytes. It compares against the size that
     *  is left over, so that offset + size cannot overflow. */
    [[nodiscard]] bool RangeIsInside(uint64_t offset, uint64_t size, uint64_t buffer_size) noexcept;
    /** @brief Returns true when both ranges have bytes, and share at least one. Both ranges must be inside a buffer. */
    [[nodiscard]] bool RangesOverlap(uint64_t first_offset, uint64_t first_size, uint64_t second_offset, uint64_t second_size) noexcept;
    /** @brief Returns true when every region is acceptable: a size above 0, offsets and sizes that are multiples of 4,
     *  and both ranges inside their buffers. For a copy inside one buffer, no source range may overlap a destination
     *  range, and no two destination ranges may overlap. */
    [[nodiscard]] bool BufferCopyRegionsAreValid(std::span<const BufferCopyRegion> regions, uint64_t source_size, uint64_t destination_size, bool is_same_buffer) noexcept;
    /** @brief Returns true when every upload piece has an offset and a size that are multiples of 4, ends inside the
     *  buffer, and overlaps no other piece in the destination. */
    [[nodiscard]] bool BufferUploadPiecesAreValid(const InternalResourceDataContainer& data, uint64_t buffer_size) noexcept;
    /** @brief The extent of one mip level of an image, in texels. */
    struct MipExtent
    {
        int32_t Width{ 0 };
        int32_t Height{ 0 };
        int32_t Depth{ 0 };
    };
    /** @brief Returns the extent of the mip level. Only a 3D image has a depth above 1. */
    [[nodiscard]] MipExtent MipExtentOf(const ImageCreateInfo& info, int32_t mip_level) noexcept;
    /** @brief Returns true when the region has an extent and a layer count above 0, a mip level and layers that the
     *  image has, and a box inside the mip level. For a block-compressed format, the box must start on a block edge,
     *  and end on a block edge or on the edge of the mip level. */
    [[nodiscard]] bool ImageRegionIsValid(const ImageRegion& region, const ImageCreateInfo& info) noexcept;
    /** @brief Returns true when the box covers every texel of its mip level. */
    [[nodiscard]] bool RegionCoversWholeMip(const ImageRegion& region, const ImageCreateInfo& info) noexcept;
    /** @brief Returns true when both regions touch a texel of the same subresource: one mip level, one layer. */
    [[nodiscard]] bool ImageBoxesOverlap(const ImageRegion& first, const ImageRegion& second) noexcept;
    /** @brief Returns the product, or nullopt when it overflows 64 bits. */
    [[nodiscard]] std::optional<uint64_t> CheckedProduct(uint64_t first, uint64_t second) noexcept;
    /** @brief Returns the bytes from the buffer offset of the region to the end of its last texel block, or nullopt when
     *  the arithmetic overflows. The region must have passed ImageRegionIsValid. */
    [[nodiscard]] std::optional<uint64_t> BufferImageFootprintBytes(const BufferImageRegion& region, const ImageFormat& format) noexcept;
    /** @brief Returns true when the buffer side of the region is valid: an offset that is a multiple of the staging
     *  alignment, row and image heights that are 0 or large enough and a multiple of the block size, and, when a buffer
     *  size is given, a footprint inside the buffer. */
    [[nodiscard]] bool BufferImageRegionIsValid(const BufferImageRegion& region, const ImageCreateInfo& info, std::optional<uint64_t> checked_buffer_size) noexcept;
    /** @brief Returns true when every region is valid, and no region is outside the buffer when a size is given. */
    [[nodiscard]] bool ImageToBufferRegionsAreValid(std::span<const BufferImageRegion> regions, const ImageCreateInfo& info, std::optional<uint64_t> checked_buffer_size) noexcept;
    /** @brief As ImageToBufferRegionsAreValid, and also no two boxes of the image overlap. When the image is discarded,
     *  every box must cover its whole mip level. */
    [[nodiscard]] bool BufferToImageRegionsAreValid(std::span<const BufferImageRegion> regions, const ImageCreateInfo& info, std::optional<uint64_t> checked_buffer_size, bool image_is_discarded) noexcept;
    /** @brief Returns the destination box of an image copy region: the extent and layer count of the source at the
     *  mip level, layers and offset of the destination. */
    [[nodiscard]] ImageRegion DestinationBoxOf(const ImageCopyRegion& region) noexcept;
    /** @brief Returns true when the two images have the same aspect, the same texel size and the same compression. */
    [[nodiscard]] bool ImagesAreCopyCompatible(const ImageCreateInfo& src, const ImageCreateInfo& dst) noexcept;
    /** @brief Returns true when every region is valid on both sides, and no two destination boxes overlap. For a copy
     *  inside one image, no source box overlaps a destination box either. When the destination is discarded, every
     *  destination box must cover its whole mip level. */
    [[nodiscard]] bool ImageCopyRegionsAreValid(std::span<const ImageCopyRegion> regions, const ImageCreateInfo& src_info, const ImageCreateInfo& dst_info, bool is_same_image, bool destination_is_discarded) noexcept;
    /** @brief Sets the depth of each upload piece that has a depth of 0 to the depth of its mip level. */
    void SetUploadPieceDepths(InternalResourceDataContainer& data, const ImageCreateInfo& info) noexcept;
    /** @brief Returns true when there is at least one piece, every piece is valid and has the bytes that its box needs,
     *  and no two pieces overlap. When the image is discarded, every piece must cover its whole mip level. */
    [[nodiscard]] bool ImageUploadPiecesAreValid(const InternalResourceDataContainer& data, const ImageCreateInfo& info, bool image_is_discarded) noexcept;
    /** @brief Returns the one region that copies mip 0 of every layer, tightly packed from buffer offset 0. */
    [[nodiscard]] BufferImageRegionBlock MakeMipZeroRegionBlock(const ImageCreateInfo& info);
    /** @brief Returns one region for each mip level of the image, each with every layer. */
    [[nodiscard]] ImageCopyRegionBlock MakeEveryMipRegionBlock(const ImageCreateInfo& info);
    /** @brief Returns true when a resource with these requirements fits at the offset: aligned, inside the heap, and
     *  of a memory type that the resource allows. */
    [[nodiscard]] bool PlacementFits(const VkMemoryRequirements& requirements, uint64_t offset, uint64_t heap_size, uint32_t heap_memory_type_index) noexcept;

    // the buffer offset alignment that a transfer set needs at least: the one that a buffer upload has
    constexpr uint64_t k_SetMinimumStagingAlignment = 16u;

    // the range hazard validator is for debug builds: a check that runs in shipped code on every set is a cost paid always
#ifndef NDEBUG
    constexpr bool k_RangeHazardValidatorEnabled = true;
#else
    constexpr bool k_RangeHazardValidatorEnabled = false;
#endif

    /** @brief One read or write of a resource by an op of a transfer set. */
    struct RangeAccess
    {
        uint32_t Entity{ 0u };
        bool IsWrite{ false };
    };

    /** @brief Returns every aspect that the format of the image has. A barrier lists all of them. */
    [[nodiscard]] VkImageAspectFlags BarrierAspectOf(const ImageCreateInfo& info) noexcept;
    /** @brief Returns true for the kinds whose `Source` is a resource that the op reads. The upload kinds read the data of the set. */
    [[nodiscard]] bool TransferOpReadsSource(TransferOpKind kind) noexcept;
    /** @brief Returns true when the alternative that `Region` holds is the one that the kind of the op reads. Returns
     *  false for the invalid kind. Every read of `Region` comes after this check. */
    [[nodiscard]] bool TransferOpRegionMatchesKind(const TransferSetOp& op) noexcept;
    /** @brief Returns true when two ops of one range write one resource, or when one resource is both read and written
     *  in one range. Sorts at most 2 entries for each op, in a fixed array. Allocates nothing. */
    [[nodiscard]] bool TransferSetHasRangeHazard(std::span<const TransferSetOp> ops, std::span<const int32_t> range_ends) noexcept;
    /** @brief Writes the entity of every resource that an op reads or writes into `out`, which needs 2 entries for each
     *  op. An entity can appear more than once. Returns the count. */
    [[nodiscard]] size_t CollectTouchedEntities(std::span<const TransferSetOp> ops, std::span<entt::entity> out) noexcept;
    /** @brief Returns true when an op of the set uploads bytes of the data of the set. */
    [[nodiscard]] bool TransferSetUploads(std::span<const TransferSetOp> ops) noexcept;
    /** @brief Returns the alignment that the start of the staged data needs, so that the buffer offset of every image
     *  upload keeps the alignment of its format. At least 16, as a buffer upload has. */
    [[nodiscard]] uint64_t StagingAlignmentOfSet(const entt::registry& registry, std::span<const TransferSetOp> ops) noexcept;
}

ResourceContextError ResourceContextImpl::Construct(const ResourceContextCreateInfo& create_info)
{
    if (device != nullptr)
    {
        return ResourceContextError::AlreadyInitialized;
    }

    // one slot is shared by the futures of a full table, so a table of one slot could hold no future
    if (create_info.Instance == nullptr || create_info.Device == nullptr || create_info.MaxFutures < 2 ||
        create_info.StagingRingSize < k_MinStagingRingSize)
    {
        return ResourceContextError::InvalidCreateInfo;
    }

    queueFamilyIndices.clear();
    const rhi::QueueFamilyIndices& families = create_info.Device->GetQueueFamilyIndices();
    for (const uint32_t family : { families.Graphics, families.Compute, families.Transfer })
    {
        if (family != ~0u && std::ranges::find(queueFamilyIndices, family) == queueFamilyIndices.end())
        {
            queueFamilyIndices.push_back(family);
        }
    }

    const VkInstance instance = create_info.Instance->Handle().As<VkInstance>();
    VmaAllocatorCreateInfo createInfo{};
    // only the worker thread allocates, so the library does not need to sync for us
    createInfo.flags = VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT;
    if (create_info.DeviceAddressesEnabled)
    {
        createInfo.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    }

    memoryBudgetExtensionEnabled = create_info.Device->HasExtension("VK_EXT_memory_budget");
    if (memoryBudgetExtensionEnabled)
    {
        createInfo.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
    }

    createInfo.physicalDevice = create_info.Device->GetPhysicalDevice().As<VkPhysicalDevice>();
    createInfo.device = create_info.Device->Handle().As<VkDevice>();
    createInfo.instance = instance;
    createInfo.vulkanApiVersion = create_info.VulkanApiVersion;

    VkResult result = vmaCreateAllocator(&createInfo, &allocatorHandle);
    if (result != VK_SUCCESS)
    {
        allocatorHandle = VK_NULL_HANDLE;
        return ResourceContextError::AllocatorCreationFailed;
    }

    const ResourceContextError transferResult = transferSystem.Initialize(create_info.Device, instance, create_info.VulkanApiVersion, &messageQueue, &futures, create_info.StagingRingSize);
    if (transferResult != ResourceContextError::Success)
    {
        vmaDestroyAllocator(allocatorHandle);
        allocatorHandle = VK_NULL_HANDLE;
        return transferResult;
    }

    // no message exists yet, so neither thread writes the table before this
    const VkSemaphore transferTimeline = reinterpret_cast<VkSemaphore>(transferSystem.TimelineHandle());
    futures.Initialize(create_info.MaxFutures, create_info.Device->Handle().As<VkDevice>(), transferTimeline);

    // set last: Destroy() treats a null device as "never initialized"
    device = create_info.Device;
    validationEnabled = create_info.ValidationEnabled;
    deviceAddressesEnabled = create_info.DeviceAddressesEnabled;
    StartWorker();
    return ResourceContextError::Success;
}

void ResourceContextImpl::Destroy()
{
    if (device == nullptr)
    {
        return;
    }

    SetExitWorker();

    // worker stopped, now it's this threads job to drain the queue
    while (true)
    {
        std::optional<ResourceMessagePayloadType> message = messageQueue.try_pop();
        if (!message.has_value())
        {
            break;
        }
        dispatchMessage(std::move(*message));
        retireReady();
    }

    // destroy transfer system, which may have pending resources and transfers
    transferSystem.destroy();

    // the owner waits for its own frames before it destroys the context, and the transfer queue is idle now
    for (RetiringResource& entry : retiring)
    {
        retire(entry);
    }

    retiring.clear();
    retirementsPending.store(0, std::memory_order_relaxed);

    // now traverse the registry for live entities and destroy their resources
    // heaps go last: a heap must outlive the placed resources that are bound into it
    std::vector<entt::entity> liveEntities;
    std::vector<entt::entity> liveHeaps;
    for (const entt::entity entity : resourceRegistry.view<ResourceFlags>())
    {
        std::vector<entt::entity>& destination = resourceRegistry.all_of<MemoryHeap>(entity) ? liveHeaps : liveEntities;
        destination.push_back(entity);
    }

    for (const entt::entity entity : liveEntities)
    {
        destroyEntityResources(entity);
    }

    for (const entt::entity entity : liveHeaps)
    {
        destroyEntityResources(entity);
    }

    // last step, destroy the allocator and the registry. allocator last
    resourceRegistry.clear();
    vmaDestroyAllocator(allocatorHandle);
    allocatorHandle = VK_NULL_HANDLE;
    device = nullptr;
}

void ResourceContextImpl::PushMessage(ResourceMessagePayloadType message)
{
    messageQueue.push(std::move(message));
}

void ResourceContextImpl::SetExitWorker()
{
    if (workerThread.joinable())
    {
        // messages pushed before this are processed by worker, any pushed after it by the drain in destroy()
        messageQueue.push(ExitWorkerMessage{});
        workerThread.join();
    }
}

void ResourceContextImpl::StartWorker()
{
    workerThread = std::thread(&ResourceContextImpl::processMessages, this);
}

rhi::FutureTable& ResourceContextImpl::Futures() noexcept
{
    return futures;
}


VkDevice ResourceContextImpl::VulkanDevice() const noexcept
{
    return device->Handle().As<VkDevice>();
}

std::span<const uint32_t> ResourceContextImpl::QueueFamilies() const noexcept
{
    return queueFamilyIndices;
}

ResourceContextCounters ResourceContextImpl::Counters() const noexcept
{
    const TransferSystemCounters transfer = transferSystem.Counters();
    return ResourceContextCounters
    {
        messagesProcessed.load(std::memory_order_relaxed),
        transfer.TransfersRecorded,
        transfer.TransfersFailed,
        transfer.BatchesSubmitted,
        transfer.CompletionReads,
        consumerTimelineReads.load(std::memory_order_relaxed),
        transfer.BytesStaged,
        resourcesRetired.load(std::memory_order_relaxed),
        retirementsPending.load(std::memory_order_relaxed),
        messageQueue.BlockedPushCount(),
        transfer.BlockedPushes,
        futures.LiveCount(),
        futures.FullCount(),
        transfer.StagingRingStalls,
        transfer.StagingDedicatedUploads,
        transfer.StagingRingBytesInUse,
        transfer.TransferSetsRecorded,
        transfer.BarriersRecorded
    };
}

MemoryBudgetSnapshot ResourceContextImpl::MemoryBudgets() const noexcept
{
    // acquire pairs with the release in publishMemoryBudgets: the values read below are from that publish or a newer one
    MemoryBudgetSnapshot snapshot{};
    snapshot.TickSequence = memoryBudgetSequence.load(std::memory_order_acquire);
    snapshot.HeapCount = memoryHeapCount.load(std::memory_order_relaxed);
    snapshot.FromExtension = memoryBudgetFromExtension.load(std::memory_order_relaxed);

    for (int32_t heapIndex = 0; heapIndex < snapshot.HeapCount; ++heapIndex)
    {
        const size_t index = static_cast<size_t>(heapIndex);
        snapshot.Heaps[index].BudgetBytes = heapBudgets[index].load(std::memory_order_relaxed);
        snapshot.Heaps[index].UsageBytes = heapUsages[index].load(std::memory_order_relaxed);
        snapshot.Heaps[index].DeviceLocal = heapsAreDeviceLocal[index].load(std::memory_order_relaxed);
    }

    return snapshot;
}

void ResourceContextImpl::processMessages()
{
    // fixed: we used to use an exponential backoff sleeper, which was totally pointless since mwsr queue
    // blocks on pop(). we now use `ExitWorkerMessage` instead of a flag to handle worker exit more cleanly
    while (true)
    {
        ResourceMessagePayloadType message = messageQueue.pop();
        if (std::holds_alternative<ExitWorkerMessage>(message))
        {
            return;
        }
        dispatchMessage(std::move(message));
        retireReady();
    }
}

void ResourceContextImpl::dispatchMessage(ResourceMessagePayloadType&& message)
{
    std::visit([this](auto&& alternative) { processMessage(std::forward<decltype(alternative)>(alternative)); }, std::move(message));
    messagesProcessed.fetch_add(1, std::memory_order_relaxed);
}

void ResourceContextImpl::processMessage(CreateBufferMessage&& message)
{
    // a staged upload needs the destination bit. host-visible domains are written directly and never stage
    if (message.InitialData.has_value() && !DomainIsHostVisible(message.BufferInfo.Domain))
    {
        message.BufferInfo.Usage |= BufferUsageBits::TransferDestination;
    }

    const std::expected<GraphicsResource, rhi::ResourceError> createdResource = createBufferEntity(message.BufferInfo, message.Name);
    if (!createdResource.has_value())
    {
        futures.Fail(message.Future, createdResource.error());
        return;
    }

    const ResourceDescription description = DescribeBuffer(message.BufferInfo);
    const entt::entity newEntity = entt::entity(createdResource->EntityHandle);

    // before anything is written or staged: a bad piece would write outside the buffer
    if (message.InitialData.has_value() && !BufferUploadPiecesAreValid(*message.InitialData, message.BufferInfo.Size))
    {
        destroyEntityResources(newEntity);
        futures.Fail(message.Future, rhi::ResourceError::InvalidRegion);
        return;
    }

    if (message.InitialData.has_value() && !allocationIsHostVisible(newEntity))
    {
        // pass responsiblity on to transfer system now, transferring ownership of data with a move
        futures.StoreResource(message.Future, *createdResource, description);
        enqueueBufferUpload(newEntity, std::move(*message.InitialData), TransferWaitSet{}, message.Future);
        return;
    }

    if (message.InitialData.has_value())
    {
        setBufferDataHostOnly(newEntity, *message.InitialData);
    }

    futures.CompleteWithResource(message.Future, *createdResource, description);
}

void ResourceContextImpl::processMessage(CreateImageMessage&& message)
{
    if (message.InitialData.has_value())
    {
        message.ImageInfo.Usage |= ImageUsageBits::TransferDestination;
    }

    // a discard is a transition word, and no image has it as a usage
    const bool createUsesDiscard = (message.ImageInfo.Usage & ImageUsageBits::Discard) != ImageUsageBits::Invalid ||
                                   (message.InitialData.has_value() && (message.UsageAfterUpload & ImageUsageBits::Discard) != ImageUsageBits::Invalid);
    if (createUsesDiscard)
    {
        futures.Fail(message.Future, rhi::ResourceError::InvalidCreateInfo);
        return;
    }

    // before the image exists, so that a bad usage leaves nothing to clean up
    if (message.InitialData.has_value() && !UsageIsValidForImage(message.ImageInfo.Usage, message.UsageAfterUpload))
    {
        futures.Fail(message.Future, rhi::ResourceError::InvalidUsageTransition);
        return;
    }

    const std::expected<GraphicsResource, rhi::ResourceError> createdResource = createImageEntity(message.ImageInfo, message.Name);
    if (!createdResource.has_value())
    {
        futures.Fail(message.Future, createdResource.error());
        return;
    }

    if (message.InitialData.has_value())
    {
        const entt::entity newEntity = entt::entity(createdResource->EntityHandle);

        // the image is new, so its old contents do not matter and a piece may cover part of a mip level
        SetUploadPieceDepths(*message.InitialData, message.ImageInfo);
        if (!ImageUploadPiecesAreValid(*message.InitialData, message.ImageInfo, false))
        {
            destroyEntityResources(newEntity);
            futures.Fail(message.Future, rhi::ResourceError::InvalidRegion);
            return;
        }

        const ResourceDescription uploadDescription = DescribeImage(message.ImageInfo, createdResource->Type, message.UsageAfterUpload);
        futures.StoreResource(message.Future, *createdResource, uploadDescription);
        const TransferSystemReqImageInfo imageInfo = newImageTransferInfo(newEntity, message.UsageAfterUpload);
        enqueueImageUpload(imageInfo, newEntity, std::move(*message.InitialData), TransferWaitSet{}, message.Future);
        return;
    }

    const ResourceDescription description = DescribeImage(message.ImageInfo, createdResource->Type, ImageUsageBits::Invalid);
    futures.CompleteWithResource(message.Future, *createdResource, description);
}

std::expected<GraphicsResource, rhi::ResourceError> ResourceContextImpl::createBufferEntity(const BufferCreateInfo& info, const DebugName& name)
{
    const std::expected<entt::entity, rhi::ResourceError> begun = beginBufferEntity(info, name);
    if (!begun.has_value())
    {
        return std::unexpected(begun.error());
    }

    const BufferCreateInfo& storedInfo = resourceRegistry.get<BufferCreateInfo>(*begun);
    VkBuffer bufferHandle = createBuffer(*begun, storedInfo);
    if (bufferHandle == VK_NULL_HANDLE)
    {
        destroyEntityResources(*begun);
        return std::unexpected(rhi::ResourceError::AllocationFailed);
    }

    return finishBufferEntity(*begun);
}

std::expected<entt::entity, rhi::ResourceError> ResourceContextImpl::beginBufferEntity(const BufferCreateInfo& info, const DebugName& name)
{
    if (info.Domain == ResourceDomain::Invalid || info.Size == 0u)
    {
        return std::unexpected(rhi::ResourceError::InvalidCreateInfo);
    }

    if (!BufferUsageIsSupported(info.Usage, deviceAddressesEnabled))
    {
        return std::unexpected(rhi::ResourceError::UnsupportedUsage);
    }

    const entt::entity newEntity = resourceRegistry.create();
    resourceRegistry.emplace<ResourceFlags>(newEntity, ResourceType::Buffer, info.Flags, info.Domain);
    resourceRegistry.emplace<BufferCreateInfo>(newEntity, info);
    if (!name.Empty())
    {
        resourceRegistry.emplace<DebugName>(newEntity, name);
    }

    return newEntity;
}

std::expected<GraphicsResource, rhi::ResourceError> ResourceContextImpl::finishBufferEntity(entt::entity newEntity)
{
    const BufferCreateInfo& storedInfo = resourceRegistry.get<BufferCreateInfo>(newEntity);
    const VkBuffer bufferHandle = resourceRegistry.get<VkBuffer>(newEntity);

    VkBufferView bufferView = VK_NULL_HANDLE;
    if (storedInfo.ViewFormat.has_value())
    {
        bufferView = createBufferView(newEntity, storedInfo);
        if (bufferView == VK_NULL_HANDLE)
        {
            destroyEntityResources(newEntity);
            return std::unexpected(rhi::ResourceError::ViewCreationFailed);
        }
    }
    
    uint64_t deviceAddress = 0u;
    if ((storedInfo.Usage & BufferUsageBits::DeviceAddress) != BufferUsageBits::Invalid)
    {
        const VkBufferDeviceAddressInfo addressInfo
        {
            VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
            nullptr,
            bufferHandle
        };
        deviceAddress = vkGetBufferDeviceAddress(device->Handle().As<VkDevice>(), &addressInfo);
    }

    return GraphicsResource
    {
        storedInfo.Domain,
        ResourceType::Buffer,
        static_cast<uint32_t>(newEntity),
        reinterpret_cast<uint64_t>(bufferHandle),
        reinterpret_cast<uint64_t>(bufferView),
        0u,
        deviceAddress
    };
}

std::expected<GraphicsResource, rhi::ResourceError> ResourceContextImpl::createImageEntity(const ImageCreateInfo& info, const DebugName& name)
{
    const std::expected<entt::entity, rhi::ResourceError> begun = beginImageEntity(info, name);
    if (!begun.has_value())
    {
        return std::unexpected(begun.error());
    }

    const ImageCreateInfo& storedInfo = resourceRegistry.get<ImageCreateInfo>(*begun);
    VkImage imageHandle = createImage(*begun, storedInfo);
    if (imageHandle == VK_NULL_HANDLE)
    {
        destroyEntityResources(*begun);
        return std::unexpected(rhi::ResourceError::AllocationFailed);
    }

    return finishImageEntity(*begun);
}

std::expected<entt::entity, rhi::ResourceError> ResourceContextImpl::beginImageEntity(const ImageCreateInfo& info, const DebugName& name)
{
    if (info.Domain == ResourceDomain::Invalid || info.Width == 0u || !info.Format.IsValid())
    {
        return std::unexpected(rhi::ResourceError::InvalidCreateInfo);
    }

    if (!ImageUsageIsSupported(info.Usage))
    {
        return std::unexpected(rhi::ResourceError::UnsupportedUsage);
    }

    const ResourceType resourceType = info.SamplerInfo.has_value() ? ResourceType::CombinedImageSampler : ResourceType::Image;
    const entt::entity newEntity = resourceRegistry.create();
    resourceRegistry.emplace<ResourceFlags>(newEntity, resourceType, info.Flags, info.Domain);
    resourceRegistry.emplace<ImageCreateInfo>(newEntity, info);
    if (!name.Empty())
    {
        resourceRegistry.emplace<DebugName>(newEntity, name);
    }

    return newEntity;
}

std::expected<GraphicsResource, rhi::ResourceError> ResourceContextImpl::finishImageEntity(entt::entity newEntity)
{
    const ImageCreateInfo& storedInfo = resourceRegistry.get<ImageCreateInfo>(newEntity);
    const ResourceType resourceType = resourceRegistry.get<ResourceFlags>(newEntity).Type;
    const VkImage imageHandle = resourceRegistry.get<VkImage>(newEntity);

    VkImageView imageView = VK_NULL_HANDLE;
    if (storedInfo.ViewInfo.has_value())
    {
        imageView = createImageView(newEntity, storedInfo);
        if (imageView == VK_NULL_HANDLE)
        {
            destroyEntityResources(newEntity);
            return std::unexpected(rhi::ResourceError::ViewCreationFailed);
        }
    }

    VkSampler sampler = VK_NULL_HANDLE;
    if (storedInfo.SamplerInfo.has_value())
    {
        sampler = createSampler(newEntity, *storedInfo.SamplerInfo, "_sampler");
        if (sampler == VK_NULL_HANDLE)
        {
            destroyEntityResources(newEntity);
            return std::unexpected(rhi::ResourceError::SamplerCreationFailed);
        }
    }

    return GraphicsResource
    {
        storedInfo.Domain,
        resourceType,
        static_cast<uint32_t>(newEntity),
        reinterpret_cast<uint64_t>(imageHandle),
        reinterpret_cast<uint64_t>(imageView),
        reinterpret_cast<uint64_t>(sampler)
    };
}


void ResourceContextImpl::processMessage(CreateSamplerMessage&& message)
{
    const entt::entity newEntity = resourceRegistry.create();
    resourceRegistry.emplace<ResourceFlags>(newEntity, ResourceType::Sampler, ResourceCreationFlags::None, ResourceDomain::GPUOnly);
    if (!message.Name.Empty())
    {
        resourceRegistry.emplace<DebugName>(newEntity, message.Name);
    }

    VkSampler sampler = createSampler(newEntity, message.SamplerInfo, std::string_view{});
    if (sampler == VK_NULL_HANDLE)
    {
        destroyEntityResources(newEntity);
        futures.Fail(message.Future, rhi::ResourceError::SamplerCreationFailed);
        return;
    }

    const GraphicsResource created
    {
        ResourceDomain::GPUOnly,
        ResourceType::Sampler,
        static_cast<uint32_t>(newEntity),
        0u,
        0u,
        reinterpret_cast<uint64_t>(sampler),
        0u
    };

    futures.CompleteWithResource(message.Future, created, DescribeSampler());
}

void ResourceContextImpl::processMessage(CreateMemoryHeapMessage&& message)
{
    if (message.Size == 0u || message.MemoryTypeBits == 0u)
    {
        futures.Fail(message.Future, rhi::ResourceError::InvalidCreateInfo);
        return;
    }

    // a dedicated allocation starts at offset 0 of its own VkDeviceMemory, so every alignment fits. the AUTO usages
    // cannot be used here, because VMA only knows what a buffer or an image needs when it creates one
    const VkMemoryRequirements requirements{ message.Size, 1u, message.MemoryTypeBits };
    const VmaAllocationCreateInfo allocCreateInfo
    {
        VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
        VMA_MEMORY_USAGE_UNKNOWN,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        0u,
        0u,
        VK_NULL_HANDLE,
        nullptr,
        0.0f
    };

    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo allocationInfo{};
    const VkResult result = vmaAllocateMemory(allocatorHandle, &requirements, &allocCreateInfo, &allocation, &allocationInfo);
    if (result != VK_SUCCESS)
    {
        futures.Fail(message.Future, rhi::ResourceError::AllocationFailed);
        return;
    }

    const entt::entity newEntity = resourceRegistry.create();
    resourceRegistry.emplace<ResourceFlags>(newEntity, ResourceType::Heap, ResourceCreationFlags::None, ResourceDomain::GPUOnly);
    resourceRegistry.emplace<MemoryHeap>(newEntity, allocationInfo.deviceMemory, allocationInfo.memoryType, message.Size, 0);
    resourceRegistry.emplace<VmaAllocation>(newEntity, allocation);

    if (!message.Name.Empty())
    {
        resourceRegistry.emplace<DebugName>(newEntity, message.Name);
        setObjectName(VK_OBJECT_TYPE_DEVICE_MEMORY, reinterpret_cast<uint64_t>(allocationInfo.deviceMemory), message.Name);
        vmaSetAllocationName(allocatorHandle, allocation, message.Name.CString());
    }

    const GraphicsResource created
    {
        ResourceDomain::GPUOnly,
        ResourceType::Heap,
        static_cast<uint32_t>(newEntity),
        reinterpret_cast<uint64_t>(allocationInfo.deviceMemory),
        0u,
        0u,
        0u
    };

    futures.CompleteWithResource(message.Future, created, DescribeHeap(message.Size));
}

void ResourceContextImpl::processMessage(CreatePlacedBufferMessage&& message)
{
    const BufferCreateInfo& info = message.Create.BufferInfo;
    if (info.Domain != ResourceDomain::GPUOnly)
    {
        futures.Fail(message.Create.Future, rhi::ResourceError::InvalidCreateInfo);
        return;
    }

    const std::expected<entt::entity, rhi::ResourceError> heap = resolveHeap(message.Heap);
    if (!heap.has_value())
    {
        futures.Fail(message.Create.Future, heap.error());
        return;
    }

    const std::expected<entt::entity, rhi::ResourceError> begun = beginBufferEntity(info, message.Create.Name);
    if (!begun.has_value())
    {
        futures.Fail(message.Create.Future, begun.error());
        return;
    }

    const rhi::ResourceError placed = placeBuffer(*begun, resourceRegistry.get<BufferCreateInfo>(*begun), *heap, message.Offset);
    if (placed != rhi::ResourceError::Success)
    {
        destroyEntityResources(*begun);
        futures.Fail(message.Create.Future, placed);
        return;
    }

    const std::expected<GraphicsResource, rhi::ResourceError> created = finishBufferEntity(*begun);
    if (!created.has_value())
    {
        futures.Fail(message.Create.Future, created.error());
        return;
    }

    futures.CompleteWithResource(message.Create.Future, *created, DescribeBuffer(info));
}

void ResourceContextImpl::processMessage(CreatePlacedImageMessage&& message)
{
    const ImageCreateInfo& info = message.Create.ImageInfo;
    const bool usesDiscard = (info.Usage & ImageUsageBits::Discard) != ImageUsageBits::Invalid;
    if (info.Domain != ResourceDomain::GPUOnly || usesDiscard)
    {
        futures.Fail(message.Create.Future, rhi::ResourceError::InvalidCreateInfo);
        return;
    }

    const std::expected<entt::entity, rhi::ResourceError> heap = resolveHeap(message.Heap);
    if (!heap.has_value())
    {
        futures.Fail(message.Create.Future, heap.error());
        return;
    }

    const std::expected<entt::entity, rhi::ResourceError> begun = beginImageEntity(info, message.Create.Name);
    if (!begun.has_value())
    {
        futures.Fail(message.Create.Future, begun.error());
        return;
    }

    const rhi::ResourceError placed = placeImage(*begun, resourceRegistry.get<ImageCreateInfo>(*begun), *heap, message.Offset);
    if (placed != rhi::ResourceError::Success)
    {
        destroyEntityResources(*begun);
        futures.Fail(message.Create.Future, placed);
        return;
    }

    const std::expected<GraphicsResource, rhi::ResourceError> created = finishImageEntity(*begun);
    if (!created.has_value())
    {
        futures.Fail(message.Create.Future, created.error());
        return;
    }

    futures.CompleteWithResource(message.Create.Future, *created, DescribeImage(info, created->Type, ImageUsageBits::Invalid));
}

void ResourceContextImpl::processMessage(SetBufferDataMessage&& message)
{
    const std::expected<entt::entity, rhi::ResourceError> entity = resolveBuffer(message.DestBuffer);
    if (!entity.has_value())
    {
        futures.Fail(message.Future, entity.error());
        return;
    }

    const uint64_t bufferSize = resourceRegistry.get<BufferCreateInfo>(*entity).Size;

    if (allocationIsHostVisible(*entity))
    {
        // the worker writes this memory at once, so a wait would block the worker thread
        if (message.Waits.Count > 0)
        {
            futures.Fail(message.Future, rhi::ResourceError::WaitOnHostWrite);
            return;
        }

        if (!BufferUploadPiecesAreValid(message.Data, bufferSize))
        {
            futures.Fail(message.Future, rhi::ResourceError::InvalidRegion);
            return;
        }

        setBufferDataHostOnly(*entity, message.Data);
        futures.Complete(message.Future);
        return;
    }

    if (!WaitsAreValid(message.Waits))
    {
        futures.Fail(message.Future, rhi::ResourceError::InvalidWait);
        return;
    }

    if (!CanBeTransferDestination(resourceRegistry, *entity))
    {
        futures.Fail(message.Future, rhi::ResourceError::MissingTransferUsage);
        return;
    }

    if (!BufferUploadPiecesAreValid(message.Data, bufferSize))
    {
        futures.Fail(message.Future, rhi::ResourceError::InvalidRegion);
        return;
    }

    enqueueBufferUpload(*entity, std::move(message.Data), message.Waits, message.Future);
}

void ResourceContextImpl::processMessage(SetImageDataMessage&& message)
{
    const std::expected<entt::entity, rhi::ResourceError> entity = resolveImage(message.DestImage);
    if (!entity.has_value())
    {
        futures.Fail(message.Future, entity.error());
        return;
    }

    if (!WaitsAreValid(message.Waits))
    {
        futures.Fail(message.Future, rhi::ResourceError::InvalidWait);
        return;
    }

    if (!CanBeTransferDestination(resourceRegistry, *entity))
    {
        futures.Fail(message.Future, rhi::ResourceError::MissingTransferUsage);
        return;
    }

    if (!DestinationTransitionIsValidForImage(resourceRegistry.get<ImageCreateInfo>(*entity).Usage, message.Usage))
    {
        futures.Fail(message.Future, rhi::ResourceError::InvalidUsageTransition);
        return;
    }

    const ImageCreateInfo& createInfo = resourceRegistry.get<ImageCreateInfo>(*entity);
    SetUploadPieceDepths(message.Data, createInfo);

    const bool imageIsDiscarded = message.Usage.Before == ImageUsageBits::Discard;
    if (!ImageUploadPiecesAreValid(message.Data, createInfo, imageIsDiscarded))
    {
        futures.Fail(message.Future, rhi::ResourceError::InvalidRegion);
        return;
    }

    const TransferSystemReqImageInfo imageInfo = imageTransferInfo(*entity, message.Usage);
    enqueueImageUpload(imageInfo, *entity, std::move(message.Data), message.Waits, message.Future);
}

void ResourceContextImpl::processMessage(FillResourceMessage&& message)
{
    const std::expected<entt::entity, rhi::ResourceError> entity = resolveBuffer(message.Resource);
    if (!entity.has_value())
    {
        futures.Fail(message.Future, entity.error());
        return;
    }

    if (!WaitsAreValid(message.Waits))
    {
        futures.Fail(message.Future, rhi::ResourceError::InvalidWait);
        return;
    }

    if (!CanBeTransferDestination(resourceRegistry, *entity))
    {
        futures.Fail(message.Future, rhi::ResourceError::MissingTransferUsage);
        return;
    }

    // vkCmdFillBuffer needs both multiples of 4 (or VK_WHOLE_SIZE), so we check here rather than fail in the transfer system
    const bool sizeIsValid = (message.Size == VK_WHOLE_SIZE) || (message.Size % 4u == 0u);
    if (message.Offset % 4u != 0u || !sizeIsValid)
    {
        futures.Fail(message.Future, rhi::ResourceError::InvalidFillRange);
        return;
    }

    TransferSystemFillBufferMessage fillBufferMessage
    {
        bufferTransferInfo(*entity),
        message.Value,
        message.Offset,
        message.Size,
        message.Future
    };

    enqueueTransfer(std::move(fillBufferMessage), std::span(&*entity, 1), message.Waits);
}

void ResourceContextImpl::processMessage(MapResourceMessage&& message)
{
    const std::expected<entt::entity, rhi::ResourceError> entity = resolveBuffer(message.Resource);
    if (!entity.has_value())
    {
        futures.Fail(message.Future, entity.error());
        return;
    }

    if (!allocationIsHostVisible(*entity))
    {
        futures.Fail(message.Future, rhi::ResourceError::NotHostVisible);
        return;
    }

    const VmaAllocation allocation = resourceRegistry.get<VmaAllocation>(*entity);
    const ResourceFlags& flags = resourceRegistry.get<ResourceFlags>(*entity);

    // readback memory may be cached and not coherent, so the device's writes have to be made visible to the host first
    if (flags.Domain == ResourceDomain::GPUToCPU)
    {
        vmaInvalidateAllocation(allocatorHandle, allocation, 0u, VK_WHOLE_SIZE);
    }

    // now check for some easy stuff - is this persistently mapped or created mapped? if so, yay we can just use that pointer!
    VmaAllocationInfo allocInfo{};
    vmaGetAllocationInfo(allocatorHandle, allocation, &allocInfo);
    const bool createdMapped = HasFlag(flags.Flags, ResourceCreationFlags::PersistentlyMapped) ||
                               HasFlag(flags.Flags, ResourceCreationFlags::CreateMapped);
    if (createdMapped && (allocInfo.pMappedData != nullptr))
    {
        futures.CompleteWithPointer(message.Future, static_cast<std::byte*>(allocInfo.pMappedData) + message.Offset);
        return;
    }

    // otherwise, we need to get a pointer to the buffer
    void* mappedPointer = nullptr;
    VkResult result = vmaMapMemory(allocatorHandle, allocation, &mappedPointer);
    if (result != VK_SUCCESS)
    {
        futures.Fail(message.Future, rhi::ResourceError::MapFailed);
        return;
    }

    resourceRegistry.get_or_emplace<UserMapCount>(*entity).Count += 1;
    // fixed: the offset parameter was ignored
    futures.CompleteWithPointer(message.Future, static_cast<std::byte*>(mappedPointer) + message.Offset);
}

void ResourceContextImpl::processMessage(UnmapResourceMessage&& message)
{
    const std::expected<entt::entity, rhi::ResourceError> entity = resolveBuffer(message.Resource);
    if (!entity.has_value())
    {
        futures.Fail(message.Future, entity.error());
        return;
    }

    // a placed buffer owns no allocation, and is device-local
    const VmaAllocation* allocationPointer = resourceRegistry.try_get<VmaAllocation>(*entity);
    if (allocationPointer == nullptr)
    {
        futures.Fail(message.Future, rhi::ResourceError::NotHostVisible);
        return;
    }

    const VmaAllocation allocation = *allocationPointer;
    const ResourceFlags& flags = resourceRegistry.get<ResourceFlags>(*entity);

    // host writes become visible to the device at the next submit, once they are flushed
    vmaFlushAllocation(allocatorHandle, allocation, message.Offset, message.Size);

    // don't want to unmap something that's persistently mapped!
    const bool createdMapped = HasFlag(flags.Flags, ResourceCreationFlags::PersistentlyMapped) ||
                               HasFlag(flags.Flags, ResourceCreationFlags::CreateMapped);
    UserMapCount* mapCount = resourceRegistry.try_get<UserMapCount>(*entity);
    if (!createdMapped && mapCount != nullptr && mapCount->Count > 0)
    {
        vmaUnmapMemory(allocatorHandle, allocation);
        mapCount->Count -= 1;
    }

    // fixed: the unmap reply was never completed
    futures.Complete(message.Future);
}

void ResourceContextImpl::processMessage(CopyResourceMessage&& message)
{
    // fixed: this checked the type and did nothing else, so its reply never completed
    const std::expected<entt::entity, rhi::ResourceError> resolvedSource = resolveResource(message.SourceResource);
    if (!resolvedSource.has_value())
    {
        futures.Fail(message.Future, resolvedSource.error());
        return;
    }

    const entt::entity srcEntity = *resolvedSource;
    const BufferCreateInfo* bufferInfo = resourceRegistry.try_get<BufferCreateInfo>(srcEntity);
    const ImageCreateInfo* imageInfo = resourceRegistry.try_get<ImageCreateInfo>(srcEntity);

    // CopyBuffer takes buffers and CopyImage takes images. a sampler has no copy path, and fails below
    if ((bufferInfo != nullptr && message.SourceIsImage) || (imageInfo != nullptr && !message.SourceIsImage))
    {
        futures.Fail(message.Future, rhi::ResourceError::WrongResourceKind);
        return;
    }

    // the waits belong to the copy of the contents, and they are checked before the copy exists as the usages are
    if (message.CopyContents && !WaitsAreValid(message.Waits))
    {
        futures.Fail(message.Future, rhi::ResourceError::InvalidWait);
        return;
    }

    // the usages are checked before the copy exists, so that a bad usage leaves nothing to clean up
    if (imageInfo != nullptr && message.CopyContents)
    {
        const ImageUsageBits copyUsage = imageInfo->Usage | ImageUsageBits::TransferDestination;
        const bool usagesAreValid = TransitionIsValidForImage(imageInfo->Usage, message.SourceUsage) &&
                                    UsageIsValidForImage(copyUsage, message.CopyUsageAfter);
        if (!usagesAreValid)
        {
            futures.Fail(message.Future, rhi::ResourceError::InvalidUsageTransition);
            return;
        }
    }

    // the copy is named after its source, with a suffix. a sampler has no copy path
    const DebugName* sourceName = resourceRegistry.try_get<DebugName>(srcEntity);
    const DebugName copyName = sourceName != nullptr ? sourceName->WithSuffix("_copy") : DebugName{};
    std::expected<GraphicsResource, rhi::ResourceError> created = std::unexpected(rhi::ResourceError::UnsupportedCopy);
    ResourceDescription description{};
    if (bufferInfo != nullptr)
    {
        BufferCreateInfo copyInfo = *bufferInfo;
        copyInfo.Name = {};
        if (message.CopyContents)
        {
            copyInfo.Usage |= BufferUsageBits::TransferDestination;
        }

        created = createBufferEntity(copyInfo, copyName);
        description = DescribeBuffer(copyInfo);
    }
    else if (imageInfo != nullptr)
    {
        ImageCreateInfo copyInfo = *imageInfo;
        copyInfo.Name = {};
        if (message.CopyContents)
        {
            copyInfo.Usage |= ImageUsageBits::TransferDestination;
        }

        created = createImageEntity(copyInfo, copyName);
        const ImageUsageBits initialUsage = message.CopyContents ? message.CopyUsageAfter : ImageUsageBits::Invalid;
        description = DescribeImage(copyInfo, created.has_value() ? created->Type : ResourceType::Image, initialUsage);
    }

    if (!created.has_value())
    {
        futures.Fail(message.Future, created.error());
        return;
    }

    if (!message.CopyContents)
    {
        futures.CompleteWithResource(message.Future, *created, description);
        return;
    }

    futures.StoreResource(message.Future, *created, description);

    CopyResourceContentsMessage copyMessage;
    copyMessage.Kind = message.SourceIsImage ? CopyKind::ImageToImage : CopyKind::BufferToBuffer;
    copyMessage.SourceResource = message.SourceResource;
    copyMessage.SourceUsage = message.SourceUsage;
    copyMessage.DestinationResource = *created;
    copyMessage.DestinationUsage = rhi::ImageUsageTransition{ ImageUsageBits::Invalid, message.CopyUsageAfter };
    copyMessage.DestinationStartsUndefined = true;
    copyMessage.Waits = message.Waits;
    copyMessage.Future = message.Future;
    processMessage(std::move(copyMessage));
}

void ResourceContextImpl::processMessage(CopyResourceContentsMessage&& message)
{
    const std::expected<entt::entity, rhi::ResourceError> resolvedSource = resolveResource(message.SourceResource);
    if (!resolvedSource.has_value())
    {
        futures.Fail(message.Future, resolvedSource.error());
        return;
    }

    const std::expected<entt::entity, rhi::ResourceError> resolvedDestination = resolveResource(message.DestinationResource);
    if (!resolvedDestination.has_value())
    {
        futures.Fail(message.Future, resolvedDestination.error());
        return;
    }

    if (!WaitsAreValid(message.Waits))
    {
        futures.Fail(message.Future, rhi::ResourceError::InvalidWait);
        return;
    }

    const entt::entity srcEntity = *resolvedSource;
    const entt::entity dstEntity = *resolvedDestination;

    const bool srcIsBuffer = resourceRegistry.all_of<VkBuffer, BufferCreateInfo>(srcEntity);
    const bool dstIsBuffer = resourceRegistry.all_of<VkBuffer, BufferCreateInfo>(dstEntity);
    const bool srcIsImage = resourceRegistry.all_of<VkImage, ImageCreateInfo>(srcEntity);
    const bool dstIsImage = resourceRegistry.all_of<VkImage, ImageCreateInfo>(dstEntity);
    if (!(srcIsBuffer || srcIsImage) || !(dstIsBuffer || dstIsImage))
    {
        futures.Fail(message.Future, rhi::ResourceError::UnsupportedCopy);
        return;
    }

    if (!CopyKindMatches(message.Kind, srcIsBuffer, dstIsBuffer))
    {
        futures.Fail(message.Future, rhi::ResourceError::WrongResourceKind);
        return;
    }

    if (!CanBeTransferSource(resourceRegistry, srcEntity) || !CanBeTransferDestination(resourceRegistry, dstEntity))
    {
        futures.Fail(message.Future, rhi::ResourceError::MissingTransferUsage);
        return;
    }

    // the transfer thread trusts every message it gets, so the checks that need both create infos happen here
    if (message.Kind == CopyKind::BufferToBuffer)
    {
        enqueueBufferCopy(message, srcEntity, dstEntity);
        return;
    }

    if (srcIsImage && !TransitionIsValidForImage(resourceRegistry.get<ImageCreateInfo>(srcEntity).Usage, message.SourceUsage))
    {
        futures.Fail(message.Future, rhi::ResourceError::InvalidUsageTransition);
        return;
    }

    // a fresh image (the copy that CopyImage made) has no Before usage, and CopyImage checked its After usage already
    const bool dstUsageIsChecked = dstIsImage && !message.DestinationStartsUndefined;
    if (dstUsageIsChecked && !DestinationTransitionIsValidForImage(resourceRegistry.get<ImageCreateInfo>(dstEntity).Usage, message.DestinationUsage))
    {
        futures.Fail(message.Future, rhi::ResourceError::InvalidUsageTransition);
        return;
    }

    if (message.Kind == CopyKind::ImageToImage)
    {
        enqueueImageToImageCopy(message, srcEntity, dstEntity);
        return;
    }

    if (message.Kind == CopyKind::BufferToImage)
    {
        enqueueBufferToImageCopy(message, srcEntity, dstEntity);
        return;
    }

    enqueueImageToBufferCopy(message, srcEntity, dstEntity);
}

void ResourceContextImpl::enqueueBufferCopy(CopyResourceContentsMessage& message, entt::entity src_entity, entt::entity dst_entity)
{
    const uint64_t sourceSize = resourceRegistry.get<BufferCreateInfo>(src_entity).Size;
    const uint64_t destinationSize = resourceRegistry.get<BufferCreateInfo>(dst_entity).Size;
    const bool copiesWholeSource = message.Regions.Count == 0;

    // CopyBufferContents sends no regions: its one region covers the source, so it is valid when the source fits
    if (copiesWholeSource)
    {
        if (sourceSize > destinationSize)
        {
            futures.Fail(message.Future, rhi::ResourceError::CopySizeMismatch);
            return;
        }

        const BufferCopyRegion wholeSource{ 0u, 0u, sourceSize };
        message.Regions = CopyRegionBlock(std::span<const BufferCopyRegion>(&wholeSource, 1u));
    }

    const bool isSameBuffer = src_entity == dst_entity;
    if (!copiesWholeSource && !BufferCopyRegionsAreValid(message.Regions.View(), sourceSize, destinationSize, isSameBuffer))
    {
        futures.Fail(message.Future, rhi::ResourceError::InvalidRegion);
        return;
    }

    TransferSystemCopyBufferToBufferMessage transferMessage{ bufferTransferInfo(src_entity), bufferTransferInfo(dst_entity) };
    transferMessage.Regions = std::move(message.Regions);
    transferMessage.Future = message.Future;

    const std::array<entt::entity, 2> touched{ src_entity, dst_entity };
    enqueueTransfer(std::move(transferMessage), touched, message.Waits);
}

void ResourceContextImpl::enqueueImageToImageCopy(CopyResourceContentsMessage& message, entt::entity src_entity, entt::entity dst_entity)
{
    const ImageCreateInfo& srcCreateInfo = resourceRegistry.get<ImageCreateInfo>(src_entity);
    const ImageCreateInfo& dstCreateInfo = resourceRegistry.get<ImageCreateInfo>(dst_entity);
    const bool copiesEveryMip = message.ImageCopyRegions.Count == 0;

    // CopyImageContents and CopyImage send no regions: they need the same shape, and get one region for each mip level
    const bool shapeIsValid = copiesEveryMip ? ImagesHaveTheSameShape(srcCreateInfo, dstCreateInfo) : ImagesAreCopyCompatible(srcCreateInfo, dstCreateInfo);
    if (!shapeIsValid)
    {
        futures.Fail(message.Future, rhi::ResourceError::CopyShapeMismatch);
        return;
    }

    if (copiesEveryMip)
    {
        message.ImageCopyRegions = MakeEveryMipRegionBlock(srcCreateInfo);
    }

    // one image has one layout during the copy, so both sides must ask for the same usages
    const bool isSameImage = src_entity == dst_entity;
    const bool usagesAreEqual = message.SourceUsage.Before == message.DestinationUsage.Before &&
                                message.SourceUsage.After == message.DestinationUsage.After;
    if (isSameImage && !usagesAreEqual)
    {
        futures.Fail(message.Future, rhi::ResourceError::InvalidRegion);
        return;
    }

    const bool destinationIsDiscarded = message.DestinationUsage.Before == ImageUsageBits::Discard;
    if (!ImageCopyRegionsAreValid(message.ImageCopyRegions.View(), srcCreateInfo, dstCreateInfo, isSameImage, destinationIsDiscarded))
    {
        futures.Fail(message.Future, rhi::ResourceError::InvalidRegion);
        return;
    }

    // a fresh image (the copy that CopyImage made) has no Before usage, and CopyImage checked its After usage already
    const TransferSystemReqImageInfo srcInfo = imageTransferInfo(src_entity, message.SourceUsage);
    const TransferSystemReqImageInfo dstInfo = message.DestinationStartsUndefined ?
        newImageTransferInfo(dst_entity, message.DestinationUsage.After) :
        imageTransferInfo(dst_entity, message.DestinationUsage);

    TransferSystemCopyImageToImageMessage transferMessage{ srcInfo, dstInfo };
    transferMessage.Regions = std::move(message.ImageCopyRegions);
    transferMessage.Future = message.Future;

    const std::array<entt::entity, 2> touched{ src_entity, dst_entity };
    enqueueTransfer(std::move(transferMessage), touched, message.Waits);
}

void ResourceContextImpl::enqueueBufferToImageCopy(CopyResourceContentsMessage& message, entt::entity src_entity, entt::entity dst_entity)
{
    const ImageCreateInfo& imageCreateInfo = resourceRegistry.get<ImageCreateInfo>(dst_entity);
    const bool copiesMipZero = message.BufferImageRegions.Count == 0;

    // CopyBufferToImage sends no regions. The caller sizes the buffer for its one region, and the context does not check it
    if (copiesMipZero)
    {
        message.BufferImageRegions = MakeMipZeroRegionBlock(imageCreateInfo);
    }

    const uint64_t bufferSize = resourceRegistry.get<BufferCreateInfo>(src_entity).Size;
    const std::optional<uint64_t> checkedBufferSize = copiesMipZero ? std::nullopt : std::optional<uint64_t>(bufferSize);
    const bool imageIsDiscarded = message.DestinationUsage.Before == ImageUsageBits::Discard;
    if (!BufferToImageRegionsAreValid(message.BufferImageRegions.View(), imageCreateInfo, checkedBufferSize, imageIsDiscarded))
    {
        futures.Fail(message.Future, rhi::ResourceError::InvalidRegion);
        return;
    }

    TransferSystemCopyBufferToImageMessage transferMessage{ bufferTransferInfo(src_entity), imageTransferInfo(dst_entity, message.DestinationUsage) };
    transferMessage.Regions = std::move(message.BufferImageRegions);
    transferMessage.Future = message.Future;

    const std::array<entt::entity, 2> touched{ src_entity, dst_entity };
    enqueueTransfer(std::move(transferMessage), touched, message.Waits);
}

void ResourceContextImpl::enqueueImageToBufferCopy(CopyResourceContentsMessage& message, entt::entity src_entity, entt::entity dst_entity)
{
    const ImageCreateInfo& imageCreateInfo = resourceRegistry.get<ImageCreateInfo>(src_entity);
    const bool copiesMipZero = message.BufferImageRegions.Count == 0;

    // CopyImageToBuffer sends no regions. The caller sizes the buffer for its one region, and the context does not check it
    if (copiesMipZero)
    {
        message.BufferImageRegions = MakeMipZeroRegionBlock(imageCreateInfo);
    }

    const uint64_t bufferSize = resourceRegistry.get<BufferCreateInfo>(dst_entity).Size;
    const std::optional<uint64_t> checkedBufferSize = copiesMipZero ? std::nullopt : std::optional<uint64_t>(bufferSize);
    if (!ImageToBufferRegionsAreValid(message.BufferImageRegions.View(), imageCreateInfo, checkedBufferSize))
    {
        futures.Fail(message.Future, rhi::ResourceError::InvalidRegion);
        return;
    }

    TransferSystemCopyImageToBufferMessage transferMessage{ imageTransferInfo(src_entity, message.SourceUsage), bufferTransferInfo(dst_entity) };
    transferMessage.Regions = std::move(message.BufferImageRegions);
    transferMessage.Future = message.Future;

    const std::array<entt::entity, 2> touched{ src_entity, dst_entity };
    enqueueTransfer(std::move(transferMessage), touched, message.Waits);
}

void ResourceContextImpl::processMessage(TransferSetMessage&& message)
{
    if (!WaitsAreValid(message.Waits))
    {
        futures.Fail(message.Future, rhi::ResourceError::InvalidWait);
        return;
    }

    const std::span<const TransferSetOp> ops = message.Set.Ops();
    const size_t opCount = ops.size();
    std::unique_ptr<ResolvedTransferOp[]> resolvedOps = std::make_unique<ResolvedTransferOp[]>(opCount);

    // all or nothing: every op is checked before the ticket exists, and the first bad op decides the error
    for (size_t opIndex = 0u; opIndex < opCount; ++opIndex)
    {
        const rhi::ResourceError error = resolveTransferSetOp(ops[opIndex], message.Data.Size, resolvedOps[opIndex]);
        if (error != rhi::ResourceError::Success)
        {
            futures.Fail(message.Future, error);
            return;
        }
    }

    if constexpr (k_RangeHazardValidatorEnabled)
    {
        if (TransferSetHasRangeHazard(ops, message.Set.RangeEnds()))
        {
            futures.Fail(message.Future, rhi::ResourceError::RangeHazard);
            return;
        }
    }

    std::array<entt::entity, 2u * static_cast<size_t>(k_MaxTransferSetOps)> touched;
    const size_t touchedCount = CollectTouchedEntities(ops, touched);

    // a set that has no upload stages nothing, whatever data the caller passed
    const bool usesData = TransferSetUploads(ops);

    TransferSystemTransferSetMessage transferMessage;
    transferMessage.Ops = std::move(resolvedOps);
    transferMessage.OpCount = static_cast<int32_t>(opCount);
    transferMessage.StagingAlignment = StagingAlignmentOfSet(resourceRegistry, ops);
    transferMessage.Set = std::move(message.Set);
    transferMessage.Data = usesData ? std::move(message.Data) : ByteBlock{};
    transferMessage.Future = message.Future;

    enqueueTransfer(std::move(transferMessage), std::span<const entt::entity>(touched.data(), touchedCount), message.Waits);
}

rhi::ResourceError ResourceContextImpl::resolveTransferSetOp(const TransferSetOp& op, uint64_t data_size, ResolvedTransferOp& resolved) const
{
    resolved.Kind = op.Kind;

    if (!TransferOpRegionMatchesKind(op))
    {
        return rhi::ResourceError::InvalidTransferSet;
    }

    switch (op.Kind)
    {
    case TransferOpKind::UploadBuffer:
        return resolveUploadBufferOp(op, data_size, resolved);
    case TransferOpKind::UploadImage:
        return resolveUploadImageOp(op, data_size, resolved);
    case TransferOpKind::FillBuffer:
        return resolveFillBufferOp(op, resolved);
    case TransferOpKind::CopyBuffer:
        return resolveCopyBufferOp(op, resolved);
    case TransferOpKind::CopyBufferToImage:
        return resolveCopyBufferToImageOp(op, resolved);
    case TransferOpKind::CopyImageToBuffer:
        return resolveCopyImageToBufferOp(op, resolved);
    case TransferOpKind::CopyImage:
        return resolveCopyImageOp(op, resolved);
    case TransferOpKind::Invalid:
        break;
    }

    return rhi::ResourceError::InvalidTransferSet;
}

std::expected<ResourceContextImpl::SetBufferSide, rhi::ResourceError> ResourceContextImpl::resolveSetBuffer(const GraphicsResource& resource, bool is_destination) const
{
    const std::expected<entt::entity, rhi::ResourceError> entity = resolveBuffer(resource);
    if (!entity.has_value())
    {
        return std::unexpected(entity.error());
    }

    const bool hasTransferUsage = is_destination ? CanBeTransferDestination(resourceRegistry, *entity) : CanBeTransferSource(resourceRegistry, *entity);
    if (!hasTransferUsage)
    {
        return std::unexpected(rhi::ResourceError::MissingTransferUsage);
    }

    if (is_destination && allocationIsHostVisible(*entity))
    {
        return std::unexpected(rhi::ResourceError::InvalidTransferSet);
    }

    return SetBufferSide{ resourceRegistry.get<VkBuffer>(*entity), resourceRegistry.get<BufferCreateInfo>(*entity).Size };
}

std::expected<ResourceContextImpl::SetImageSide, rhi::ResourceError> ResourceContextImpl::resolveSetImage(const GraphicsResource& resource, rhi::ImageUsageTransition usage, bool is_destination) const
{
    const std::expected<entt::entity, rhi::ResourceError> entity = resolveImage(resource);
    if (!entity.has_value())
    {
        return std::unexpected(entity.error());
    }

    const bool hasTransferUsage = is_destination ? CanBeTransferDestination(resourceRegistry, *entity) : CanBeTransferSource(resourceRegistry, *entity);
    if (!hasTransferUsage)
    {
        return std::unexpected(rhi::ResourceError::MissingTransferUsage);
    }

    const ImageCreateInfo& info = resourceRegistry.get<ImageCreateInfo>(*entity);
    const bool usageIsValid = is_destination ? DestinationTransitionIsValidForImage(info.Usage, usage) : TransitionIsValidForImage(info.Usage, usage);
    if (!usageIsValid)
    {
        return std::unexpected(rhi::ResourceError::InvalidUsageTransition);
    }

    if (is_destination && allocationIsHostVisible(*entity))
    {
        return std::unexpected(rhi::ResourceError::InvalidTransferSet);
    }

    return SetImageSide
    {
        resourceRegistry.get<VkImage>(*entity),
        BarrierAspectOf(info),
        LayoutBeforeTransfer(usage.Before),
        ImageLayoutFromUsageBits(usage.After),
        &info
    };
}

rhi::ResourceError ResourceContextImpl::resolveUploadBufferOp(const TransferSetOp& op, uint64_t data_size, ResolvedTransferOp& resolved) const
{
    const std::expected<SetBufferSide, rhi::ResourceError> destination = resolveSetBuffer(op.Destination, true);
    if (!destination.has_value())
    {
        return destination.error();
    }

    const BufferCopyRegion& region = std::get<BufferCopyRegion>(op.Region);

    // the bytes must be in the data of the set. the other rules are the rules of SetBufferData
    if (!RangeIsInside(region.SourceOffset, region.Size, data_size))
    {
        return rhi::ResourceError::InvalidTransferSet;
    }

    if (!BufferCopyRegionsAreValid(std::span<const BufferCopyRegion>(&region, 1u), data_size, destination->Size, false))
    {
        return rhi::ResourceError::InvalidRegion;
    }

    resolved.DestinationBuffer = destination->Handle;
    resolved.Buffer = region;
    return rhi::ResourceError::Success;
}

rhi::ResourceError ResourceContextImpl::resolveUploadImageOp(const TransferSetOp& op, uint64_t data_size, ResolvedTransferOp& resolved) const
{
    const std::expected<SetImageSide, rhi::ResourceError> destination = resolveSetImage(op.Destination, op.DestinationUsage, true);
    if (!destination.has_value())
    {
        return destination.error();
    }

    const BufferImageRegion& region = std::get<BufferImageRegion>(op.Region);

    // the rules of SetImageData first, with no buffer size: the bytes come from the data of the set
    const bool imageIsDiscarded = op.DestinationUsage.Before == ImageUsageBits::Discard;
    if (!BufferToImageRegionsAreValid(std::span<const BufferImageRegion>(&region, 1u), *destination->Info, std::nullopt, imageIsDiscarded))
    {
        return rhi::ResourceError::InvalidRegion;
    }

    const std::optional<uint64_t> footprint = BufferImageFootprintBytes(region, destination->Info->Format);
    if (!footprint.has_value() || !RangeIsInside(region.BufferOffset, *footprint, data_size))
    {
        return rhi::ResourceError::InvalidTransferSet;
    }

    resolved.DestinationImage = destination->Handle;
    resolved.DestinationAspect = destination->Aspect;
    resolved.DestinationBeforeLayout = destination->BeforeLayout;
    resolved.DestinationAfterLayout = destination->AfterLayout;
    resolved.BufferImage = region;
    return rhi::ResourceError::Success;
}

rhi::ResourceError ResourceContextImpl::resolveFillBufferOp(const TransferSetOp& op, ResolvedTransferOp& resolved) const
{
    const std::expected<SetBufferSide, rhi::ResourceError> destination = resolveSetBuffer(op.Destination, true);
    if (!destination.has_value())
    {
        return destination.error();
    }

    const BufferCopyRegion& region = std::get<BufferCopyRegion>(op.Region);

    // vkCmdFillBuffer needs both multiples of 4, as in FillBuffer. a set has no whole-size fill
    const bool isUnaligned = region.DestinationOffset % 4u != 0u || region.Size % 4u != 0u;
    if (isUnaligned)
    {
        return rhi::ResourceError::InvalidFillRange;
    }

    if (region.Size == 0u || !RangeIsInside(region.DestinationOffset, region.Size, destination->Size))
    {
        return rhi::ResourceError::InvalidRegion;
    }

    resolved.DestinationBuffer = destination->Handle;
    resolved.Buffer = region;
    resolved.FillValue = op.FillValue;
    return rhi::ResourceError::Success;
}

rhi::ResourceError ResourceContextImpl::resolveCopyBufferOp(const TransferSetOp& op, ResolvedTransferOp& resolved) const
{
    const std::expected<SetBufferSide, rhi::ResourceError> source = resolveSetBuffer(op.Source, false);
    if (!source.has_value())
    {
        return source.error();
    }

    const std::expected<SetBufferSide, rhi::ResourceError> destination = resolveSetBuffer(op.Destination, true);
    if (!destination.has_value())
    {
        return destination.error();
    }

    // an op never reads and writes one resource. CopyBufferRegions copies inside one buffer
    if (op.Source.EntityHandle == op.Destination.EntityHandle)
    {
        return rhi::ResourceError::InvalidTransferSet;
    }

    const BufferCopyRegion& region = std::get<BufferCopyRegion>(op.Region);
    if (!BufferCopyRegionsAreValid(std::span<const BufferCopyRegion>(&region, 1u), source->Size, destination->Size, false))
    {
        return rhi::ResourceError::InvalidRegion;
    }

    resolved.SourceBuffer = source->Handle;
    resolved.DestinationBuffer = destination->Handle;
    resolved.Buffer = region;
    return rhi::ResourceError::Success;
}

rhi::ResourceError ResourceContextImpl::resolveCopyBufferToImageOp(const TransferSetOp& op, ResolvedTransferOp& resolved) const
{
    const std::expected<SetBufferSide, rhi::ResourceError> source = resolveSetBuffer(op.Source, false);
    if (!source.has_value())
    {
        return source.error();
    }

    const std::expected<SetImageSide, rhi::ResourceError> destination = resolveSetImage(op.Destination, op.DestinationUsage, true);
    if (!destination.has_value())
    {
        return destination.error();
    }

    const BufferImageRegion& region = std::get<BufferImageRegion>(op.Region);
    const bool imageIsDiscarded = op.DestinationUsage.Before == ImageUsageBits::Discard;
    if (!BufferToImageRegionsAreValid(std::span<const BufferImageRegion>(&region, 1u), *destination->Info, source->Size, imageIsDiscarded))
    {
        return rhi::ResourceError::InvalidRegion;
    }

    resolved.SourceBuffer = source->Handle;
    resolved.DestinationImage = destination->Handle;
    resolved.DestinationAspect = destination->Aspect;
    resolved.DestinationBeforeLayout = destination->BeforeLayout;
    resolved.DestinationAfterLayout = destination->AfterLayout;
    resolved.BufferImage = region;
    return rhi::ResourceError::Success;
}

rhi::ResourceError ResourceContextImpl::resolveCopyImageToBufferOp(const TransferSetOp& op, ResolvedTransferOp& resolved) const
{
    const std::expected<SetImageSide, rhi::ResourceError> source = resolveSetImage(op.Source, op.SourceUsage, false);
    if (!source.has_value())
    {
        return source.error();
    }

    const std::expected<SetBufferSide, rhi::ResourceError> destination = resolveSetBuffer(op.Destination, true);
    if (!destination.has_value())
    {
        return destination.error();
    }

    const BufferImageRegion& region = std::get<BufferImageRegion>(op.Region);
    if (!ImageToBufferRegionsAreValid(std::span<const BufferImageRegion>(&region, 1u), *source->Info, destination->Size))
    {
        return rhi::ResourceError::InvalidRegion;
    }

    resolved.SourceImage = source->Handle;
    resolved.SourceAspect = source->Aspect;
    resolved.SourceBeforeLayout = source->BeforeLayout;
    resolved.SourceAfterLayout = source->AfterLayout;
    resolved.DestinationBuffer = destination->Handle;
    resolved.BufferImage = region;
    return rhi::ResourceError::Success;
}

rhi::ResourceError ResourceContextImpl::resolveCopyImageOp(const TransferSetOp& op, ResolvedTransferOp& resolved) const
{
    const std::expected<SetImageSide, rhi::ResourceError> source = resolveSetImage(op.Source, op.SourceUsage, false);
    if (!source.has_value())
    {
        return source.error();
    }

    const std::expected<SetImageSide, rhi::ResourceError> destination = resolveSetImage(op.Destination, op.DestinationUsage, true);
    if (!destination.has_value())
    {
        return destination.error();
    }

    // an op never reads and writes one resource. CopyImageRegions copies inside one image
    if (op.Source.EntityHandle == op.Destination.EntityHandle)
    {
        return rhi::ResourceError::InvalidTransferSet;
    }

    if (!ImagesAreCopyCompatible(*source->Info, *destination->Info))
    {
        return rhi::ResourceError::CopyShapeMismatch;
    }

    const ImageCopyRegion& region = std::get<ImageCopyRegion>(op.Region);
    const bool destinationIsDiscarded = op.DestinationUsage.Before == ImageUsageBits::Discard;
    if (!ImageCopyRegionsAreValid(std::span<const ImageCopyRegion>(&region, 1u), *source->Info, *destination->Info, false, destinationIsDiscarded))
    {
        return rhi::ResourceError::InvalidRegion;
    }

    resolved.SourceImage = source->Handle;
    resolved.SourceAspect = source->Aspect;
    resolved.SourceBeforeLayout = source->BeforeLayout;
    resolved.SourceAfterLayout = source->AfterLayout;
    resolved.DestinationImage = destination->Handle;
    resolved.DestinationAspect = destination->Aspect;
    resolved.DestinationBeforeLayout = destination->BeforeLayout;
    resolved.DestinationAfterLayout = destination->AfterLayout;
    resolved.Image = region;
    return rhi::ResourceError::Success;
}

void ResourceContextImpl::processMessage(DestroyResourceMessage&& message)
{
    if (message.HasAfter && (message.After.Semaphore.Get() == 0u || message.After.Value == 0u))
    {
        futures.Fail(message.Future, rhi::ResourceError::InvalidWait);
        return;
    }

    const std::expected<entt::entity, rhi::ResourceError> entity = resolveResource(message.Resource);
    if (!entity.has_value())
    {
        futures.Fail(message.Future, entity.error());
        return;
    }

    const MemoryHeap* heap = resourceRegistry.try_get<MemoryHeap>(*entity);
    if (heap != nullptr && heap->PlacementCount > 0)
    {
        futures.Fail(message.Future, rhi::ResourceError::HeapInUse);
        return;
    }

    // the values are recorded now: work that the owner records after this destroy cannot use the resource
    RetiringResource entry{ *entity };
    bool mustWait = false;
    for (size_t i = 0; i < consumerTimelines.size(); ++i)
    {
        const ConsumerTimelineSlot& slot = consumerTimelines[i];
        if (slot.Semaphore != 0u && slot.PendingValue > slot.CompletedValue)
        {
            entry.TimelineValues[i] = slot.PendingValue;
            mustWait = true;
        }
    }

    const LastTransferTicket* lastTicket = resourceRegistry.try_get<LastTransferTicket>(*entity);
    if (lastTicket != nullptr && lastTicket->Value > transferSystem.CompletedTicket())
    {
        entry.TransferTicket = lastTicket->Value;
        mustWait = true;
    }

    if (message.HasAfter && !timelineReached(message.After.Semaphore.Get(), message.After.Value))
    {
        entry.AfterSemaphore = message.After.Semaphore.Get();
        entry.AfterValue = message.After.Value;
        mustWait = true;
    }

    entry.Future = message.Future;
    if (!mustWait)
    {
        retire(entry);
        return;
    }

    resourceRegistry.emplace<Retiring>(*entity);
    retiring.push_back(std::move(entry));
    retirementsPending.store(static_cast<int64_t>(retiring.size()), std::memory_order_relaxed);
}

void ResourceContextImpl::processMessage([[maybe_unused]] ExitWorkerMessage&& message) noexcept
{
    // the worker loop returns on this message before it dispatches, and Destroy() drains only after the worker has
    // consumed it, so this never runs. it exists so that the dispatch stays exhaustive
}

void ResourceContextImpl::processMessage(TickMessage&& message)
{
    for (int32_t i = 0; i < message.Count; ++i)
    {
        const ConsumerTimelinePoint& point = message.Points[static_cast<size_t>(i)];
        for (ConsumerTimelineSlot& slot : consumerTimelines)
        {
            if (slot.Semaphore == point.Semaphore && point.Semaphore != 0u)
            {
                slot.PendingValue = std::max(slot.PendingValue, point.PendingValue);
            }
        }
    }

    // one counter read per registered timeline per tick, not one per resource
    for (ConsumerTimelineSlot& slot : consumerTimelines)
    {
        if (slot.Semaphore == 0u)
        {
            continue;
        }

        uint64_t value = 0u;
        if (vkGetSemaphoreCounterValue(device->Handle().As<VkDevice>(), reinterpret_cast<VkSemaphore>(slot.Semaphore), &value) == VK_SUCCESS)
        {
            slot.CompletedValue = value;
        }

        consumerTimelineReads.fetch_add(1, std::memory_order_relaxed);
    }

    publishMemoryBudgets();
}

void ResourceContextImpl::publishMemoryBudgets()
{
    const VkPhysicalDeviceMemoryProperties* memoryProperties = nullptr;
    vmaGetMemoryProperties(allocatorHandle, &memoryProperties);

    std::array<VmaBudget, VK_MAX_MEMORY_HEAPS> budgets{};
    vmaGetHeapBudgets(allocatorHandle, budgets.data());

    const int32_t heapCount = static_cast<int32_t>(memoryProperties->memoryHeapCount);
    for (int32_t heapIndex = 0; heapIndex < heapCount; ++heapIndex)
    {
        const size_t index = static_cast<size_t>(heapIndex);
        const bool isDeviceLocal = (memoryProperties->memoryHeaps[index].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0u;

        heapBudgets[index].store(budgets[index].budget, std::memory_order_relaxed);
        heapUsages[index].store(budgets[index].usage, std::memory_order_relaxed);
        heapsAreDeviceLocal[index].store(isDeviceLocal, std::memory_order_relaxed);
    }

    memoryBudgetFromExtension.store(memoryBudgetExtensionEnabled, std::memory_order_relaxed);
    memoryHeapCount.store(heapCount, std::memory_order_relaxed);
    memoryBudgetSequence.fetch_add(1, std::memory_order_release);
}

void ResourceContextImpl::processMessage(RegisterConsumerTimelineMessage&& message)
{
    if (message.Semaphore == 0u)
    {
        futures.Fail(message.Future, rhi::ResourceError::InvalidTimeline);
        return;
    }

    if (std::ranges::find(consumerTimelines, message.Semaphore, &ConsumerTimelineSlot::Semaphore) != consumerTimelines.end())
    {
        futures.Fail(message.Future, rhi::ResourceError::TimelineAlreadyRegistered);
        return;
    }

    const auto freeSlot = std::ranges::find(consumerTimelines, uint64_t{ 0u }, &ConsumerTimelineSlot::Semaphore);
    if (freeSlot == consumerTimelines.end())
    {
        futures.Fail(message.Future, rhi::ResourceError::NoFreeTimelineSlot);
        return;
    }

    *freeSlot = ConsumerTimelineSlot{ message.Semaphore, 0u, 0u };
    futures.Complete(message.Future);
}

void ResourceContextImpl::processMessage(UnregisterConsumerTimelineMessage&& message)
{
    if (message.Semaphore == 0u)
    {
        futures.Fail(message.Future, rhi::ResourceError::InvalidTimeline);
        return;
    }

    for (size_t i = 0; i < consumerTimelines.size(); ++i)
    {
        if (consumerTimelines[i].Semaphore != message.Semaphore)
        {
            continue;
        }

        // the owner unregisters only after the GPU passed every value it reported, so nothing waits on this one now
        for (RetiringResource& entry : retiring)
        {
            entry.TimelineValues[i] = 0u;
        }

        consumerTimelines[i] = ConsumerTimelineSlot{};
        futures.Complete(message.Future);
        return;
    }

    futures.Fail(message.Future, rhi::ResourceError::TimelineNotRegistered);
}

void ResourceContextImpl::processMessage([[maybe_unused]] TransferProgressMessage&& message) noexcept
{
    // nothing to do here: the worker runs retireReady() after every message
}

template<typename TransferMessage>
void ResourceContextImpl::enqueueTransfer(TransferMessage&& message, std::span<const entt::entity> touched_entities, const TransferWaitSet& waits)
{
    // the worker is the only producer for the transfer queue, so a plain counter gives tickets that rise in queue order
    const uint64_t ticket = ++lastIssuedTicket;
    message.Ticket = ticket;
    message.Waits = waits;
    futures.SetTransferring(message.Future, ticket);

    for (const entt::entity entity : touched_entities)
    {
        resourceRegistry.emplace_or_replace<LastTransferTicket>(entity, ticket);
    }

    transferSystem.EnqueueTransfer(std::forward<TransferMessage>(message));
}

void ResourceContextImpl::retireReady()
{
    if (retiring.empty())
    {
        return;
    }

    const uint64_t completedTicket = transferSystem.CompletedTicket();
    size_t index = 0u;
    while (index < retiring.size())
    {
        if (!isReadyToRetire(retiring[index], completedTicket))
        {
            ++index;
            continue;
        }

        // the count drops before the future completes, so a caller that sees the future also sees the count
        RetiringResource entry = std::move(retiring[index]);
        retiring[index] = std::move(retiring.back());
        retiring.pop_back();
        retirementsPending.store(static_cast<int64_t>(retiring.size()), std::memory_order_relaxed);
        retire(entry);
    }
}

bool ResourceContextImpl::isReadyToRetire(const RetiringResource& entry, uint64_t completed_ticket) const noexcept
{
    if (entry.TransferTicket > completed_ticket)
    {
        return false;
    }

    for (size_t i = 0; i < consumerTimelines.size(); ++i)
    {
        if (entry.TimelineValues[i] > consumerTimelines[i].CompletedValue)
        {
            return false;
        }
    }

    if (entry.AfterValue != 0u && !timelineReached(entry.AfterSemaphore, entry.AfterValue))
    {
        return false;
    }

    return true;
}

bool ResourceContextImpl::timelineReached(uint64_t semaphore, uint64_t value) const noexcept
{
    uint64_t counter = 0u;
    const VkResult result = vkGetSemaphoreCounterValue(device->Handle().As<VkDevice>(), reinterpret_cast<VkSemaphore>(semaphore), &counter);

    return result == VK_SUCCESS && counter >= value;
}

void ResourceContextImpl::retire(RetiringResource& entry)
{
    destroyEntityResources(entry.Entity);
    resourcesRetired.fetch_add(1, std::memory_order_relaxed);
    // last: the owner may free the slot as soon as it sees Complete
    futures.Complete(entry.Future);
    entry.Future = -1;
}

std::expected<entt::entity, rhi::ResourceError> ResourceContextImpl::resolveBuffer(const GraphicsResource& resource) const
{
    const entt::entity entity = entt::entity(resource.EntityHandle);
    if (!resourceRegistry.valid(entity))
    {
        return std::unexpected(rhi::ResourceError::StaleResource);
    }

    const VkBuffer* buffer = resourceRegistry.try_get<VkBuffer>(entity);
    if (buffer == nullptr)
    {
        return std::unexpected(rhi::ResourceError::WrongResourceKind);
    }

    // the entity's version can wrap, so the API handle is the second check against a stale GraphicsResource
    if (reinterpret_cast<uint64_t>(*buffer) != resource.VkHandle)
    {
        return std::unexpected(rhi::ResourceError::StaleResource);
    }

    if (resourceRegistry.all_of<Retiring>(entity))
    {
        return std::unexpected(rhi::ResourceError::ResourceRetiring);
    }

    return entity;
}

std::expected<entt::entity, rhi::ResourceError> ResourceContextImpl::resolveImage(const GraphicsResource& resource) const
{
    const entt::entity entity = entt::entity(resource.EntityHandle);
    if (!resourceRegistry.valid(entity))
    {
        return std::unexpected(rhi::ResourceError::StaleResource);
    }

    const VkImage* image = resourceRegistry.try_get<VkImage>(entity);
    if (image == nullptr)
    {
        return std::unexpected(rhi::ResourceError::WrongResourceKind);
    }

    if (reinterpret_cast<uint64_t>(*image) != resource.VkHandle)
    {
        return std::unexpected(rhi::ResourceError::StaleResource);
    }

    if (resourceRegistry.all_of<Retiring>(entity))
    {
        return std::unexpected(rhi::ResourceError::ResourceRetiring);
    }

    return entity;
}

std::expected<entt::entity, rhi::ResourceError> ResourceContextImpl::resolveHeap(const GraphicsResource& resource) const
{
    const entt::entity entity = entt::entity(resource.EntityHandle);
    if (!resourceRegistry.valid(entity))
    {
        return std::unexpected(rhi::ResourceError::StaleResource);
    }

    const MemoryHeap* heap = resourceRegistry.try_get<MemoryHeap>(entity);
    if (heap == nullptr)
    {
        return std::unexpected(rhi::ResourceError::WrongResourceKind);
    }

    if (reinterpret_cast<uint64_t>(heap->Memory) != resource.VkHandle)
    {
        return std::unexpected(rhi::ResourceError::StaleResource);
    }

    if (resourceRegistry.all_of<Retiring>(entity))
    {
        return std::unexpected(rhi::ResourceError::ResourceRetiring);
    }

    return entity;
}

std::expected<entt::entity, rhi::ResourceError> ResourceContextImpl::resolveResource(const GraphicsResource& resource) const
{
    const entt::entity entity = entt::entity(resource.EntityHandle);
    if (!resourceRegistry.valid(entity))
    {
        return std::unexpected(rhi::ResourceError::StaleResource);
    }

    if (resourceRegistry.all_of<VkBuffer>(entity))
    {
        return resolveBuffer(resource);
    }

    if (resourceRegistry.all_of<VkImage>(entity))
    {
        return resolveImage(resource);
    }

    if (resourceRegistry.all_of<MemoryHeap>(entity))
    {
        return resolveHeap(resource);
    }

    // a sampler has no main handle, so its sampler handle is the one we compare
    const VkSampler* sampler = resourceRegistry.try_get<VkSampler>(entity);
    if (sampler == nullptr || reinterpret_cast<uint64_t>(*sampler) != resource.VkSamplerHandle)
    {
        return std::unexpected(rhi::ResourceError::StaleResource);
    }

    if (resourceRegistry.all_of<Retiring>(entity))
    {
        return std::unexpected(rhi::ResourceError::ResourceRetiring);
    }

    return entity;
}

VkBuffer ResourceContextImpl::createBuffer(entt::entity new_entity, const BufferCreateInfo& buffer_info)
{
    const VkBufferCreateInfo vkBufferInfo = ToVkBufferCreateInfo(buffer_info, queueFamilyIndices);

    const VmaAllocationCreateInfo allocCreateInfo
    {
        ToVmaAllocationFlags(buffer_info.Domain, buffer_info.Flags),
        ToVmaMemoryUsage(buffer_info.Domain),
        0u,
        0u,
        UINT32_MAX,
        VK_NULL_HANDLE,
        nullptr, // userData from our struct is not passed into VMA: we haven't used it for anything
        0.0f
    };

    VkBuffer bufferHandle = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;
    VkResult result = vmaCreateBuffer(allocatorHandle, &vkBufferInfo, &allocCreateInfo, &bufferHandle, &alloc, nullptr);
    if (result != VK_SUCCESS)
    {
        return VK_NULL_HANDLE;
    }

    resourceRegistry.emplace<VkBuffer>(new_entity, bufferHandle);
    resourceRegistry.emplace<VmaAllocation>(new_entity, alloc);

    if (const DebugName* debugName = resourceRegistry.try_get<DebugName>(new_entity))
    {
        setObjectName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(bufferHandle), *debugName);
        vmaSetAllocationName(allocatorHandle, alloc, debugName->WithSuffix("_allocation").CString());
    }

    return bufferHandle;
}

VkBufferView ResourceContextImpl::createBufferView(entt::entity new_entity, const BufferCreateInfo& buffer_info)
{
    const VkBufferViewCreateInfo viewInfo
    {
        VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
        nullptr,
        0u,
        resourceRegistry.get<VkBuffer>(new_entity),
        ToVkFormat(*buffer_info.ViewFormat),
        0u,
        VK_WHOLE_SIZE
    };

    VkBufferView bufferView = VK_NULL_HANDLE;
    VkResult result = vkCreateBufferView(device->Handle().As<VkDevice>(), &viewInfo, nullptr, &bufferView);
    if (result != VK_SUCCESS)
    {
        return VK_NULL_HANDLE;
    }

    // fixed: this and the caller both emplaced the view, which asserts in EnTT
    resourceRegistry.emplace<VkBufferView>(new_entity, bufferView);

    if (const DebugName* parentName = resourceRegistry.try_get<DebugName>(new_entity))
    {
        setObjectName(VK_OBJECT_TYPE_BUFFER_VIEW, reinterpret_cast<uint64_t>(bufferView), parentName->WithSuffix("_buffer_view"));
    }

    return bufferView;
}

rhi::ResourceError ResourceContextImpl::placeBuffer(entt::entity new_entity, const BufferCreateInfo& buffer_info, entt::entity heap_entity, uint64_t offset)
{
    const VkDevice vkDevice = VulkanDevice();
    const VkBufferCreateInfo vkBufferInfo = ToVkBufferCreateInfo(buffer_info, queueFamilyIndices);

    VkBuffer bufferHandle = VK_NULL_HANDLE;
    if (vkCreateBuffer(vkDevice, &vkBufferInfo, nullptr, &bufferHandle) != VK_SUCCESS)
    {
        return rhi::ResourceError::AllocationFailed;
    }

    // the real requirements of this buffer decide, whatever the caller's query said
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(vkDevice, bufferHandle, &requirements);

    const MemoryHeap& heap = resourceRegistry.get<MemoryHeap>(heap_entity);
    if (!PlacementFits(requirements, offset, heap.Size, heap.MemoryTypeIndex))
    {
        vkDestroyBuffer(vkDevice, bufferHandle, nullptr);
        return rhi::ResourceError::InvalidPlacement;
    }

    // not vmaCreateAliasingBuffer2: it asserts on a bad placement, and the check above must be the one that answers
    const VmaAllocation heapAllocation = resourceRegistry.get<VmaAllocation>(heap_entity);
    if (vmaBindBufferMemory2(allocatorHandle, heapAllocation, offset, bufferHandle, nullptr) != VK_SUCCESS)
    {
        vkDestroyBuffer(vkDevice, bufferHandle, nullptr);
        return rhi::ResourceError::AllocationFailed;
    }

    resourceRegistry.emplace<VkBuffer>(new_entity, bufferHandle);
    registerPlacement(new_entity, heap_entity);

    if (const DebugName* debugName = resourceRegistry.try_get<DebugName>(new_entity))
    {
        setObjectName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(bufferHandle), *debugName);
    }

    return rhi::ResourceError::Success;
}

rhi::ResourceError ResourceContextImpl::placeImage(entt::entity new_entity, const ImageCreateInfo& image_info, entt::entity heap_entity, uint64_t offset)
{
    const VkDevice vkDevice = VulkanDevice();
    const VkImageCreateInfo vkImageInfo = ToVkImageCreateInfo(image_info, queueFamilyIndices);

    VkImage imageHandle = VK_NULL_HANDLE;
    if (vkCreateImage(vkDevice, &vkImageInfo, nullptr, &imageHandle) != VK_SUCCESS)
    {
        return rhi::ResourceError::AllocationFailed;
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(vkDevice, imageHandle, &requirements);

    const MemoryHeap& heap = resourceRegistry.get<MemoryHeap>(heap_entity);
    if (!PlacementFits(requirements, offset, heap.Size, heap.MemoryTypeIndex))
    {
        vkDestroyImage(vkDevice, imageHandle, nullptr);
        return rhi::ResourceError::InvalidPlacement;
    }

    // not vmaCreateAliasingImage2, for the reason given in placeBuffer
    const VmaAllocation heapAllocation = resourceRegistry.get<VmaAllocation>(heap_entity);
    if (vmaBindImageMemory2(allocatorHandle, heapAllocation, offset, imageHandle, nullptr) != VK_SUCCESS)
    {
        vkDestroyImage(vkDevice, imageHandle, nullptr);
        return rhi::ResourceError::AllocationFailed;
    }

    resourceRegistry.emplace<VkImage>(new_entity, imageHandle);
    registerPlacement(new_entity, heap_entity);

    if (const DebugName* debugName = resourceRegistry.try_get<DebugName>(new_entity))
    {
        setObjectName(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(imageHandle), *debugName);
    }

    return rhi::ResourceError::Success;
}

void ResourceContextImpl::registerPlacement(entt::entity new_entity, entt::entity heap_entity)
{
    resourceRegistry.emplace<PlacedIn>(new_entity, heap_entity);
    resourceRegistry.get<MemoryHeap>(heap_entity).PlacementCount += 1;
}

void ResourceContextImpl::setBufferDataHostOnly(entt::entity entity, InternalResourceDataContainer& data_container)
{
    const VmaAllocation alloc = resourceRegistry.get<VmaAllocation>(entity);
    VmaAllocationInfo allocInfo{};
    vmaGetAllocationInfo(allocatorHandle, alloc, &allocInfo);

    void* mappedAddress = allocInfo.pMappedData;
    const bool mappedHere = (mappedAddress == nullptr);
    if (mappedHere)
    {
        [[maybe_unused]] VkResult result = vmaMapMemory(allocatorHandle, alloc, &mappedAddress);
        assert(result == VK_SUCCESS);
    }

    InternalResourceDataContainer::BufferDataVector& dataVector =
        std::get<InternalResourceDataContainer::BufferDataVector>(data_container.DataVector);

    for (const InternalResourceDataContainer::BufferData& data : dataVector)
    {
        std::memcpy(static_cast<std::byte*>(mappedAddress) + data.DestinationOffset, data.Data.get(), data.Size);
    }

    vmaFlushAllocation(allocatorHandle, alloc, 0u, VK_WHOLE_SIZE);
    // fixed: only unmap if we mapped it here, to avoid unmapping persistently mapped buffers
    if (mappedHere)
    {
        vmaUnmapMemory(allocatorHandle, alloc);
    }
    // free copied memory, finally
    dataVector.clear();
}

VkImage ResourceContextImpl::createImage(entt::entity new_entity, const ImageCreateInfo& image_info)
{
    const VkImageCreateInfo vkImageInfo = ToVkImageCreateInfo(image_info, queueFamilyIndices);

    const VmaAllocationCreateInfo allocCreateInfo
    {
        ToVmaAllocationFlags(image_info.Domain, image_info.Flags),
        ToVmaMemoryUsage(image_info.Domain),
        0u,
        0u,
        UINT32_MAX,
        VK_NULL_HANDLE,
        nullptr, // used to userData, but we only ever used that for names: Vma doesn't need those
        0.0f
    };

    VkImage imageHandle = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;
    VkResult result = vmaCreateImage(allocatorHandle, &vkImageInfo, &allocCreateInfo, &imageHandle, &alloc, nullptr);
    if (result != VK_SUCCESS)
    {
        return VK_NULL_HANDLE;
    }

    // fixed: this and the caller both emplaced the image, which asserts in EnTT
    resourceRegistry.emplace<VkImage>(new_entity, imageHandle);
    resourceRegistry.emplace<VmaAllocation>(new_entity, alloc);

    if (const DebugName* debugName = resourceRegistry.try_get<DebugName>(new_entity))
    {
        setObjectName(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(imageHandle), *debugName);
        vmaSetAllocationName(allocatorHandle, alloc, debugName->WithSuffix("_allocation").CString());
    }

    return imageHandle;
}

VkImageView ResourceContextImpl::createImageView(entt::entity new_entity, const ImageCreateInfo& image_info)
{
    const ImageViewCreateInfo& view = *image_info.ViewInfo;
    const VkFormat format = ToVkFormat(view.FormatOverride.value_or(image_info.Format));
    const ImageAspectFlags aspect = (view.Range.AspectMask != ImageAspectFlags::None) ?
        view.Range.AspectMask : AspectFromVkFormat(ToVkFormat(image_info.Format));

    const VkImageViewCreateInfo viewInfo
    {
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        nullptr,
        0u,
        resourceRegistry.get<VkImage>(new_entity),
        (view.ViewType != ImageViewType::Invalid) ? ToVkImageViewType(view.ViewType) : DefaultVkImageViewType(image_info.Type, image_info.ArrayLayers),
        format,
        ToVkComponentMapping(view.ComponentMapping),
        VkImageSubresourceRange
        {
            ToVkImageAspect(aspect),
            view.Range.BaseMipLevel,
            view.Range.LevelCount,
            view.Range.BaseArrayLayer,
            view.Range.LayerCount
        }
    };

    VkImageView imageView = VK_NULL_HANDLE;
    VkResult result = vkCreateImageView(device->Handle().As<VkDevice>(), &viewInfo, nullptr, &imageView);
    if (result != VK_SUCCESS)
    {
        return VK_NULL_HANDLE;
    }

    resourceRegistry.emplace<VkImageView>(new_entity, imageView);

    if (const DebugName* parentName = resourceRegistry.try_get<DebugName>(new_entity))
    {
        setObjectName(VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<uint64_t>(imageView), parentName->WithSuffix("_image_view"));
    }

    return imageView;
}

VkSampler ResourceContextImpl::createSampler(
    entt::entity new_entity,
    const SamplerCreateInfo& sampler_info,
    std::string_view suffix)
{
    const VkSamplerCreateInfo samplerCreateInfo = ToVkSamplerCreateInfo(sampler_info);
    VkSampler sampler = VK_NULL_HANDLE;
    VkResult result = vkCreateSampler(device->Handle().As<VkDevice>(), &samplerCreateInfo, nullptr, &sampler);
    if (result != VK_SUCCESS)
    {
        return VK_NULL_HANDLE;
    }

    resourceRegistry.emplace<VkSampler>(new_entity, sampler);

    // a combined image sampler passes a suffix, so that its sampler is named after the image
    if (const DebugName* parentName = resourceRegistry.try_get<DebugName>(new_entity))
    {
        setObjectName(VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<uint64_t>(sampler), parentName->WithSuffix(suffix));
    }

    return sampler;
}

bool ResourceContextImpl::allocationIsHostVisible(entt::entity entity) const
{
    const VmaAllocation* allocation = resourceRegistry.try_get<VmaAllocation>(entity);
    if (allocation == nullptr)
    {
        return false;
    }
    VkMemoryPropertyFlags properties = 0u;
    vmaGetAllocationMemoryProperties(allocatorHandle, *allocation, &properties);
    return (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0u;
}

void ResourceContextImpl::enqueueBufferUpload(entt::entity entity, InternalResourceDataContainer&& data, const TransferWaitSet& waits, int32_t future_slot)
{
    TransferSystemSetBufferDataMessage setBufferDataMessage
    {
        bufferTransferInfo(entity),
        std::move(data),
        future_slot
    };

    enqueueTransfer(std::move(setBufferDataMessage), std::span(&entity, 1), waits);
}

void ResourceContextImpl::enqueueImageUpload(
    const TransferSystemReqImageInfo& image_info,
    entt::entity entity,
    InternalResourceDataContainer&& data,
    const TransferWaitSet& waits,
    int32_t future_slot)
{
    TransferSystemSetImageDataMessage setImageDataMessage
    {
        image_info,
        std::move(data),
        future_slot
    };

    enqueueTransfer(std::move(setImageDataMessage), std::span(&entity, 1), waits);
}

TransferSystemReqBufferInfo ResourceContextImpl::bufferTransferInfo(entt::entity entity) const
{
    return TransferSystemReqBufferInfo{ resourceRegistry.get<VkBuffer>(entity), resourceRegistry.get<BufferCreateInfo>(entity) };
}

TransferSystemReqImageInfo ResourceContextImpl::imageTransferInfo(entt::entity entity, rhi::ImageUsageTransition usage) const
{
    return TransferSystemReqImageInfo
    {
        resourceRegistry.get<VkImage>(entity),
        resourceRegistry.get<ImageCreateInfo>(entity),
        LayoutBeforeTransfer(usage.Before),
        ImageLayoutFromUsageBits(usage.After)
    };
}

TransferSystemReqImageInfo ResourceContextImpl::newImageTransferInfo(entt::entity entity, ImageUsageBits usage_after) const
{
    return TransferSystemReqImageInfo
    {
        resourceRegistry.get<VkImage>(entity),
        resourceRegistry.get<ImageCreateInfo>(entity),
        VK_IMAGE_LAYOUT_UNDEFINED,
        ImageLayoutFromUsageBits(usage_after)
    };
}

void ResourceContextImpl::setObjectName(VkObjectType object_type, uint64_t handle, const DebugName& name) const
{
    if (!validationEnabled)
    {
        return;
    }
    
    [[maybe_unused]] VkResult result = device->SetObjectName(object_type, handle, name.CString());
    assert(result == VK_SUCCESS);
}

void ResourceContextImpl::destroyEntityResources(entt::entity entity)
{
    const VkDevice vkDevice = device->Handle().As<VkDevice>();

    if (const VkSampler* sampler = resourceRegistry.try_get<VkSampler>(entity))
    {
        vkDestroySampler(vkDevice, *sampler, nullptr);
    }

    if (const VkImageView* imageView = resourceRegistry.try_get<VkImageView>(entity))
    {
        vkDestroyImageView(vkDevice, *imageView, nullptr);
    }

    if (const VkBufferView* bufferView = resourceRegistry.try_get<VkBufferView>(entity))
    {
        vkDestroyBufferView(vkDevice, *bufferView, nullptr);
    }

    const VmaAllocation* allocation = resourceRegistry.try_get<VmaAllocation>(entity);
    if (const UserMapCount* mapCount = resourceRegistry.try_get<UserMapCount>(entity))
    {
        for (int32_t i = 0; i < mapCount->Count; ++i)
        {
            vmaUnmapMemory(allocatorHandle, *allocation);
        }
    }

    // a placed resource has no allocation. VMA then destroys only the Vulkan object, and the heap keeps the memory
    const VmaAllocation allocationHandle = (allocation != nullptr) ? *allocation : VK_NULL_HANDLE;
    if (const VkImage* image = resourceRegistry.try_get<VkImage>(entity))
    {
        vmaDestroyImage(allocatorHandle, *image, allocationHandle);
    }

    if (const VkBuffer* buffer = resourceRegistry.try_get<VkBuffer>(entity))
    {
        vmaDestroyBuffer(allocatorHandle, *buffer, allocationHandle);
    }

    if (resourceRegistry.all_of<MemoryHeap>(entity))
    {
        vmaFreeMemory(allocatorHandle, allocationHandle);
    }

    // the memory is not in use by this resource from here on, so the heap may be destroyed
    if (const PlacedIn* placedIn = resourceRegistry.try_get<PlacedIn>(entity))
    {
        resourceRegistry.get<MemoryHeap>(placedIn->Heap).PlacementCount -= 1;
    }

    resourceRegistry.destroy(entity);
}

namespace
{

    bool HasFlag(ResourceCreationFlags flags, ResourceCreationFlags flag) noexcept
    {
        return (flags & flag) != ResourceCreationFlags::None;
    }

    ResourceDescription DescribeBuffer(const BufferCreateInfo& info) noexcept
    {
        ResourceDescription description{};
        description.Type = ResourceType::Buffer;
        description.Domain = info.Domain;
        description.BufferUsage = info.Usage;
        description.ByteSize = static_cast<uint64_t>(info.Size);
        return description;
    }

    ResourceDescription DescribeImage(const ImageCreateInfo& info, ResourceType type, ImageUsageBits initial_usage) noexcept
    {
        ResourceDescription description{};
        description.Type = type;
        description.Domain = info.Domain;
        description.ImageUsage = info.Usage;
        description.Dimensions = info.Type;
        description.Format = info.Format;
        description.Width = static_cast<int32_t>(info.Width);
        description.Height = static_cast<int32_t>(info.Height);
        description.Depth = static_cast<int32_t>(info.Depth);
        description.MipLevels = static_cast<int32_t>(info.MipLevels);
        description.ArrayLayers = static_cast<int32_t>(info.ArrayLayers);
        description.SampleCount = static_cast<int32_t>(info.SampleCount);
        description.InitialUsage = initial_usage;
        return description;
    }

    ResourceDescription DescribeSampler() noexcept
    {
        ResourceDescription description{};
        description.Type = ResourceType::Sampler;
        return description;
    }

    ResourceDescription DescribeHeap(uint64_t byte_size) noexcept
    {
        ResourceDescription description{};
        description.Type = ResourceType::Heap;
        description.Domain = ResourceDomain::GPUOnly;
        description.ByteSize = byte_size;
        return description;
    }

    bool ImagesHaveTheSameShape(const ImageCreateInfo& src, const ImageCreateInfo& dst) noexcept
    {
        const VkImageAspectFlags srcAspect = ToVkImageAspect(AspectFromVkFormat(ToVkFormat(src.Format)));
        const VkImageAspectFlags dstAspect = ToVkImageAspect(AspectFromVkFormat(ToVkFormat(dst.Format)));
        return srcAspect == dstAspect &&
               src.ArrayLayers == dst.ArrayLayers &&
               src.Width == dst.Width &&
               src.Height == dst.Height &&
               src.Depth == dst.Depth &&
               src.MipLevels == dst.MipLevels;
    }

    bool WaitsAreValid(const TransferWaitSet& waits) noexcept
    {
        for (int32_t i = 0; i < waits.Count; ++i)
        {
            const rhi::TransferWait& wait = waits.Items[static_cast<size_t>(i)];
            if (wait.Semaphore.Get() == 0u || wait.Value == 0u)
            {
                return false;
            }
        }

        return true;
    }

    bool UsageIsValidForImage(ImageUsageBits image_usage, ImageUsageBits usage) noexcept
    {
        return usage != ImageUsageBits::Invalid && (usage & image_usage) == usage;
    }

    bool TransitionIsValidForImage(ImageUsageBits image_usage, rhi::ImageUsageTransition transition) noexcept
    {
        return UsageIsValidForImage(image_usage, transition.Before) && UsageIsValidForImage(image_usage, transition.After);
    }

    bool DestinationTransitionIsValidForImage(ImageUsageBits image_usage, rhi::ImageUsageTransition transition) noexcept
    {
        const bool beforeIsValid = transition.Before == ImageUsageBits::Discard || UsageIsValidForImage(image_usage, transition.Before);
        return beforeIsValid && UsageIsValidForImage(image_usage, transition.After);
    }

    VkImageLayout LayoutBeforeTransfer(ImageUsageBits before) noexcept
    {
        return ImageLayoutFromUsageBits(before);
    }

    bool CopyKindMatches(CopyKind kind, bool src_is_buffer, bool dst_is_buffer) noexcept
    {
        switch (kind)
        {
        case CopyKind::BufferToBuffer: return src_is_buffer && dst_is_buffer;
        case CopyKind::ImageToBuffer: return !src_is_buffer && dst_is_buffer;
        case CopyKind::BufferToImage: return src_is_buffer && !dst_is_buffer;
        case CopyKind::ImageToImage: return !src_is_buffer && !dst_is_buffer;
        default: return false;
        }
    }

    bool RangeIsInside(uint64_t offset, uint64_t size, uint64_t buffer_size) noexcept
    {
        return offset <= buffer_size && size <= buffer_size - offset;
    }

    bool RangesOverlap(uint64_t first_offset, uint64_t first_size, uint64_t second_offset, uint64_t second_size) noexcept
    {
        if (first_size == 0u || second_size == 0u)
        {
            return false;
        }

        return first_offset < second_offset + second_size && second_offset < first_offset + first_size;
    }

    bool BufferCopyRegionsAreValid(std::span<const BufferCopyRegion> regions, uint64_t source_size, uint64_t destination_size, bool is_same_buffer) noexcept
    {
        for (const BufferCopyRegion& region : regions)
        {
            const bool isEmpty = region.Size == 0u;
            const bool isUnaligned = region.SourceOffset % 4u != 0u || region.DestinationOffset % 4u != 0u || region.Size % 4u != 0u;
            const bool isInside = RangeIsInside(region.SourceOffset, region.Size, source_size) &&
                                  RangeIsInside(region.DestinationOffset, region.Size, destination_size);
            if (isEmpty || isUnaligned || !isInside)
            {
                return false;
            }
        }

        if (!is_same_buffer)
        {
            return true;
        }

        const int32_t regionCount = static_cast<int32_t>(regions.size());
        for (int32_t firstIndex = 0; firstIndex < regionCount; ++firstIndex)
        {
            const BufferCopyRegion& first = regions[static_cast<size_t>(firstIndex)];
            for (int32_t secondIndex = 0; secondIndex < regionCount; ++secondIndex)
            {
                const BufferCopyRegion& second = regions[static_cast<size_t>(secondIndex)];
                const bool sourceHitsDestination = RangesOverlap(first.SourceOffset, first.Size, second.DestinationOffset, second.Size);
                const bool destinationsOverlap = firstIndex < secondIndex &&
                                                 RangesOverlap(first.DestinationOffset, first.Size, second.DestinationOffset, second.Size);
                if (sourceHitsDestination || destinationsOverlap)
                {
                    return false;
                }
            }
        }

        return true;
    }

    bool BufferUploadPiecesAreValid(const InternalResourceDataContainer& data, uint64_t buffer_size) noexcept
    {
        const InternalResourceDataContainer::BufferDataVector& pieces =
            std::get<InternalResourceDataContainer::BufferDataVector>(data.DataVector);

        for (const InternalResourceDataContainer::BufferData& piece : pieces)
        {
            const uint64_t pieceSize = static_cast<uint64_t>(piece.Size);
            const bool isUnaligned = piece.DestinationOffset % 4u != 0u || pieceSize % 4u != 0u;
            if (isUnaligned || !RangeIsInside(piece.DestinationOffset, pieceSize, buffer_size))
            {
                return false;
            }
        }

        const int32_t pieceCount = static_cast<int32_t>(pieces.size());
        for (int32_t firstIndex = 0; firstIndex < pieceCount; ++firstIndex)
        {
            const InternalResourceDataContainer::BufferData& first = pieces[static_cast<size_t>(firstIndex)];
            for (int32_t secondIndex = firstIndex + 1; secondIndex < pieceCount; ++secondIndex)
            {
                const InternalResourceDataContainer::BufferData& second = pieces[static_cast<size_t>(secondIndex)];
                if (RangesOverlap(first.DestinationOffset, first.Size, second.DestinationOffset, second.Size))
                {
                    return false;
                }
            }
        }

        return true;
    }

    MipExtent MipExtentOf(const ImageCreateInfo& info, int32_t mip_level) noexcept
    {
        const int32_t shift = std::clamp(mip_level, 0, 31);
        const int32_t baseWidth = static_cast<int32_t>(info.Width);
        const int32_t baseHeight = std::max(static_cast<int32_t>(info.Height), 1);
        const int32_t baseDepth = info.Type == ImageType::Type3D ? std::max(static_cast<int32_t>(info.Depth), 1) : 1;
        return MipExtent{ std::max(baseWidth >> shift, 1), std::max(baseHeight >> shift, 1), std::max(baseDepth >> shift, 1) };
    }

    bool ImageRegionIsValid(const ImageRegion& region, const ImageCreateInfo& info) noexcept
    {
        const bool hasExtent = region.Width > 0 && region.Height > 0 && region.Depth > 0 && region.LayerCount > 0;
        if (!hasExtent)
        {
            return false;
        }

        const int32_t mipLevels = static_cast<int32_t>(info.MipLevels);
        const int32_t arrayLayers = static_cast<int32_t>(info.ArrayLayers);
        const bool mipIsValid = region.MipLevel >= 0 && region.MipLevel < mipLevels;
        const bool layersAreValid = region.BaseArrayLayer >= 0 && region.BaseArrayLayer < arrayLayers &&
                                    region.LayerCount <= arrayLayers - region.BaseArrayLayer;
        if (!mipIsValid || !layersAreValid || region.X < 0 || region.Y < 0 || region.Z < 0)
        {
            return false;
        }

        // 64 bits, so that offset + extent cannot overflow
        const MipExtent mip = MipExtentOf(info, region.MipLevel);
        const int64_t endX = static_cast<int64_t>(region.X) + region.Width;
        const int64_t endY = static_cast<int64_t>(region.Y) + region.Height;
        const int64_t endZ = static_cast<int64_t>(region.Z) + region.Depth;
        if (endX > mip.Width || endY > mip.Height || endZ > mip.Depth)
        {
            return false;
        }

        const int32_t blockEdge = ImageTexelBlockEdge(info.Format);
        const bool startsOnBlockEdge = region.X % blockEdge == 0 && region.Y % blockEdge == 0;
        const bool endsOnBlockEdgeX = endX % blockEdge == 0 || endX == mip.Width;
        const bool endsOnBlockEdgeY = endY % blockEdge == 0 || endY == mip.Height;
        return startsOnBlockEdge && endsOnBlockEdgeX && endsOnBlockEdgeY;
    }

    bool RegionCoversWholeMip(const ImageRegion& region, const ImageCreateInfo& info) noexcept
    {
        const MipExtent mip = MipExtentOf(info, region.MipLevel);
        const bool startsAtOrigin = region.X == 0 && region.Y == 0 && region.Z == 0;
        return startsAtOrigin && region.Width == mip.Width && region.Height == mip.Height && region.Depth == mip.Depth;
    }

    bool ImageBoxesOverlap(const ImageRegion& first, const ImageRegion& second) noexcept
    {
        if (first.MipLevel != second.MipLevel)
        {
            return false;
        }

        const int64_t firstLayerEnd = static_cast<int64_t>(first.BaseArrayLayer) + first.LayerCount;
        const int64_t secondLayerEnd = static_cast<int64_t>(second.BaseArrayLayer) + second.LayerCount;
        const bool layersOverlap = first.BaseArrayLayer < secondLayerEnd && second.BaseArrayLayer < firstLayerEnd;
        const bool xOverlaps = first.X < static_cast<int64_t>(second.X) + second.Width && second.X < static_cast<int64_t>(first.X) + first.Width;
        const bool yOverlaps = first.Y < static_cast<int64_t>(second.Y) + second.Height && second.Y < static_cast<int64_t>(first.Y) + first.Height;
        const bool zOverlaps = first.Z < static_cast<int64_t>(second.Z) + second.Depth && second.Z < static_cast<int64_t>(first.Z) + first.Depth;
        return layersOverlap && xOverlaps && yOverlaps && zOverlaps;
    }

    std::optional<uint64_t> BufferImageFootprintBytes(const BufferImageRegion& region, const ImageFormat& format) noexcept
    {
        const ImageRegion& image = region.Image;
        const uint64_t blockEdge = static_cast<uint64_t>(ImageTexelBlockEdge(format));
        const uint64_t blockBytes = ImageTexelBlockBytes(format);
        const uint64_t rowTexels = static_cast<uint64_t>(region.BufferRowTexels != 0 ? region.BufferRowTexels : image.Width);
        const uint64_t imageRows = static_cast<uint64_t>(region.BufferImageRows != 0 ? region.BufferImageRows : image.Height);
        const uint64_t blocksPerRow = (rowTexels + blockEdge - 1u) / blockEdge;
        const uint64_t blockRowsPerImage = (imageRows + blockEdge - 1u) / blockEdge;
        const uint64_t rowBytes = blocksPerRow * blockBytes;

        // the layers and the depth slices of a box are images in the buffer, one after another. the last image ends at its
        // last texel block, and not at the end of its image height
        const uint64_t imageCount = static_cast<uint64_t>(image.LayerCount) * static_cast<uint64_t>(image.Depth);
        const uint64_t lastImageRows = (static_cast<uint64_t>(image.Height) + blockEdge - 1u) / blockEdge;
        const uint64_t lastImageColumns = (static_cast<uint64_t>(image.Width) + blockEdge - 1u) / blockEdge;
        const uint64_t lastRowBytes = lastImageColumns * blockBytes;

        const std::optional<uint64_t> imageBytes = CheckedProduct(blockRowsPerImage, rowBytes);
        const std::optional<uint64_t> earlierImageBytes = imageBytes.has_value() ? CheckedProduct(imageCount - 1u, *imageBytes) : std::nullopt;
        const std::optional<uint64_t> earlierRowBytes = CheckedProduct(lastImageRows - 1u, rowBytes);
        if (!earlierImageBytes.has_value() || !earlierRowBytes.has_value())
        {
            return std::nullopt;
        }

        const uint64_t largest = std::numeric_limits<uint64_t>::max();
        const bool sumOverflows = *earlierRowBytes > largest - lastRowBytes ||
                                  *earlierImageBytes > largest - (*earlierRowBytes + lastRowBytes);
        if (sumOverflows)
        {
            return std::nullopt;
        }

        return *earlierImageBytes + *earlierRowBytes + lastRowBytes;
    }

    std::optional<uint64_t> CheckedProduct(uint64_t first, uint64_t second) noexcept
    {
        if (first != 0u && second > std::numeric_limits<uint64_t>::max() / first)
        {
            return std::nullopt;
        }

        return first * second;
    }

    bool BufferImageRegionIsValid(const BufferImageRegion& region, const ImageCreateInfo& info, std::optional<uint64_t> checked_buffer_size) noexcept
    {
        if (!ImageRegionIsValid(region.Image, info))
        {
            return false;
        }

        const int32_t blockEdge = ImageTexelBlockEdge(info.Format);
        const bool offsetIsAligned = region.BufferOffset % ImageStagingAlignment(info.Format) == 0u;
        const bool rowsAreValid = region.BufferRowTexels == 0 ||
                                  (region.BufferRowTexels >= region.Image.Width && region.BufferRowTexels % blockEdge == 0);
        const bool imageRowsAreValid = region.BufferImageRows == 0 ||
                                       (region.BufferImageRows >= region.Image.Height && region.BufferImageRows % blockEdge == 0);
        if (!offsetIsAligned || !rowsAreValid || !imageRowsAreValid)
        {
            return false;
        }

        if (!checked_buffer_size.has_value())
        {
            return true;
        }

        const std::optional<uint64_t> footprint = BufferImageFootprintBytes(region, info.Format);
        return footprint.has_value() && RangeIsInside(region.BufferOffset, *footprint, *checked_buffer_size);
    }

    bool ImageToBufferRegionsAreValid(std::span<const BufferImageRegion> regions, const ImageCreateInfo& info, std::optional<uint64_t> checked_buffer_size) noexcept
    {
        for (const BufferImageRegion& region : regions)
        {
            if (!BufferImageRegionIsValid(region, info, checked_buffer_size))
            {
                return false;
            }
        }

        return true;
    }

    bool BufferToImageRegionsAreValid(std::span<const BufferImageRegion> regions, const ImageCreateInfo& info, std::optional<uint64_t> checked_buffer_size, bool image_is_discarded) noexcept
    {
        if (!ImageToBufferRegionsAreValid(regions, info, checked_buffer_size))
        {
            return false;
        }

        const int32_t regionCount = static_cast<int32_t>(regions.size());
        for (int32_t firstIndex = 0; firstIndex < regionCount; ++firstIndex)
        {
            const ImageRegion& first = regions[static_cast<size_t>(firstIndex)].Image;
            if (image_is_discarded && !RegionCoversWholeMip(first, info))
            {
                return false;
            }

            for (int32_t secondIndex = firstIndex + 1; secondIndex < regionCount; ++secondIndex)
            {
                const ImageRegion& second = regions[static_cast<size_t>(secondIndex)].Image;
                if (ImageBoxesOverlap(first, second))
                {
                    return false;
                }
            }
        }

        return true;
    }

    ImageRegion DestinationBoxOf(const ImageCopyRegion& region) noexcept
    {
        ImageRegion box = region.Source;
        box.MipLevel = region.Destination.MipLevel;
        box.BaseArrayLayer = region.Destination.BaseArrayLayer;
        box.X = region.Destination.X;
        box.Y = region.Destination.Y;
        box.Z = region.Destination.Z;
        return box;
    }

    bool ImagesAreCopyCompatible(const ImageCreateInfo& src, const ImageCreateInfo& dst) noexcept
    {
        const VkImageAspectFlags srcAspect = ToVkImageAspect(AspectFromVkFormat(ToVkFormat(src.Format)));
        const VkImageAspectFlags dstAspect = ToVkImageAspect(AspectFromVkFormat(ToVkFormat(dst.Format)));
        return srcAspect == dstAspect &&
               ImageTexelBlockBytes(src.Format) == ImageTexelBlockBytes(dst.Format) &&
               IsCompressedFormat(src.Format) == IsCompressedFormat(dst.Format);
    }

    bool ImageCopyRegionsAreValid(std::span<const ImageCopyRegion> regions, const ImageCreateInfo& src_info, const ImageCreateInfo& dst_info, bool is_same_image, bool destination_is_discarded) noexcept
    {
        const int32_t regionCount = static_cast<int32_t>(regions.size());
        for (int32_t firstIndex = 0; firstIndex < regionCount; ++firstIndex)
        {
            const ImageCopyRegion& first = regions[static_cast<size_t>(firstIndex)];
            const ImageRegion firstDestination = DestinationBoxOf(first);
            const bool isValid = ImageRegionIsValid(first.Source, src_info) && ImageRegionIsValid(firstDestination, dst_info);
            if (!isValid || (destination_is_discarded && !RegionCoversWholeMip(firstDestination, dst_info)))
            {
                return false;
            }

            for (int32_t secondIndex = 0; secondIndex < regionCount; ++secondIndex)
            {
                const ImageCopyRegion& second = regions[static_cast<size_t>(secondIndex)];
                const ImageRegion secondDestination = DestinationBoxOf(second);
                const bool destinationsOverlap = firstIndex < secondIndex && ImageBoxesOverlap(firstDestination, secondDestination);
                const bool sourceHitsDestination = is_same_image && ImageBoxesOverlap(first.Source, secondDestination);
                if (destinationsOverlap || sourceHitsDestination)
                {
                    return false;
                }
            }
        }

        return true;
    }

    void SetUploadPieceDepths(InternalResourceDataContainer& data, const ImageCreateInfo& info) noexcept
    {
        InternalResourceDataContainer::ImageDataVector& pieces = std::get<InternalResourceDataContainer::ImageDataVector>(data.DataVector);
        const int32_t mipLevels = static_cast<int32_t>(info.MipLevels);

        for (InternalResourceDataContainer::ImageData& piece : pieces)
        {
            const bool mipIsValid = piece.MipLevel >= 0 && piece.MipLevel < mipLevels;
            if (piece.Depth == 0 && mipIsValid)
            {
                piece.Depth = MipExtentOf(info, piece.MipLevel).Depth;
            }
        }
    }

    bool ImageUploadPiecesAreValid(const InternalResourceDataContainer& data, const ImageCreateInfo& info, bool image_is_discarded) noexcept
    {
        const InternalResourceDataContainer::ImageDataVector& pieces = std::get<InternalResourceDataContainer::ImageDataVector>(data.DataVector);
        if (pieces.empty())
        {
            return false;
        }

        const int32_t pieceCount = static_cast<int32_t>(pieces.size());
        for (int32_t firstIndex = 0; firstIndex < pieceCount; ++firstIndex)
        {
            const InternalResourceDataContainer::ImageData& first = pieces[static_cast<size_t>(firstIndex)];
            const ImageRegion firstBox{ first.MipLevel, first.ArrayLayer, first.NumLayers, first.X, first.Y, first.Z, first.Width, first.Height, first.Depth };

            // the piece is tightly packed, so its bytes must reach the last texel block of its box
            const BufferImageRegion firstRegion{ 0u, 0, 0, firstBox };
            const bool boxIsValid = ImageRegionIsValid(firstBox, info);
            const std::optional<uint64_t> footprint = boxIsValid ? BufferImageFootprintBytes(firstRegion, info.Format) : std::nullopt;
            const bool hasEnoughBytes = footprint.has_value() && *footprint <= static_cast<uint64_t>(first.Size);
            if (!hasEnoughBytes || (image_is_discarded && !RegionCoversWholeMip(firstBox, info)))
            {
                return false;
            }

            for (int32_t secondIndex = firstIndex + 1; secondIndex < pieceCount; ++secondIndex)
            {
                const InternalResourceDataContainer::ImageData& second = pieces[static_cast<size_t>(secondIndex)];
                const ImageRegion secondBox{ second.MipLevel, second.ArrayLayer, second.NumLayers, second.X, second.Y, second.Z, second.Width, second.Height, second.Depth };
                if (ImageBoxesOverlap(firstBox, secondBox))
                {
                    return false;
                }
            }
        }

        return true;
    }

    BufferImageRegionBlock MakeMipZeroRegionBlock(const ImageCreateInfo& info)
    {
        const MipExtent mip = MipExtentOf(info, 0);
        BufferImageRegion region{};
        region.Image = ImageRegion{ 0, 0, static_cast<int32_t>(info.ArrayLayers), 0, 0, 0, mip.Width, mip.Height, mip.Depth };
        return BufferImageRegionBlock(std::span<const BufferImageRegion>(&region, 1u));
    }

    ImageCopyRegionBlock MakeEveryMipRegionBlock(const ImageCreateInfo& info)
    {
        std::vector<ImageCopyRegion> regions;
        regions.reserve(info.MipLevels);

        for (int32_t mipLevel = 0; mipLevel < static_cast<int32_t>(info.MipLevels); ++mipLevel)
        {
            const MipExtent mip = MipExtentOf(info, mipLevel);
            const ImageRegion wholeMip{ mipLevel, 0, static_cast<int32_t>(info.ArrayLayers), 0, 0, 0, mip.Width, mip.Height, mip.Depth };
            regions.push_back(ImageCopyRegion{ wholeMip, wholeMip });
        }

        return ImageCopyRegionBlock(std::span<const ImageCopyRegion>(regions));
    }

    bool PlacementFits(const VkMemoryRequirements& requirements, uint64_t offset, uint64_t heap_size, uint32_t heap_memory_type_index) noexcept
    {
        const bool isAligned = (offset % requirements.alignment) == 0u;
        // compared as a size left over, so that offset + size cannot overflow
        const bool isInside = offset <= heap_size && requirements.size <= heap_size - offset;
        const bool typeIsAllowed = (requirements.memoryTypeBits & (1u << heap_memory_type_index)) != 0u;
        return isAligned && isInside && typeIsAllowed;
    }

    VkImageAspectFlags BarrierAspectOf(const ImageCreateInfo& info) noexcept
    {
        return ToVkImageAspect(AspectFromVkFormat(ToVkFormat(info.Format)));
    }

    bool TransferOpReadsSource(TransferOpKind kind) noexcept
    {
        switch (kind)
        {
        case TransferOpKind::CopyBuffer:
        case TransferOpKind::CopyBufferToImage:
        case TransferOpKind::CopyImageToBuffer:
        case TransferOpKind::CopyImage:
            return true;
        case TransferOpKind::Invalid:
        case TransferOpKind::UploadBuffer:
        case TransferOpKind::UploadImage:
        case TransferOpKind::FillBuffer:
            break;
        }

        return false;
    }

    bool TransferOpRegionMatchesKind(const TransferSetOp& op) noexcept
    {
        switch (op.Kind)
        {
        case TransferOpKind::UploadBuffer:
        case TransferOpKind::FillBuffer:
        case TransferOpKind::CopyBuffer:
            return std::holds_alternative<BufferCopyRegion>(op.Region);
        case TransferOpKind::UploadImage:
        case TransferOpKind::CopyBufferToImage:
        case TransferOpKind::CopyImageToBuffer:
            return std::holds_alternative<BufferImageRegion>(op.Region);
        case TransferOpKind::CopyImage:
            return std::holds_alternative<ImageCopyRegion>(op.Region);
        case TransferOpKind::Invalid:
            break;
        }

        return false;
    }

    [[maybe_unused]] // compiled out in any build that's not Debug
    bool TransferSetHasRangeHazard(std::span<const TransferSetOp> ops, std::span<const int32_t> range_ends) noexcept
    {
        std::array<RangeAccess, 2u * static_cast<size_t>(k_MaxTransferSetOps)> accesses;
        int32_t rangeStart = 0;

        for (const int32_t rangeEnd : range_ends)
        {
            size_t accessCount = 0u;
            for (int32_t opIndex = rangeStart; opIndex < rangeEnd; ++opIndex)
            {
                const TransferSetOp& op = ops[static_cast<size_t>(opIndex)];
                if (TransferOpReadsSource(op.Kind))
                {
                    accesses[accessCount] = RangeAccess{ op.Source.EntityHandle, false };
                    ++accessCount;
                }

                accesses[accessCount] = RangeAccess{ op.Destination.EntityHandle, true };
                ++accessCount;
            }

            const std::span<RangeAccess> rangeAccesses(accesses.data(), accessCount);
            std::ranges::sort(rangeAccesses, {}, &RangeAccess::Entity);

            // a resource that appears twice is a hazard unless both appearances are reads
            for (size_t index = 1u; index < accessCount; ++index)
            {
                const RangeAccess& earlier = rangeAccesses[index - 1u];
                const RangeAccess& later = rangeAccesses[index];
                if (earlier.Entity == later.Entity && (earlier.IsWrite || later.IsWrite))
                {
                    return true;
                }
            }

            rangeStart = rangeEnd;
        }

        return false;
    }

    size_t CollectTouchedEntities(std::span<const TransferSetOp> ops, std::span<entt::entity> out) noexcept
    {
        size_t count = 0u;
        for (const TransferSetOp& op : ops)
        {
            if (TransferOpReadsSource(op.Kind))
            {
                out[count] = entt::entity(op.Source.EntityHandle);
                ++count;
            }

            out[count] = entt::entity(op.Destination.EntityHandle);
            ++count;
        }

        return count;
    }

    bool TransferSetUploads(std::span<const TransferSetOp> ops) noexcept
    {
        for (const TransferSetOp& op : ops)
        {
            if (op.Kind == TransferOpKind::UploadBuffer || op.Kind == TransferOpKind::UploadImage)
            {
                return true;
            }
        }

        return false;
    }

    uint64_t StagingAlignmentOfSet(const entt::registry& registry, std::span<const TransferSetOp> ops) noexcept
    {
        // the staged data starts on the alignment of a buffer upload, and on the alignment of each image upload format
        uint64_t alignment = k_SetMinimumStagingAlignment;
        for (const TransferSetOp& op : ops)
        {
            if (op.Kind != TransferOpKind::UploadImage)
            {
                continue;
            }

            const ImageCreateInfo& info = registry.get<ImageCreateInfo>(entt::entity(op.Destination.EntityHandle));
            alignment = std::lcm(alignment, ImageStagingAlignment(info.Format));
        }

        return alignment;
    }

    bool CanBeTransferSource(const entt::registry& registry, entt::entity entity) noexcept
    {
        if (const BufferCreateInfo* bufferInfo = registry.try_get<BufferCreateInfo>(entity))
        {
            return (bufferInfo->Usage & BufferUsageBits::TransferSource) != BufferUsageBits::Invalid;
        }
        
        if (const ImageCreateInfo* imageInfo = registry.try_get<ImageCreateInfo>(entity))
        {
            return (imageInfo->Usage & ImageUsageBits::TransferSource) != ImageUsageBits::Invalid;
        }

        return false;
    }

    bool CanBeTransferDestination(const entt::registry& registry, entt::entity entity) noexcept
    {
        if (const BufferCreateInfo* bufferInfo = registry.try_get<BufferCreateInfo>(entity))
        {
            return (bufferInfo->Usage & BufferUsageBits::TransferDestination) != BufferUsageBits::Invalid;
        }
        
        if (const ImageCreateInfo* imageInfo = registry.try_get<ImageCreateInfo>(entity))
        {
            return (imageInfo->Usage & ImageUsageBits::TransferDestination) != ImageUsageBits::Invalid;
        }
        
        return false;
    }

}
