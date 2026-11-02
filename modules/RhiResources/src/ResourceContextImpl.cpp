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

namespace
{
    [[nodiscard]] bool HasFlag(ResourceCreationFlags flags, ResourceCreationFlags flag) noexcept;
    // transfer usage is opt-in (CopySource/CopyDestination, TransferSource/TransferDestination), so every operation
    // that records a transfer checks it here, at the surface, and fails the reply instead of recording invalid commands
    [[nodiscard]] bool CanBeTransferSource(const entt::registry& registry, entt::entity entity) noexcept;
    [[nodiscard]] bool CanBeTransferDestination(const entt::registry& registry, entt::entity entity) noexcept;
    /** @brief Image to image copies need the same aspect, extent, layers and mips (for now). */
    [[nodiscard]] bool ImagesHaveTheSameShape(const ImageCreateInfo& src, const ImageCreateInfo& dst) noexcept;
}

ResourceContextError ResourceContextImpl::Construct(const ResourceContextCreateInfo& create_info)
{
    if (device != nullptr)
    {
        return ResourceContextError::AlreadyInitialized;
    }

    // one slot is shared by the futures of a full table, so a table of one slot could hold no future
    if (create_info.Instance == nullptr || create_info.Device == nullptr || create_info.MaxFutures < 2)
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

    const ResourceContextError transferResult = transferSystem.Initialize(create_info.Device, instance, create_info.VulkanApiVersion, &messageQueue, &futures);
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
    std::vector<entt::entity> liveEntities;
    for (const entt::entity entity : resourceRegistry.view<ResourceFlags>())
    {
        liveEntities.push_back(entity);
    }
    
    for (const entt::entity entity : liveEntities)
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
        futures.FullCount()
    };
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
        enqueueBufferUpload(newEntity, std::move(*message.InitialData), message.Future);
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

    const std::expected<GraphicsResource, rhi::ResourceError> createdResource = createImageEntity(message.ImageInfo, message.Name);
    if (!createdResource.has_value())
    {
        futures.Fail(message.Future, createdResource.error());
        return;
    }

    if (message.InitialData.has_value())
    {
        futures.StoreResource(message.Future, *createdResource);
        enqueueImageUpload(entt::entity(createdResource->EntityHandle), std::move(*message.InitialData), message.Future);
        return;
    }

    futures.CompleteWithResource(message.Future, *createdResource);
}

std::expected<GraphicsResource, rhi::ResourceError> ResourceContextImpl::createBufferEntity(const BufferCreateInfo& info, const DebugName& name)
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
    const BufferCreateInfo& storedInfo = resourceRegistry.emplace<BufferCreateInfo>(newEntity, info);
    if (!name.Empty())
    {
        resourceRegistry.emplace<DebugName>(newEntity, name);
    }

    VkBuffer bufferHandle = createBuffer(newEntity, storedInfo);
    if (bufferHandle == VK_NULL_HANDLE)
    {
        destroyEntityResources(newEntity);
        return std::unexpected(rhi::ResourceError::AllocationFailed);
    }

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
        info.Domain,
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
    if (info.Domain == ResourceDomain::Invalid || info.Width == 0u || !info.Format.IsValid())
    {
        return std::unexpected(rhi::ResourceError::InvalidCreateInfo);
    }

    const ResourceType resourceType = info.SamplerInfo.has_value() ? ResourceType::CombinedImageSampler : ResourceType::Image;
    const entt::entity newEntity = resourceRegistry.create();
    resourceRegistry.emplace<ResourceFlags>(newEntity, resourceType, info.Flags, info.Domain);
    const ImageCreateInfo& storedInfo = resourceRegistry.emplace<ImageCreateInfo>(newEntity, info);
    resourceRegistry.emplace<TrackedImageLayout>(newEntity);
    if (!name.Empty())
    {
        resourceRegistry.emplace<DebugName>(newEntity, name);
    }

    VkImage imageHandle = createImage(newEntity, storedInfo);
    if (imageHandle == VK_NULL_HANDLE)
    {
        destroyEntityResources(newEntity);
        return std::unexpected(rhi::ResourceError::AllocationFailed);
    }

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
        info.Domain,
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
        setBufferDataHostOnly(*entity, message.Data);
        futures.Complete(message.Future);
        return;
    }

    if (!CanBeTransferDestination(resourceRegistry, *entity))
    {
        futures.Fail(message.Future, rhi::ResourceError::MissingTransferUsage);
        return;
    }

    enqueueBufferUpload(*entity, std::move(message.Data), message.Future);
}

