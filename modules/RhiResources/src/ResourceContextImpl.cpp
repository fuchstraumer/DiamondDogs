#include "ResourceContextImpl.hpp"
#include "ImageDataFormatsVulkan.hpp"

#include "ResourceContext.hpp"
#include "Device.hpp"
#include "Instance.hpp"
#include "ResourceFlags.hpp"
#include "ResourceMessageReply.hpp"
#include "ResourceMessageTypesInternal.hpp"
#include "ResourceTypes.hpp"
#include "RhiFlags.hpp"
#include "RhiSystem.hpp"
#include "VulkanConversions.hpp"
#include "entt/entity/fwd.hpp"
#include "vulkan/vulkan_core.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

namespace
{
    [[nodiscard]] bool HasFlag(ResourceCreationFlags flags, ResourceCreationFlags flag) noexcept;
    [[nodiscard]] const char* NameFromUserData(ResourceCreationFlags flags, const void* user_data) noexcept;
    // transfer usage is opt-in (CopySource/CopyDestination, TransferSource/TransferDestination), so every operation
    // that records a transfer checks it here, at the surface, and fails the reply instead of recording invalid commands
    [[nodiscard]] bool CanBeTransferSource(const entt::registry& registry, entt::entity entity) noexcept;
    [[nodiscard]] bool CanBeTransferDestination(const entt::registry& registry, entt::entity entity) noexcept;
}

void ResourceContextImpl::construct(const ResourceContextCreateInfo& create_info)
{
    device = create_info.Device;
    validationEnabled = create_info.ValidationEnabled;

    const rhi::QueueFamilyIndices& families = device->GetQueueFamilyIndices();
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
    createInfo.physicalDevice = device->GetPhysicalDevice().As<VkPhysicalDevice>();
    createInfo.device = device->Handle().As<VkDevice>();
    createInfo.instance = instance;
    createInfo.vulkanApiVersion = create_info.VulkanApiVersion;

    VkResult result = vmaCreateAllocator(&createInfo, &allocatorHandle);
    assert(result == VK_SUCCESS);

    transferSystem.Initialize(device, instance, create_info.VulkanApiVersion);

    startWorker();
}

void ResourceContextImpl::destroy()
{
    if (device == nullptr)
    {
        return;
    }

    setExitWorker();

    // worker stopped, now it's this threads job to drain the queue
    while (true)
    {
        std::optional<ResourceMessagePayloadType> message = messageQueue.try_pop();
        if (!message.has_value())
        {
            break;
        }
        processMessage(std::move(*message));
    }

    // destroy transfer system, which may have pending resources and transfers
    transferSystem.destroy();

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

void ResourceContextImpl::update()
{
    transferSystem.ForceCompleteTransfers();
}

void ResourceContextImpl::pushMessage(ResourceMessagePayloadType message)
{
    messageQueue.push(std::move(message));
}

void ResourceContextImpl::setExitWorker()
{
    if (workerThread.joinable())
    {
        // messages pushed before this are processed by worker, any pushed after it by the drain in destroy()
        messageQueue.push(ExitWorkerMessage{});
        workerThread.join();
    }
}

void ResourceContextImpl::startWorker()
{
    workerThread = std::thread(&ResourceContextImpl::processMessages, this);
}

const rhi::Device* ResourceContextImpl::Device() const noexcept
{
    return device;
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
        processMessage(std::move(message));
    }
}

