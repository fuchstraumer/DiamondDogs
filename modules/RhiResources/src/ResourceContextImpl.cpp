#include "ResourceContextImpl.hpp"
#include "ImageDataFormatsVulkan.hpp"

#include "ResourceContext.hpp"
#include "Device.hpp"
#include "Instance.hpp"
#include "ResourceFlags.hpp"
#include "FutureTable.hpp"
#include "ResourceFuture.hpp"
#include "ResourceMessageTypesInternal.hpp"
#include "ResourceTypes.hpp"
#include "RhiFlags.hpp"
#include "RhiSystem.hpp"
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
#include <memory>
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
    // transfer usage is opt-in (CopySource/CopyDestination, TransferSource/TransferDestination), so every operation
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
    /** @brief Returns true when a resource with these requirements fits at the offset: aligned, inside the heap, and
     *  of a memory type that the resource allows. */
    [[nodiscard]] bool PlacementFits(const VkMemoryRequirements& requirements, uint64_t offset, uint64_t heap_size, uint32_t heap_memory_type_index) noexcept;
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
        transfer.StagingRingBytesInUse
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
        message.BufferInfo.Usage |= BufferUsageBits::CopyDestination;
    }

    const std::expected<GraphicsResource, rhi::ResourceError> createdResource = createBufferEntity(message.BufferInfo, message.Name);
    if (!createdResource.has_value())
    {
        futures.Fail(message.Future, createdResource.error());
        return;
    }

    const entt::entity newEntity = entt::entity(createdResource->EntityHandle);
    if (message.InitialData.has_value() && !allocationIsHostVisible(newEntity))
    {
        // pass responsiblity on to transfer system now, transferring ownership of data with a move
        futures.StoreResource(message.Future, *createdResource);
        enqueueBufferUpload(newEntity, std::move(*message.InitialData), TransferWaitSet{}, message.Future);
        return;
    }

    if (message.InitialData.has_value())
    {
        setBufferDataHostOnly(newEntity, *message.InitialData);
    }

    futures.CompleteWithResource(message.Future, *createdResource);
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
        futures.StoreResource(message.Future, *createdResource);
        const entt::entity newEntity = entt::entity(createdResource->EntityHandle);
        const TransferSystemReqImageInfo imageInfo = newImageTransferInfo(newEntity, message.UsageAfterUpload);
        enqueueImageUpload(imageInfo, newEntity, std::move(*message.InitialData), TransferWaitSet{}, message.Future);
        return;
    }

    futures.CompleteWithResource(message.Future, *createdResource);
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
    if ((storedInfo.Usage & BufferUsageBits::DeviceAddressBuffer) != BufferUsageBits::Invalid)
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

    futures.CompleteWithResource(message.Future, created);
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

    futures.CompleteWithResource(message.Future, created);
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

    futures.CompleteWithResource(message.Create.Future, *created);
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

    futures.CompleteWithResource(message.Create.Future, *created);
}