void ResourceContextImpl::processMessage(SetImageDataMessage&& message)
{
    const std::expected<entt::entity, rhi::ResourceError> entity = resolveImage(message.DestImage);
    if (!entity.has_value())
    {
        futures.Fail(message.Future, entity.error());
        return;
    }

    if (!CanBeTransferDestination(resourceRegistry, *entity))
    {
        futures.Fail(message.Future, rhi::ResourceError::MissingTransferUsage);
        return;
    }

    enqueueImageUpload(*entity, std::move(message.Data), message.Future);
}

void ResourceContextImpl::processMessage(FillResourceMessage&& message)
{
    const std::expected<entt::entity, rhi::ResourceError> entity = resolveBuffer(message.Resource);
    if (!entity.has_value())
    {
        futures.Fail(message.Future, entity.error());
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

    enqueueTransfer(std::move(fillBufferMessage), std::span(&*entity, 1));
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

    const VmaAllocation allocation = resourceRegistry.get<VmaAllocation>(*entity);
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

    // the source's name string belongs to the source, so the copy goes unnamed. a sampler has no copy path
    std::expected<GraphicsResource, rhi::ResourceError> created = std::unexpected(rhi::ResourceError::UnsupportedCopy);
    if (const BufferCreateInfo* bufferInfo = resourceRegistry.try_get<BufferCreateInfo>(srcEntity))
    {
        BufferCreateInfo copyInfo = *bufferInfo;
        copyInfo.Name = {};
        if (message.CopyContents)
        {
            copyInfo.Usage |= BufferUsageBits::CopyDestination;
        }
        created = createBufferEntity(copyInfo, DebugName{});
    }
    else if (const ImageCreateInfo* imageInfo = resourceRegistry.try_get<ImageCreateInfo>(srcEntity))
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
    CopyResourceContentsMessage copyMessage{ message.SourceResource, *created, message.Future };
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

    if (!CanBeTransferSource(resourceRegistry, srcEntity) || !CanBeTransferDestination(resourceRegistry, dstEntity))
    {
        futures.Fail(message.Future, rhi::ResourceError::MissingTransferUsage);
        return;
    }

    // the transfer thread trusts every message it gets, so the checks that need both create infos happen here
    const std::array<entt::entity, 2> touched{ srcEntity, dstEntity };
    if (srcIsBuffer && dstIsBuffer)
    {
        if (resourceRegistry.get<BufferCreateInfo>(srcEntity).Size > resourceRegistry.get<BufferCreateInfo>(dstEntity).Size)
        {
            futures.Fail(message.Future, rhi::ResourceError::CopySizeMismatch);
            return;
        }

        enqueueTransfer(TransferSystemCopyBufferToBufferMessage{ bufferTransferInfo(srcEntity), bufferTransferInfo(dstEntity), message.Future }, touched);
        return;
    }
    else if (srcIsImage && dstIsImage)
    {
        if (!ImagesHaveTheSameShape(resourceRegistry.get<ImageCreateInfo>(srcEntity), resourceRegistry.get<ImageCreateInfo>(dstEntity)))
        {
            futures.Fail(message.Future, rhi::ResourceError::CopyShapeMismatch);
            return;
        }

        enqueueTransfer(TransferSystemCopyImageToImageMessage{ imageTransferInfo(srcEntity), imageTransferInfo(dstEntity), message.Future }, touched);
        return;
    }
    else if (srcIsBuffer && dstIsImage)
    {
        enqueueTransfer(TransferSystemCopyBufferToImageMessage{ bufferTransferInfo(srcEntity), imageTransferInfo(dstEntity), message.Future }, touched);
        return;
    }

    enqueueTransfer(TransferSystemCopyImageToBufferMessage{ imageTransferInfo(srcEntity), bufferTransferInfo(dstEntity), message.Future }, touched);
}

void ResourceContextImpl::processMessage(DestroyResourceMessage&& message)
{
    const std::expected<entt::entity, rhi::ResourceError> entity = resolveResource(message.Resource);
    if (!entity.has_value())
    {
        futures.Fail(message.Future, entity.error());
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
void ResourceContextImpl::enqueueTransfer(TransferMessage&& message, std::span<const entt::entity> touched_entities)
{
    // the worker is the only producer for the transfer queue, so a plain counter gives tickets that rise in queue order
    const uint64_t ticket = ++lastIssuedTicket;
    message.Ticket = ticket;
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
    const VkBufferCreateInfo vkBufferInfo
    {
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0u,
        static_cast<VkDeviceSize>(buffer_info.Size),
        ToVkBufferUsage(buffer_info.Usage, buffer_info.ViewFormat.has_value()),
        queueFamilyIndices.size() > 1u ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
        static_cast<uint32_t>(queueFamilyIndices.size()),
        queueFamilyIndices.data()
    };

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
    const VkImageCreateInfo vkImageInfo
    {
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        nullptr,
        image_info.ViewInfo.has_value() && image_info.ViewInfo->ViewType == ImageViewType::Cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0u,
        ToVkImageType(image_info.Type),
        ToVkFormat(image_info.Format),
        VkExtent3D{ image_info.Width, std::max(image_info.Height, 1u), std::max(image_info.Depth, 1u) },
        image_info.MipLevels,
        image_info.ArrayLayers,
        // the VkSampleCountFlagBits values are the counts themselves
        static_cast<VkSampleCountFlagBits>(std::max(image_info.SampleCount, 1u)),
        VK_IMAGE_TILING_OPTIMAL,
        ToVkImageUsage(image_info.Usage),
        queueFamilyIndices.size() > 1u ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
        static_cast<uint32_t>(queueFamilyIndices.size()),
        queueFamilyIndices.data(),
        VK_IMAGE_LAYOUT_UNDEFINED
    };

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

void ResourceContextImpl::enqueueBufferUpload(entt::entity entity, InternalResourceDataContainer&& data, int32_t future_slot)
{
    TransferSystemSetBufferDataMessage setBufferDataMessage
    {
        bufferTransferInfo(entity),
        std::move(data),
        future_slot
    };

    enqueueTransfer(std::move(setBufferDataMessage), std::span(&entity, 1));
}

void ResourceContextImpl::enqueueImageUpload(entt::entity entity, InternalResourceDataContainer&& data, int32_t future_slot)
{
    TransferSystemSetImageDataMessage setImageDataMessage
    {
        imageTransferInfo(entity),
        std::move(data),
        future_slot
    };

    enqueueTransfer(std::move(setImageDataMessage), std::span(&entity, 1));
}

TransferSystemReqBufferInfo ResourceContextImpl::bufferTransferInfo(entt::entity entity) const
{
    return TransferSystemReqBufferInfo{ resourceRegistry.get<VkBuffer>(entity), resourceRegistry.get<BufferCreateInfo>(entity) };
}

TransferSystemReqImageInfo ResourceContextImpl::imageTransferInfo(entt::entity entity)
{
    const ImageCreateInfo& createInfo = resourceRegistry.get<ImageCreateInfo>(entity);
    TrackedImageLayout& tracked = resourceRegistry.get<TrackedImageLayout>(entity);
    TransferSystemReqImageInfo result{ resourceRegistry.get<VkImage>(entity), createInfo, tracked.Layout };
    // the transfer system leaves every image it touches in this layout
    tracked.Layout = ImageLayoutAfterTransfer(createInfo.Usage);
    return result;
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

    if (const VkImage* image = resourceRegistry.try_get<VkImage>(entity))
    {
        vmaDestroyImage(allocatorHandle, *image, *allocation);
    }

    if (const VkBuffer* buffer = resourceRegistry.try_get<VkBuffer>(entity))
    {
        vmaDestroyBuffer(allocatorHandle, *buffer, *allocation);
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