void ResourceContextImpl::processMessage(ResourceMessagePayloadType&& message)
{
    // surely there has to be a better way than this. why is std::visit like this
    auto messageVisitor =
        [this](auto&& arg)
        {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, CreateBufferMessage>)
            {
                processCreateBufferMessage(std::move(arg));
            }
            else if constexpr (std::is_same_v<T, CreateImageMessage>)
            {
                processCreateImageMessage(std::move(arg));
            }
            else if constexpr (std::is_same_v<T, CreateSamplerMessage>)
            {
                processCreateSamplerMessage(std::move(arg));
            }
            else if constexpr (std::is_same_v<T, SetBufferDataMessage>)
            {
                processSetBufferDataMessage(std::move(arg));
            }
            else if constexpr (std::is_same_v<T, SetImageDataMessage>)
            {
                processSetImageDataMessage(std::move(arg));
            }
            else if constexpr (std::is_same_v<T, FillResourceMessage>)
            {
                processFillResourceMessage(std::move(arg));
            }
            else if constexpr (std::is_same_v<T, MapResourceMessage>)
            {
                processMapResourceMessage(std::move(arg));
            }
            else if constexpr (std::is_same_v<T, UnmapResourceMessage>)
            {
                processUnmapResourceMessage(std::move(arg));
            }
            else if constexpr (std::is_same_v<T, CopyResourceMessage>)
            {
                processCopyResourceMessage(std::move(arg));
            }
            else if constexpr (std::is_same_v<T, CopyResourceContentsMessage>)
            {
                processCopyResourceContentsMessage(std::move(arg));
            }
            else if constexpr (std::is_same_v<T, DestroyResourceMessage>)
            {
                processDestroyResourceMessage(std::move(arg));
            }
        };

    std::visit(messageVisitor, std::move(message));
}

void ResourceContextImpl::processCreateBufferMessage(CreateBufferMessage&& message)
{
    message.reply->setStatus(MessageReply::Status::Pending);

    // a staged upload needs the destination bit. host-visible domains are written directly and never stage
    if (message.initialData.has_value() && !DomainIsHostVisible(message.bufferInfo.Domain))
    {
        message.bufferInfo.Usage |= BufferUsageBits::CopyDestination;
    }

    std::optional<GraphicsResource> createdResource = createBufferEntity(message.bufferInfo);
    if (!createdResource.has_value())
    {
        message.reply->setStatus(MessageReply::Status::Failed);
        return;
    }

    const entt::entity new_entity = entt::entity(createdResource->EntityHandle);
    if (message.initialData.has_value() && !allocationIsHostVisible(new_entity))
    {
        // pass responsiblity on to transfer system now, transferring ownership of data with a move
        message.reply->setGraphicsResourceRelaxed(*createdResource);
        message.reply->setStatus(MessageReply::Status::Transferring);
        enqueueBufferUpload(new_entity, std::move(*message.initialData), std::move(message.reply));
        return;
    }

    if (message.initialData.has_value())
    {
        setBufferDataHostOnly(new_entity, *message.initialData);
    }

    completeWithResource(*message.reply, *createdResource);
}

void ResourceContextImpl::processCreateImageMessage(CreateImageMessage&& message)
{
    message.reply->setStatus(MessageReply::Status::Pending);

    if (message.initialData.has_value())
    {
        message.imageInfo.Usage |= ImageUsageBits::TransferDestination;
    }

    std::optional<GraphicsResource> createdResource = createImageEntity(message.imageInfo);
    if (!createdResource.has_value())
    {
        message.reply->setStatus(MessageReply::Status::Failed);
        return;
    }

    if (message.initialData.has_value())
    {
        message.reply->setGraphicsResourceRelaxed(*createdResource);
        message.reply->setStatus(MessageReply::Status::Transferring);
        enqueueImageUpload(entt::entity(createdResource->EntityHandle), std::move(*message.initialData), std::move(message.reply));
        return;
    }

    completeWithResource(*message.reply, *createdResource);
}