void ResourceContextImpl::processMessage(SetBufferDataMessage&& message)
{
    const std::expected<entt::entity, rhi::ResourceError> entity = resolveBuffer(message.DestBuffer);
    if (!entity.has_value())
    {
        futures.Fail(message.Future, entity.error());
        return;
    }

    if (allocationIsHostVisible(*entity))
    {
        // the worker writes this memory at once, so a wait would block the worker thread
        if (message.Waits.Count > 0)
        {
            futures.Fail(message.Future, rhi::ResourceError::WaitOnHostWrite);
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

    // the source's name string belongs to the source, so the copy goes unnamed. a sampler has no copy path
    std::expected<GraphicsResource, rhi::ResourceError> created = std::unexpected(rhi::ResourceError::UnsupportedCopy);
    if (bufferInfo != nullptr)
    {
        BufferCreateInfo copyInfo = *bufferInfo;
        copyInfo.Name = {};
        if (message.CopyContents)
        {
            copyInfo.Usage |= BufferUsageBits::CopyDestination;
        }

        created = createBufferEntity(copyInfo, DebugName{});
    }
    else if (imageInfo != nullptr)
    {
        ImageCreateInfo copyInfo = *imageInfo;
        copyInfo.Name = {};
        if (message.CopyContents)
        {
            copyInfo.Usage |= ImageUsageBits::TransferDestination;
        }

        created = createImageEntity(copyInfo, DebugName{});
    }

    if (!created.has_value())
    {
        futures.Fail(message.Future, created.error());
        return;
    }

    if (!message.CopyContents)
    {
        futures.CompleteWithResource(message.Future, *created);
        return;
    }

    futures.StoreResource(message.Future, *created);

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
    const std::array<entt::entity, 2> touched{ srcEntity, dstEntity };
    if (message.Kind == CopyKind::BufferToBuffer)
    {
        if (resourceRegistry.get<BufferCreateInfo>(srcEntity).Size > resourceRegistry.get<BufferCreateInfo>(dstEntity).Size)
        {
            futures.Fail(message.Future, rhi::ResourceError::CopySizeMismatch);
            return;
        }

        enqueueTransfer(TransferSystemCopyBufferToBufferMessage{ bufferTransferInfo(srcEntity), bufferTransferInfo(dstEntity), message.Future }, touched, message.Waits);
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
        if (!ImagesHaveTheSameShape(resourceRegistry.get<ImageCreateInfo>(srcEntity), resourceRegistry.get<ImageCreateInfo>(dstEntity)))
        {
            futures.Fail(message.Future, rhi::ResourceError::CopyShapeMismatch);
            return;
        }

        const TransferSystemReqImageInfo srcInfo = imageTransferInfo(srcEntity, message.SourceUsage);
        const TransferSystemReqImageInfo dstInfo = message.DestinationStartsUndefined ?
            newImageTransferInfo(dstEntity, message.DestinationUsage.After) :
            imageTransferInfo(dstEntity, message.DestinationUsage);
        enqueueTransfer(TransferSystemCopyImageToImageMessage{ srcInfo, dstInfo, message.Future }, touched, message.Waits);
        return;
    }

    if (message.Kind == CopyKind::BufferToImage)
    {
        const TransferSystemReqImageInfo dstInfo = imageTransferInfo(dstEntity, message.DestinationUsage);
        enqueueTransfer(TransferSystemCopyBufferToImageMessage{ bufferTransferInfo(srcEntity), dstInfo, message.Future }, touched, message.Waits);
        return;
    }

    const TransferSystemReqImageInfo srcInfo = imageTransferInfo(srcEntity, message.SourceUsage);
    enqueueTransfer(TransferSystemCopyImageToBufferMessage{ srcInfo, bufferTransferInfo(dstEntity), message.Future }, touched, message.Waits);
}

void ResourceContextImpl::processMessage(DestroyResourceMessage&& message)
{
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

    return true;
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
        VkResult result = vmaMapMemory(allocatorHandle, alloc, &mappedAddress);
        assert(result == VK_SUCCESS);
    }

    InternalResourceDataContainer::BufferDataVector& dataVector =
        std::get<InternalResourceDataContainer::BufferDataVector>(data_container.DataVector);

    size_t offset = 0u;
    for (const InternalResourceDataContainer::BufferData& data : dataVector)
    {
        std::memcpy(static_cast<std::byte*>(mappedAddress) + offset, data.Data.get(), data.Size);
        offset += data.Size;
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
    
    VkResult result = rhi::RhiSystem::SetObjectName(object_type, handle, name.CString());
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

    bool PlacementFits(const VkMemoryRequirements& requirements, uint64_t offset, uint64_t heap_size, uint32_t heap_memory_type_index) noexcept
    {
        const bool isAligned = (offset % requirements.alignment) == 0u;
        // compared as a size left over, so that offset + size cannot overflow
        const bool isInside = offset <= heap_size && requirements.size <= heap_size - offset;
        const bool typeIsAllowed = (requirements.memoryTypeBits & (1u << heap_memory_type_index)) != 0u;
        return isAligned && isInside && typeIsAllowed;
    }

    bool CanBeTransferSource(const entt::registry& registry, entt::entity entity) noexcept
    {
        if (const BufferCreateInfo* bufferInfo = registry.try_get<BufferCreateInfo>(entity))
        {
            return (bufferInfo->Usage & BufferUsageBits::CopySource) != BufferUsageBits::Invalid;
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
            return (bufferInfo->Usage & BufferUsageBits::CopyDestination) != BufferUsageBits::Invalid;
        }
        
        if (const ImageCreateInfo* imageInfo = registry.try_get<ImageCreateInfo>(entity))
        {
            return (imageInfo->Usage & ImageUsageBits::TransferDestination) != ImageUsageBits::Invalid;
        }
        
        return false;
    }

}