std::optional<GraphicsResource> ResourceContextImpl::createBufferEntity(const BufferCreateInfo& info)
{
    if (info.Domain == ResourceDomain::Invalid || info.Size == 0u || !BufferUsageIsSupported(info.Usage))
    {
        return std::nullopt;
    }

    const entt::entity newEntity = resourceRegistry.create();
    resourceRegistry.emplace<ResourceFlags>(newEntity, ResourceType::Buffer, info.Flags, info.Domain);
    const BufferCreateInfo& storedInfo = resourceRegistry.emplace<BufferCreateInfo>(newEntity, info);

    VkBuffer bufferHandle = createBuffer(newEntity, storedInfo);
    if (bufferHandle == VK_NULL_HANDLE)
    {
        destroyEntityResources(newEntity);
        return std::nullopt;
    }

    VkBufferView bufferView = VK_NULL_HANDLE;
    if (storedInfo.ViewFormat.has_value())
    {
        bufferView = createBufferView(newEntity, storedInfo);
        if (bufferView == VK_NULL_HANDLE)
        {
            destroyEntityResources(newEntity);
            return std::nullopt;
        }
    }

    return GraphicsResource
    {
        info.Domain,
        ResourceType::Buffer,
        static_cast<uint32_t>(newEntity),
        reinterpret_cast<uint64_t>(bufferHandle),
        reinterpret_cast<uint64_t>(bufferView),
        0u
    };
}

std::optional<GraphicsResource> ResourceContextImpl::createImageEntity(const ImageCreateInfo& info)
{
    if (info.Domain == ResourceDomain::Invalid || info.Width == 0u || !info.Format.IsValid())
    {
        return std::nullopt;
    }

    const ResourceType resourceType = info.SamplerInfo.has_value() ? ResourceType::CombinedImageSampler : ResourceType::Image;
    const entt::entity newEntity = resourceRegistry.create();
    resourceRegistry.emplace<ResourceFlags>(newEntity, resourceType, info.Flags, info.Domain);
    const ImageCreateInfo& storedInfo = resourceRegistry.emplace<ImageCreateInfo>(newEntity, info);
    resourceRegistry.emplace<TrackedImageLayout>(newEntity);

    VkImage imageHandle = createImage(newEntity, storedInfo);
    if (imageHandle == VK_NULL_HANDLE)
    {
        destroyEntityResources(newEntity);
        return std::nullopt;
    }

    VkImageView imageView = VK_NULL_HANDLE;
    if (storedInfo.ViewInfo.has_value())
    {
        imageView = createImageView(newEntity, storedInfo);
        if (imageView == VK_NULL_HANDLE)
        {
            destroyEntityResources(newEntity);
            return std::nullopt;
        }
    }

    VkSampler sampler = VK_NULL_HANDLE;
    if (storedInfo.SamplerInfo.has_value())
    {
        sampler = createSampler(newEntity, *storedInfo.SamplerInfo, storedInfo.Flags, storedInfo.UserData);
        if (sampler == VK_NULL_HANDLE)
        {
            destroyEntityResources(newEntity);
            return std::nullopt;
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

void ResourceContextImpl::completeWithResource(GraphicsResourceReply& reply, const GraphicsResource& resource) noexcept
{
    reply.setGraphicsResource(
        resource.Domain,
        resource.Type,
        resource.EntityHandle,
        resource.VkHandle,
        resource.VkViewHandle,
        resource.VkSamplerHandle);
}

void ResourceContextImpl::processCreateSamplerMessage(CreateSamplerMessage&& message)
{
    message.reply->setStatus(MessageReply::Status::Pending);

    const entt::entity newEntity = resourceRegistry.create();
    resourceRegistry.emplace<ResourceFlags>(newEntity, ResourceType::Sampler, ResourceCreationFlags::None, ResourceDomain::GPUOnly);
    VkSampler sampler = createSampler(newEntity, message.samplerInfo, ResourceCreationFlags::None, message.userData);
    if (sampler == VK_NULL_HANDLE)
    {
        destroyEntityResources(newEntity);
        message.reply->setStatus(MessageReply::Status::Failed);
        return;
    }

    message.reply->setGraphicsResource(
        ResourceDomain::GPUOnly,
        ResourceType::Sampler,
        static_cast<uint32_t>(newEntity),
        0u, 0u, reinterpret_cast<uint64_t>(sampler));
}

void ResourceContextImpl::processSetBufferDataMessage(SetBufferDataMessage&& message)
{
    const entt::entity entity = entt::entity(message.destBuffer.EntityHandle);
    if (!resourceRegistry.valid(entity) || !resourceRegistry.all_of<VkBuffer>(entity))
    {
        message.reply->setStatus(MessageReply::Status::Failed);
        return;
    }

    if (allocationIsHostVisible(entity))
    {
        setBufferDataHostOnly(entity, message.data);
        message.reply->setStatus(MessageReply::Status::Completed);
        return;
    }

    if (!CanBeTransferDestination(resourceRegistry, entity))
    {
        message.reply->setStatus(MessageReply::Status::Failed);
        return;
    }

    message.reply->setStatus(MessageReply::Status::Transferring);
    enqueueBufferUpload(entity, std::move(message.data), std::move(message.reply));
}

void ResourceContextImpl::processSetImageDataMessage(SetImageDataMessage&& message)
{
    const entt::entity entity = entt::entity(message.destImage.EntityHandle);
    if (!resourceRegistry.valid(entity) || !resourceRegistry.all_of<VkImage>(entity) || !CanBeTransferDestination(resourceRegistry, entity))
    {
        message.reply->setStatus(MessageReply::Status::Failed);
        return;
    }

    message.reply->setStatus(MessageReply::Status::Transferring);
    enqueueImageUpload(entity, std::move(message.data), std::move(message.reply));
}

void ResourceContextImpl::processFillResourceMessage(FillResourceMessage&& message)
{
    const entt::entity entity = entt::entity(message.resource.EntityHandle);
    if (!resourceRegistry.valid(entity) || !resourceRegistry.all_of<VkBuffer>(entity) || !CanBeTransferDestination(resourceRegistry, entity))
    {
        message.reply->setStatus(MessageReply::Status::Failed);
        return;
    }

    // vkCmdFillBuffer needs both multiples of 4 (or VK_WHOLE_SIZE), so we check here rather than fail in the transfer system
    const bool sizeIsValid = (message.size == VK_WHOLE_SIZE) || (message.size % 4u == 0u);
    if (message.offset % 4u != 0u || !sizeIsValid)
    {
        message.reply->setStatus(MessageReply::Status::Failed);
        return;
    }

    message.reply->setStatus(MessageReply::Status::Transferring);

    TransferSystemFillBufferMessage fillBufferMessage
    {
        bufferTransferInfo(entity),
        message.value,
        message.offset,
        message.size,
        std::move(message.reply)
    };

    transferSystem.EnqueueTransfer(std::move(fillBufferMessage));
}

void ResourceContextImpl::processMapResourceMessage(MapResourceMessage&& message)
{
    const entt::entity entity = entt::entity(message.resource.EntityHandle);
    if (!resourceRegistry.valid(entity))
    {
        message.reply->setStatus(MessageReply::Status::Failed);
        return;
    }

    message.reply->setStatus(MessageReply::Status::Pending);

    auto[ bufferHandle, allocInfo, allocHandle, flags ] =
        resourceRegistry.try_get<VkBuffer, VmaAllocationInfo, VmaAllocation, ResourceFlags>(entity);
    if ((bufferHandle == VK_NULL_HANDLE) ||
        (allocInfo == nullptr) ||
        (allocHandle == VK_NULL_HANDLE) ||
        (flags == nullptr) ||
        !allocationIsHostVisible(entity))
    {
        message.reply->setStatus(MessageReply::Status::Failed);
        return;
    }

    // sanity check - is the VkBuffer in the resource the same as the one in the registry?
    if (message.resource.VkHandle != reinterpret_cast<uint64_t>(*bufferHandle))
    {
        message.reply->setStatus(MessageReply::Status::Failed);
        return;
    }

    // readback memory may be cached and not coherent, so the device's writes have to be made visible to the host first
    if (flags->domain == ResourceDomain::GPUToCPU)
    {
        vmaInvalidateAllocation(allocatorHandle, *allocHandle, 0u, VK_WHOLE_SIZE);
    }

    // now check for some easy stuff - is this persistently mapped or created mapped? if so, yay we can just use that pointer!
    const bool createdMapped = HasFlag(flags->flags, ResourceCreationFlags::PersistentlyMapped) ||
                               HasFlag(flags->flags, ResourceCreationFlags::CreateMapped);
    if (createdMapped && (allocInfo->pMappedData != nullptr))
    {
        message.reply->setPointer(static_cast<std::byte*>(allocInfo->pMappedData) + message.offset);
        return;
    }

    // otherwise, we need to get a pointer to the buffer
    void* mappedPointer = nullptr;
    VkResult result = vmaMapMemory(allocatorHandle, *allocHandle, &mappedPointer);
    if (result != VK_SUCCESS)
    {
        message.reply->setStatus(MessageReply::Status::Failed);
        return;
    }

    resourceRegistry.get_or_emplace<UserMapCount>(entity).Count += 1;
    // fixed: the offset parameter was ignored
    message.reply->setPointer(static_cast<std::byte*>(mappedPointer) + message.offset);
}

void ResourceContextImpl::processUnmapResourceMessage(UnmapResourceMessage&& message)
{
    const entt::entity entity = entt::entity(message.resource.EntityHandle);
    if (!resourceRegistry.valid(entity))
    {
        message.reply->setStatus(MessageReply::Status::Failed);
        return;
    }

    auto[ bufferHandle, allocationHandle, flags ] =
        resourceRegistry.try_get<VkBuffer, VmaAllocation, ResourceFlags>(entity);
    if ((bufferHandle == VK_NULL_HANDLE) ||
        (allocationHandle == VK_NULL_HANDLE) ||
        (flags == nullptr))
    {
        message.reply->setStatus(MessageReply::Status::Failed);
        return;
    }

    if (message.resource.VkHandle != reinterpret_cast<uint64_t>(*bufferHandle))
    {
        message.reply->setStatus(MessageReply::Status::Failed);
        return;
    }

    // host writes become visible to the device at the next submit, once they are flushed
    vmaFlushAllocation(allocatorHandle, *allocationHandle, message.offset, message.size);

    // don't want to unmap something that's persistently mapped!
    const bool createdMapped = HasFlag(flags->flags, ResourceCreationFlags::PersistentlyMapped) ||
                               HasFlag(flags->flags, ResourceCreationFlags::CreateMapped);
    UserMapCount* mapCount = resourceRegistry.try_get<UserMapCount>(entity);
    if (!createdMapped && mapCount != nullptr && mapCount->Count > 0)
    {
        vmaUnmapMemory(allocatorHandle, *allocationHandle);
        mapCount->Count -= 1;
    }

    // fixed: the unmap reply was never completed
    message.reply->setStatus(MessageReply::Status::Completed);
}

void ResourceContextImpl::processCopyResourceMessage(CopyResourceMessage&& message)
{
    // fixed: this checked the type and did nothing else, so its reply never completed
    message.reply->setStatus(MessageReply::Status::Pending);

    const entt::entity srcEntity = entt::entity(message.sourceResource.EntityHandle);
    if (!resourceRegistry.valid(srcEntity))
    {
        message.reply->setStatus(MessageReply::Status::Failed);
        return;
    }

    // the source's name string belongs to the source, so the copy goes unnamed
    std::optional<GraphicsResource> created = std::nullopt;
    if (const BufferCreateInfo* bufferInfo = resourceRegistry.try_get<BufferCreateInfo>(srcEntity))
    {
        BufferCreateInfo copyInfo = *bufferInfo;
        copyInfo.Flags = copyInfo.Flags & ~ResourceCreationFlags::UserDataAsNameString;
        copyInfo.UserData = nullptr;
        if (message.copyContents)
        {
            copyInfo.Usage |= BufferUsageBits::CopyDestination;
        }
        created = createBufferEntity(copyInfo);
    }
    else if (const ImageCreateInfo* imageInfo = resourceRegistry.try_get<ImageCreateInfo>(srcEntity))
    {
        ImageCreateInfo copyInfo = *imageInfo;
        copyInfo.Flags = copyInfo.Flags & ~ResourceCreationFlags::UserDataAsNameString;
        copyInfo.UserData = nullptr;
        if (message.copyContents)
        {
            copyInfo.Usage |= ImageUsageBits::TransferDestination;
        }
        created = createImageEntity(copyInfo);
    }

    if (!created.has_value())
    {
        message.reply->setStatus(MessageReply::Status::Failed);
        return;
    }

    if (!message.copyContents)
    {
        completeWithResource(*message.reply, *created);
        return;
    }

    message.reply->setGraphicsResourceRelaxed(*created);
    CopyResourceContentsMessage copyMessage{ message.sourceResource, *created, std::move(message.reply) };
    processCopyResourceContentsMessage(std::move(copyMessage));
}

void ResourceContextImpl::processCopyResourceContentsMessage(CopyResourceContentsMessage&& message)
{
    // Verify both entities are valid
    const entt::entity srcEntity = entt::entity(message.sourceResource.EntityHandle);
    const entt::entity dstEntity = entt::entity(message.destinationResource.EntityHandle);
    if (!resourceRegistry.valid(srcEntity) || !resourceRegistry.valid(dstEntity))
    {
        message.reply->setStatus(MessageReply::Status::Failed);
        return;
    }

    const bool srcIsBuffer = resourceRegistry.all_of<VkBuffer, BufferCreateInfo>(srcEntity);
    const bool dstIsBuffer = resourceRegistry.all_of<VkBuffer, BufferCreateInfo>(dstEntity);
    const bool srcIsImage = resourceRegistry.all_of<VkImage, ImageCreateInfo>(srcEntity);
    const bool dstIsImage = resourceRegistry.all_of<VkImage, ImageCreateInfo>(dstEntity);
    if (!CanBeTransferSource(resourceRegistry, srcEntity) || !CanBeTransferDestination(resourceRegistry, dstEntity))
    {
        message.reply->setStatus(MessageReply::Status::Failed);
        return;
    }

    // Switch message type based on resource types involved
    if (srcIsBuffer && dstIsBuffer)
    {
        message.reply->setStatus(MessageReply::Status::Transferring);
        transferSystem.EnqueueTransfer(TransferSystemCopyBufferToBufferMessage{ bufferTransferInfo(srcEntity), bufferTransferInfo(dstEntity), std::move(message.reply) });
        return;
    }
    else if (srcIsImage && dstIsImage)
    {
        message.reply->setStatus(MessageReply::Status::Transferring);
        transferSystem.EnqueueTransfer(TransferSystemCopyImageToImageMessage{ imageTransferInfo(srcEntity), imageTransferInfo(dstEntity), std::move(message.reply) });
        return;
    }
    else if (srcIsBuffer && dstIsImage)
    {
        message.reply->setStatus(MessageReply::Status::Transferring);
        transferSystem.EnqueueTransfer(TransferSystemCopyBufferToImageMessage{ bufferTransferInfo(srcEntity), imageTransferInfo(dstEntity), std::move(message.reply) });
        return;
    }
    else if (srcIsImage && dstIsBuffer)
    {
        message.reply->setStatus(MessageReply::Status::Transferring);
        transferSystem.EnqueueTransfer(TransferSystemCopyImageToBufferMessage{ imageTransferInfo(srcEntity), bufferTransferInfo(dstEntity), std::move(message.reply) });
        return;
    }

    // invalid types or handles
    message.reply->setStatus(MessageReply::Status::Failed);
}

void ResourceContextImpl::processDestroyResourceMessage(DestroyResourceMessage&& message)
{
    const entt::entity entity = entt::entity(message.resource.EntityHandle);
    if (!resourceRegistry.valid(entity))
    {
        message.reply->setStatus(MessageReply::Status::Failed);
        return;
    }

    // sanity check - a GraphicsResource that names another object than the registry does is stale or corrupt
    const VkBuffer* buffer = resourceRegistry.try_get<VkBuffer>(entity);
    const VkImage* image = resourceRegistry.try_get<VkImage>(entity);
    const VkSampler* sampler = resourceRegistry.try_get<VkSampler>(entity);
    const uint64_t registryHandle =
        (buffer != nullptr) ? reinterpret_cast<uint64_t>(*buffer) :
        (image != nullptr) ? reinterpret_cast<uint64_t>(*image) : 0u;
    const bool handleMatches = (registryHandle != 0u) ?
        (registryHandle == message.resource.VkHandle) :
        (sampler != nullptr && reinterpret_cast<uint64_t>(*sampler) == message.resource.VkSamplerHandle);
    if (!handleMatches)
    {
        message.reply->setStatus(MessageReply::Status::Failed);
        return;
    }

    destroyEntityResources(entity);
    message.reply->setStatus(MessageReply::Status::Completed);
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
        buffer_info.UserData,
        0.0f
    };

    VkBuffer bufferHandle = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;
    VmaAllocationInfo allocInfo{};
    VkResult result = vmaCreateBuffer(allocatorHandle, &vkBufferInfo, &allocCreateInfo, &bufferHandle, &alloc, &allocInfo);
    if (result != VK_SUCCESS)
    {
        return VK_NULL_HANDLE;
    }

    resourceRegistry.emplace<VkBuffer>(new_entity, bufferHandle);
    resourceRegistry.emplace<VmaAllocation>(new_entity, alloc);
    resourceRegistry.emplace<VmaAllocationInfo>(new_entity, allocInfo);

    const char* name = NameFromUserData(buffer_info.Flags, buffer_info.UserData);
    if (name != nullptr)
    {
        // working with a stored copy, since the caller's string may not outlive the message
        const std::string& debugName = resourceRegistry.emplace<std::string>(new_entity, name);
        setObjectName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(bufferHandle), debugName);
        vmaSetAllocationName(allocatorHandle, alloc, std::format("{}_allocation", debugName).c_str());
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

    if (const std::string* parentName = resourceRegistry.try_get<std::string>(new_entity))
    {
        setObjectName(VK_OBJECT_TYPE_BUFFER_VIEW, reinterpret_cast<uint64_t>(bufferView), *parentName + "_buffer_view");
    }

    return bufferView;
}

void ResourceContextImpl::setBufferDataHostOnly(entt::entity entity, InternalResourceDataContainer& data_container)
{
    const VmaAllocationInfo& allocInfo = resourceRegistry.get<VmaAllocationInfo>(entity);
    const VmaAllocation& alloc = resourceRegistry.get<VmaAllocation>(entity);

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
        std::memcpy(static_cast<std::byte*>(mappedAddress) + offset, data.data.get(), data.size);
        offset += data.size;
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
        VK_SAMPLE_COUNT_1_BIT,
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
        image_info.UserData,
        0.0f
    };

    VkImage imageHandle = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;
    VmaAllocationInfo allocInfo{};
    VkResult result = vmaCreateImage(allocatorHandle, &vkImageInfo, &allocCreateInfo, &imageHandle, &alloc, &allocInfo);
    if (result != VK_SUCCESS)
    {
        return VK_NULL_HANDLE;
    }

    // fixed: this and the caller both emplaced the image, which asserts in EnTT
    resourceRegistry.emplace<VkImage>(new_entity, imageHandle);
    resourceRegistry.emplace<VmaAllocation>(new_entity, alloc);
    resourceRegistry.emplace<VmaAllocationInfo>(new_entity, allocInfo);

    const char* name = NameFromUserData(image_info.Flags, image_info.UserData);
    if (name != nullptr)
    {
        const std::string& debugName = resourceRegistry.emplace<std::string>(new_entity, name);
        setObjectName(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(imageHandle), debugName);
        vmaSetAllocationName(allocatorHandle, alloc, std::format("{}_allocation", debugName).c_str());
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

    if (const std::string* parentName = resourceRegistry.try_get<std::string>(new_entity))
    {
        setObjectName(VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<uint64_t>(imageView), *parentName + "_image_view");
    }

    return imageView;
}

VkSampler ResourceContextImpl::createSampler(
    entt::entity new_entity,
    const SamplerCreateInfo& sampler_info,
    ResourceCreationFlags flags,
    void* user_data_ptr)
{
    const VkSamplerCreateInfo& samplerCreateInfo = resourceRegistry.emplace<VkSamplerCreateInfo>(new_entity, ToVkSamplerCreateInfo(sampler_info));
    VkSampler sampler = VK_NULL_HANDLE;
    VkResult result = vkCreateSampler(device->Handle().As<VkDevice>(), &samplerCreateInfo, nullptr, &sampler);
    if (result != VK_SUCCESS)
    {
        return VK_NULL_HANDLE;
    }

    resourceRegistry.emplace<VkSampler>(new_entity, sampler);

    // can use entity system to find out if this is a combined image sampler or just a sampler, try and find parent name
    if (const std::string* parentName = resourceRegistry.try_get<std::string>(new_entity))
    {
        setObjectName(VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<uint64_t>(sampler), *parentName + "_sampler");
    }
    else if (const char* name = NameFromUserData(flags, user_data_ptr))
    {
        setObjectName(VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<uint64_t>(sampler), name);
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

void ResourceContextImpl::enqueueBufferUpload(entt::entity entity, InternalResourceDataContainer&& data, std::shared_ptr<ResourceTransferReply>&& reply)
{
    TransferSystemSetBufferDataMessage setBufferDataMessage
    {
        bufferTransferInfo(entity),
        std::move(data),
        std::move(reply)
    };
    transferSystem.EnqueueTransfer(std::move(setBufferDataMessage));
}

void ResourceContextImpl::enqueueImageUpload(entt::entity entity, InternalResourceDataContainer&& data, std::shared_ptr<ResourceTransferReply>&& reply)
{
    TransferSystemSetImageDataMessage setImageDataMessage
    {
        imageTransferInfo(entity),
        std::move(data),
        std::move(reply)
    };
    transferSystem.EnqueueTransfer(std::move(setImageDataMessage));
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

void ResourceContextImpl::setObjectName(VkObjectType object_type, uint64_t handle, const std::string& name) const
{
    if (!validationEnabled)
    {
        return;
    }
    VkResult result = rhi::RhiSystem::SetObjectName(object_type, handle, name.c_str());
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

void ResourceContextImpl::writeStatsJsonFile(const char* output_file)
{
    char* output = nullptr;
    vmaBuildStatsString(allocatorHandle, &output, VK_TRUE);
    std::ofstream outputFile(output_file);
    if (!outputFile.is_open())
    {
        throw std::runtime_error("Failed to open output file for JSON dump!");
    }

    outputFile.write(output, strlen(output));
    vmaFreeStatsString(allocatorHandle, output);
}

namespace
{

    bool HasFlag(ResourceCreationFlags flags, ResourceCreationFlags flag) noexcept
    {
        return (flags & flag) != ResourceCreationFlags::None;
    }

    const char* NameFromUserData(ResourceCreationFlags flags, const void* user_data) noexcept
    {
        if (!HasFlag(flags, ResourceCreationFlags::UserDataAsNameString) || user_data == nullptr)
        {
            return nullptr;
        }
        return static_cast<const char*>(user_data);
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
