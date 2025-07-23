#include "ResourceContextImpl.hpp"

#include "ResourceContext.hpp"
#include "../../rendering_context/include/RenderingContext.hpp"
#include "Instance.hpp"

#include <fstream>
#include <format>

#include <thsvs_simpler_vulkan_synchronization.h>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

namespace
{
    VkMemoryPropertyFlags GetMemoryPropertyFlags(resource_usage _resource_usage) noexcept;
    VkFormatFeatureFlags GetFormatFeatureFlagsFromUsage(const VkImageUsageFlags flags) noexcept;
    VmaAllocationCreateFlags GetAllocationCreateFlags(const resource_creation_flags flags) noexcept;
    VmaMemoryUsage GetVmaMemoryUsage(const resource_usage _resource_usage) noexcept;
}

void ResourceContextImpl::construct(const ResourceContextCreateInfo& createInfo)
{
    device = createInfo.logicalDevice;
    validationEnabled = createInfo.validationEnabled;
    if (validationEnabled)
    {
        vkDebugFns = device->DebugUtilsHandler();
    }

    const auto& applicationInfo = device->ParentInstance()->ApplicationInfo();
    VmaAllocatorCreateFlags create_flags = VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT;
    if (applicationInfo.apiVersion < VK_API_VERSION_1_1)
    {
        create_flags |= VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT;
    }

    VmaAllocatorCreateInfo create_info
    {
        create_flags,
        createInfo.physicalDevice->vkHandle(),
        device->vkHandle(),
        0u,
        nullptr, //pAllocationCallbacks
        nullptr, //pDeviceMemoryCallbacks
        nullptr, //pHeapSizeLimit
        nullptr, //pVulkanFunctions
        device->ParentInstance()->vkHandle(),
        applicationInfo.apiVersion,
        nullptr
    };

    VkResult result = vmaCreateAllocator(&create_info, &allocatorHandle);
    VkAssert(result);

    transferSystem.Initialize(device);

    startWorker();

}

void ResourceContextImpl::destroy()
{
    // have to exit worker and then clear all the pending messages
    setExitWorker();
    // ... which we can only do by processing them :'(
    while (!messageQueue.empty())
    {
        
    }

    // destroy transfer system, which may have pending resources and transfers
    transferSystem.destroy();

    // last step, destroy the allocator and the registry. allocator last
    resourceRegistry.clear();
    vmaDestroyAllocator(allocatorHandle);
}

void ResourceContextImpl::update()
{
    
}

void ResourceContextImpl::pushMessage(ResourceMessagePayloadType message)
{
    messageQueue.push(std::move(message));
}

void ResourceContextImpl::setExitWorker()
{
    shouldExitWorker.store(true);
    workerThread.join();
}

void ResourceContextImpl::startWorker()
{
    shouldExitWorker.store(false);
    workerThread = std::thread(&ResourceContextImpl::processMessages, this);
}

void ResourceContextImpl::processMessages()
{
    // Create a backoff sleeper with default parameters
    foundation::ExponentialBackoffSleeper sleeper;
    // surely there has to be a better way than this. why is std::visit like this
    auto MessageVisitor =
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
            else if constexpr (std::is_same_v<T, CreateCombinedImageSamplerMessage>)
            {
                processCreateCombinedImageSamplerMessage(std::move(arg));
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

    while (!shouldExitWorker.load())
    {
        bool didProcessMessage = false;
        
        // Process all available messages
        while (!messageQueue.empty())
        {
            ResourceMessagePayloadType message = messageQueue.pop();
            std::visit(MessageVisitor, message);
            didProcessMessage = true;
        }
        
        // Adjust sleep behavior based on whether we processed any messages
        if (didProcessMessage)
        {
            // Reset sleep duration to minimum if we processed messages
            sleeper.reset();
        }
        else
        {
            // Back off if no messages were processed
            sleeper.backoff();
        }
        
        // Sleep for the calculated duration
        sleeper.sleep();
    }
}

void ResourceContextImpl::processCreateBufferMessage(CreateBufferMessage&& message)
{
    const entt::entity new_entity = resourceRegistry.create();

    message.reply->SetStatus(MessageReply::Status::Pending);
    
    VkBuffer buffer_handle = createBuffer(
        new_entity,
        std::move(message.bufferInfo),
        message.flags,
        message.resourceUsage,
        message.userData,
        message.initialData.has_value());

    if (buffer_handle == VK_NULL_HANDLE)
    {
        message.reply->SetStatus(MessageReply::Status::Failed);
        return;
    }

    resourceRegistry.emplace<VkBuffer>(new_entity, buffer_handle);

    VkBufferView buffer_view = VK_NULL_HANDLE;
    if (message.viewInfo)
    {
        buffer_view = createBufferView(new_entity, std::move(message.viewInfo.value()), message.flags, message.userData);
        resourceRegistry.emplace<VkBufferView>(new_entity, buffer_view);
    }

    if (message.initialData)
    {
        // pass responsiblity on to transfer system now, transferring ownership of data with a move
        message.reply->SetStatus(MessageReply::Status::Transferring);
        GraphicsResource createdResource
        {
            resource_type::Buffer,
            static_cast<uint32_t>(new_entity),
            reinterpret_cast<uint64_t>(buffer_handle),
            reinterpret_cast<uint64_t>(buffer_view),
            0u
        };
        message.reply->SetGraphicsResourceRelaxed(createdResource);

        TransferSystemSetBufferDataMessage set_buffer_data_message
        {
            TransferSystemReqBufferInfo
            {
                buffer_handle,
                message.bufferInfo,
                message.resourceUsage,
                message.flags
            },
            std::move(message.initialData.value()),
            std::move(message.reply)
        };

        transferSystem.EnqueueTransfer(std::move(set_buffer_data_message));
    }
    else
    {
        message.reply->SetGraphicsResource(
            resource_type::Buffer,
            static_cast<uint32_t>(new_entity),
            reinterpret_cast<uint64_t>(buffer_handle),
            reinterpret_cast<uint64_t>(buffer_view),
            0u);
    }

}

void ResourceContextImpl::processCreateImageMessage(CreateImageMessage&& message)
{
    const entt::entity new_entity = resourceRegistry.create();
    VkImage image_handle = createImage(
        new_entity,
        std::move(message.imageInfo),
        message.flags,
        message.resourceUsage,
        message.userData,
        message.initialData.has_value());

    if (image_handle != VK_NULL_HANDLE)
    {
        resourceRegistry.emplace<VkImage>(new_entity, image_handle);
    }
    else
    {
        message.reply->SetStatus(MessageReply::Status::Failed);
        resourceRegistry.destroy(new_entity);
        return;
    }

    VkImageView image_view = VK_NULL_HANDLE;
    if (message.viewInfo.has_value())
    {
        image_view = createImageView(new_entity, message.viewInfo.value(), message.flags);
        if (image_view != VK_NULL_HANDLE)
        {
            resourceRegistry.emplace<VkImageView>(new_entity, image_view);
        }
        else
        {
            message.reply->SetStatus(MessageReply::Status::Failed);
            resourceRegistry.destroy(new_entity);
            return;
        }
    }

    if (message.initialData.has_value())
    {
        message.reply->SetStatus(MessageReply::Status::Transferring);
        GraphicsResource createdResource
        {
            resource_type::Image,
            static_cast<uint32_t>(new_entity),
            reinterpret_cast<uint64_t>(image_handle),
            reinterpret_cast<uint64_t>(image_view),
            0u
        };
        message.reply->SetGraphicsResourceRelaxed(createdResource);
        
        TransferSystemSetImageDataMessage set_image_data_message
        {
            TransferSystemReqImageInfo
            {
                image_handle,
                resourceRegistry.get<VkImageCreateInfo>(new_entity),
                message.resourceUsage,
                message.flags
            },
            std::move(message.initialData.value()),
            std::move(message.reply)
        };
        
        transferSystem.EnqueueTransfer(std::move(set_image_data_message));
    }
    else
    {
        message.reply->SetGraphicsResource(
            resource_type::Image,
            static_cast<uint32_t>(new_entity),
            reinterpret_cast<uint64_t>(image_handle),
            reinterpret_cast<uint64_t>(image_view),
            0u);
    }
}

void ResourceContextImpl::processCreateCombinedImageSamplerMessage(CreateCombinedImageSamplerMessage&& message)
{
    const entt::entity new_entity = resourceRegistry.create();
    VkImage image_handle = createImage(
        new_entity,
        std::move(message.imageInfo),
        message.flags,
        message.resourceUsage,
        message.userData,
        message.initialData.has_value());

    if (image_handle != VK_NULL_HANDLE)
    {
        resourceRegistry.emplace<VkImage>(new_entity, image_handle);
    }
    else
    {
        message.reply->SetStatus(MessageReply::Status::Failed);
        resourceRegistry.destroy(new_entity);
        return;
    }

    VkImageView image_view = createImageView(new_entity, message.viewInfo, message.flags);
    if (image_view != VK_NULL_HANDLE)
    {
        resourceRegistry.emplace<VkImageView>(new_entity, image_view);
    }
    else
    {
        message.reply->SetStatus(MessageReply::Status::Failed);
        resourceRegistry.destroy(new_entity);
        return;
    }

    VkSampler sampler = createSampler(new_entity, message.samplerInfo, message.flags, message.userData);
    if (sampler != VK_NULL_HANDLE)
    {
        resourceRegistry.emplace<VkSampler>(new_entity, sampler);
    }
    else
    {
        message.reply->SetStatus(MessageReply::Status::Failed);
        resourceRegistry.destroy(new_entity);
        return;
    }

    if (message.initialData.has_value())
    {
        message.reply->SetStatus(MessageReply::Status::Transferring);
        GraphicsResource createdResource
        {
            resource_type::CombinedImageSampler,
            static_cast<uint32_t>(new_entity),
            reinterpret_cast<uint64_t>(image_handle),
            reinterpret_cast<uint64_t>(image_view),
            reinterpret_cast<uint64_t>(sampler)
        };
        message.reply->SetGraphicsResourceRelaxed(createdResource);

        TransferSystemSetImageDataMessage set_image_data_message
        {
            TransferSystemReqImageInfo
            {
                image_handle,
                resourceRegistry.get<VkImageCreateInfo>(new_entity),
                message.resourceUsage,
                message.flags
            },
            std::move(message.initialData.value()),
            std::move(message.reply)
        };
        
        transferSystem.EnqueueTransfer(std::move(set_image_data_message));
    }
    else
    {
        message.reply->SetGraphicsResource(
            resource_type::CombinedImageSampler,
            static_cast<uint32_t>(new_entity),
            reinterpret_cast<uint64_t>(image_handle),
            reinterpret_cast<uint64_t>(image_view),
            reinterpret_cast<uint64_t>(sampler));
    }
}

void ResourceContextImpl::processCreateSamplerMessage(CreateSamplerMessage&& message)
{
    const entt::entity new_entity = resourceRegistry.create();
    VkSampler sampler = createSampler(new_entity, message.samplerInfo, message.flags, message.userData);
    if (sampler != VK_NULL_HANDLE)
    {
        resourceRegistry.emplace<VkSampler>(new_entity, sampler);
    }
    else
    {
        message.reply->SetStatus(MessageReply::Status::Failed);
        resourceRegistry.destroy(new_entity);
        return;
    }

    message.reply->SetGraphicsResource(
        resource_type::Sampler,
        static_cast<uint32_t>(new_entity),
        0u, 0u, reinterpret_cast<uint64_t>(sampler));

}

void ResourceContextImpl::processSetBufferDataMessage(SetBufferDataMessage&& message)
{
    const GraphicsResource& buffer = message.destBuffer;
    const entt::entity entity = entt::entity(buffer.EntityHandle);
    if (!resourceRegistry.valid(entity))
    {
        message.reply->SetStatus(MessageReply::Status::Failed);
        return;
    }

    auto[buffer_handle, buffer_info, buffer_flags] = resourceRegistry.try_get<VkBuffer, VkBufferCreateInfo, ResourceFlags>(entity);
    if (!buffer_handle || !buffer_info || !buffer_flags)
    {
        message.reply->SetStatus(MessageReply::Status::Failed);
        return;
    }

    message.reply->SetStatus(MessageReply::Status::Transferring);

    TransferSystemSetBufferDataMessage set_buffer_data_message
    {
        TransferSystemReqBufferInfo
        {
            *buffer_handle,
            *buffer_info,
            buffer_flags->resourceUsage,
            buffer_flags->flags
        },
        std::move(message.data),
        std::move(message.reply)
    };

    transferSystem.EnqueueTransfer(std::move(set_buffer_data_message));
}

void ResourceContextImpl::processSetImageDataMessage(SetImageDataMessage&& message)
{
    const GraphicsResource& image = message.destImage;
    const entt::entity entity = entt::entity(image.EntityHandle);
    if (!resourceRegistry.valid(entity))
    {
        message.reply->SetStatus(MessageReply::Status::Failed);
        return;
    }

    auto [image_handle, image_info, image_flags] = resourceRegistry.try_get<VkImage, VkImageCreateInfo, ResourceFlags>(entity);
    if (!image_handle || !image_info || !image_flags)
    {
        message.reply->SetStatus(MessageReply::Status::Failed);
        return;
    }

    message.reply->SetStatus(MessageReply::Status::Transferring);

    TransferSystemSetImageDataMessage set_image_data_message
    {
        TransferSystemReqImageInfo
        {
            *image_handle,
            *image_info,
            image_flags->resourceUsage,
            image_flags->flags
        },
        std::move(message.data),
        std::move(message.reply)
    };

    transferSystem.EnqueueTransfer(std::move(set_image_data_message));
}

void ResourceContextImpl::processFillResourceMessage(FillResourceMessage&& message)
{
    const GraphicsResource& buffer = message.resource;
    const entt::entity entity = entt::entity(buffer.EntityHandle);
    if (!resourceRegistry.valid(entity))
    {
        message.reply->SetStatus(MessageReply::Status::Failed);
        return;
    }

    auto[ buffer_handle, buffer_info, buffer_flags ] = resourceRegistry.try_get<VkBuffer, VkBufferCreateInfo, ResourceFlags>(entity);
    if (!buffer_handle || !buffer_info || !buffer_flags)
    {
        message.reply->SetStatus(MessageReply::Status::Failed);
        return;
    }

    message.reply->SetStatus(MessageReply::Status::Transferring);

    TransferSystemFillBufferMessage fill_buffer_message
    {
        TransferSystemReqBufferInfo
        {
            *buffer_handle,
            *buffer_info,
            buffer_flags->resourceUsage,
            buffer_flags->flags
        },
        message.value,
        message.offset,
        message.size,
        std::move(message.reply)
    };

    transferSystem.EnqueueTransfer(std::move(fill_buffer_message));

}

void ResourceContextImpl::processMapResourceMessage(MapResourceMessage&& message)
{
    const entt::entity entity = entt::entity(message.resource.EntityHandle);
    if (!resourceRegistry.valid(entity))
    {
        message.reply->SetStatus(MessageReply::Status::Failed);
        return;
    }

    auto[ buffer_handle, allocation_info, allocation_handle, buffer_flags ] =
        resourceRegistry.try_get<VkBuffer, VmaAllocationInfo, VmaAllocation, ResourceFlags>(entity);
    if (!buffer_handle || !allocation_info || !allocation_handle || !buffer_flags)
    {
        message.reply->SetStatus(MessageReply::Status::Failed);
        return;
    }

    // sanity check - is the VkBuffer in the resource the same as the one in the registry?
    const GraphicsResource& buffer = message.resource;
    if (buffer.VkHandle != reinterpret_cast<uint64_t>(*buffer_handle))
    {
        message.reply->SetStatus(MessageReply::Status::Failed);
        return;
    }
    
    // now check for some easy stuff - is this persistently mapped or created mapped? if so, yay we can just use that pointer!
    // (so lang as it's set, if not then oops things are broken, go home etc)
    if (((buffer_flags->flags & resource_creation_flag_bits::PersistentlyMapped) ||
         (buffer_flags->flags & resource_creation_flag_bits::CreateMapped)) &&
         allocation_info->pMappedData)
    {
        message.reply->SetPointer(allocation_info->pMappedData);
        return;
    }
    else if ((buffer_flags->flags & resource_creation_flag_bits::PersistentlyMapped) && !allocation_info->pMappedData)
    {
        message.reply->SetStatus(MessageReply::Status::Failed);
        return;
    }

    // otherwise, we need to get a pointer to the buffer
    void* mapped_pointer = nullptr;
    VkResult result = vmaMapMemory(allocatorHandle, *allocation_handle, &mapped_pointer);
    VkAssert(result);
    
    message.reply->SetPointer(mapped_pointer);
}

void ResourceContextImpl::processUnmapResourceMessage(UnmapResourceMessage&& message)
{
    const entt::entity entity = entt::entity(message.resource.EntityHandle);
    if (!resourceRegistry.valid(entity))
    {
        message.reply->SetStatus(MessageReply::Status::Failed);
        return;
    }

    auto[ buffer_handle, allocation_info, allocation_handle, buffer_flags ] =
        resourceRegistry.try_get<VkBuffer, VmaAllocationInfo, VmaAllocation, ResourceFlags>(entity);
    if (!buffer_handle || !allocation_info || !allocation_handle || !buffer_flags)
    {
        message.reply->SetStatus(MessageReply::Status::Failed);
        return;
    }

    if (message.resource.VkHandle != reinterpret_cast<uint64_t>(*buffer_handle))
    {
        message.reply->SetStatus(MessageReply::Status::Failed);
        return;
    }

    // don't want to unmap something that's persistently mapped!
    if (buffer_flags->flags & resource_creation_flag_bits::PersistentlyMapped)
    {
        message.reply->SetStatus(MessageReply::Status::Completed);
        return;
    }

    vmaUnmapMemory(allocatorHandle, *allocation_handle);

}

void ResourceContextImpl::processCopyResourceMessage(CopyResourceMessage&& message)
{
    const entt::entity src_entity = entt::entity(message.sourceResource.EntityHandle);
    if (!resourceRegistry.valid(src_entity))
    {
        message.reply->SetStatus(MessageReply::Status::Failed);
        return;
    }

    switch (message.sourceResource.Type)
    {
    case resource_type::Buffer:
        break;
    case resource_type::BufferView:
        break;
    case resource_type::Image:
        break;
    case resource_type::ImageView:
        break;
    case resource_type::Sampler:
        break;
    case resource_type::CombinedImageSampler:
        break;
    default:
        message.reply->SetStatus(MessageReply::Status::Failed);
        return;
    }
}

void ResourceContextImpl::processCopyResourceContentsMessage(CopyResourceContentsMessage&& message)
{
    // Verify both entities are valid
    const entt::entity src_entity = entt::entity(message.sourceResource.EntityHandle);
    const entt::entity dst_entity = entt::entity(message.destinationResource.EntityHandle);
    if (!resourceRegistry.valid(src_entity) || !resourceRegistry.valid(dst_entity))
    {
        message.reply->SetStatus(MessageReply::Status::Failed);
        return;
    }

    // Switch message type based on resource types involved
    if (message.sourceResource.Type == resource_type::Buffer && message.destinationResource.Type == resource_type::Buffer)
    {
        auto[src_buffer_handle, src_buffer_info, src_buffer_flags] =
            resourceRegistry.try_get<VkBuffer, VkBufferCreateInfo, ResourceFlags>(src_entity);

        if (!src_buffer_handle || !src_buffer_info || !src_buffer_flags)
        {
            message.reply->SetStatus(MessageReply::Status::Failed);
            return;
        }

        auto[dst_buffer_handle, dst_buffer_info, dst_buffer_flags] =
            resourceRegistry.try_get<VkBuffer, VkBufferCreateInfo, ResourceFlags>(dst_entity);

        if (!dst_buffer_handle || !dst_buffer_info || !dst_buffer_flags)
        {
            message.reply->SetStatus(MessageReply::Status::Failed);
            return;
        }

        TransferSystemCopyBufferToBufferMessage copy_buffer_to_buffer_message
        {
            TransferSystemReqBufferInfo
            {
                *src_buffer_handle,
                *src_buffer_info,
                src_buffer_flags->resourceUsage,
                src_buffer_flags->flags
            },
            TransferSystemReqBufferInfo
            {
                *dst_buffer_handle,
                *dst_buffer_info,
                dst_buffer_flags->resourceUsage,
                dst_buffer_flags->flags
            },
            std::move(message.reply)
        };

        copy_buffer_to_buffer_message.reply->SetStatus(MessageReply::Status::Transferring);
        transferSystem.EnqueueTransfer(std::move(copy_buffer_to_buffer_message));
        return;
    }
    else if (message.sourceResource.Type == resource_type::Image && message.destinationResource.Type == resource_type::Image)
    {
        auto[src_image_handle, src_image_info, src_image_flags] =
            resourceRegistry.try_get<VkImage, VkImageCreateInfo, ResourceFlags>(src_entity);

        if (!src_image_handle || !src_image_info || !src_image_flags)
        {
            message.reply->SetStatus(MessageReply::Status::Failed);
            return;
        }

        auto[dst_image_handle, dst_image_info, dst_image_flags] =
            resourceRegistry.try_get<VkImage, VkImageCreateInfo, ResourceFlags>(dst_entity);
            
        if (!dst_image_handle || !dst_image_info || !dst_image_flags)
        {
            message.reply->SetStatus(MessageReply::Status::Failed);
            return;
        }
        
        TransferSystemCopyImageToImageMessage copy_image_to_image_message
        {
            TransferSystemReqImageInfo
            {
                *src_image_handle,
                *src_image_info,
                src_image_flags->resourceUsage,
                src_image_flags->flags
            },
            TransferSystemReqImageInfo
            {
                *dst_image_handle,  
                *dst_image_info,
                dst_image_flags->resourceUsage,
                dst_image_flags->flags
            },
            std::move(message.reply)
        };

        copy_image_to_image_message.reply->SetStatus(MessageReply::Status::Transferring);
        transferSystem.EnqueueTransfer(std::move(copy_image_to_image_message));
        return;
    }
    else if (message.sourceResource.Type == resource_type::Buffer && message.destinationResource.Type == resource_type::Image)
    {
        auto[src_buffer_handle, src_buffer_info, src_buffer_flags] =
            resourceRegistry.try_get<VkBuffer, VkBufferCreateInfo, ResourceFlags>(src_entity);

        if (!src_buffer_handle || !src_buffer_info || !src_buffer_flags)
        {
            message.reply->SetStatus(MessageReply::Status::Failed);
            return;
        }

        auto[dst_image_handle, dst_image_info, dst_image_flags] =
            resourceRegistry.try_get<VkImage, VkImageCreateInfo, ResourceFlags>(dst_entity);

        if (!dst_image_handle || !dst_image_info || !dst_image_flags)
        {
            message.reply->SetStatus(MessageReply::Status::Failed);
            return;
        }
        
        TransferSystemCopyBufferToImageMessage copy_buffer_to_image_message
        {
            TransferSystemReqBufferInfo
            {
                *src_buffer_handle,
                *src_buffer_info,
                src_buffer_flags->resourceUsage,
                src_buffer_flags->flags
            },
            TransferSystemReqImageInfo
            {
                *dst_image_handle,
                *dst_image_info,
                dst_image_flags->resourceUsage,
                dst_image_flags->flags
            },
            std::move(message.reply)
        };
        
        copy_buffer_to_image_message.reply->SetStatus(MessageReply::Status::Transferring);
        transferSystem.EnqueueTransfer(std::move(copy_buffer_to_image_message));
        return;
    }
    else if (message.sourceResource.Type == resource_type::Image && message.destinationResource.Type == resource_type::Buffer)
    {
        auto[src_image_handle, src_image_info, src_image_flags] =
            resourceRegistry.try_get<VkImage, VkImageCreateInfo, ResourceFlags>(src_entity);

        if (!src_image_handle || !src_image_info || !src_image_flags)
        {
            message.reply->SetStatus(MessageReply::Status::Failed);
            return;
        }

        auto[dst_buffer_handle, dst_buffer_info, dst_buffer_flags] =
            resourceRegistry.try_get<VkBuffer, VkBufferCreateInfo, ResourceFlags>(dst_entity);

        if (!dst_buffer_handle || !dst_buffer_info || !dst_buffer_flags)
        {
            message.reply->SetStatus(MessageReply::Status::Failed);
            return;
        }
        
        TransferSystemCopyImageToBufferMessage copy_image_to_buffer_message
        {
            TransferSystemReqImageInfo
            {
                *src_image_handle,
                *src_image_info,
                src_image_flags->resourceUsage,
                src_image_flags->flags
            },
            TransferSystemReqBufferInfo
            {
                *dst_buffer_handle,
                *dst_buffer_info,
                dst_buffer_flags->resourceUsage,
                dst_buffer_flags->flags
            },
            std::move(message.reply)
        };

        copy_image_to_buffer_message.reply->SetStatus(MessageReply::Status::Transferring);
        transferSystem.EnqueueTransfer(std::move(copy_image_to_buffer_message));
        return;
    }

    // invalid types or handles
    message.reply->SetStatus(MessageReply::Status::Failed);
}

void ResourceContextImpl::processDestroyResourceMessage(DestroyResourceMessage&& message)
{
    const entt::entity entity = entt::entity(message.resource.EntityHandle);
    if (!resourceRegistry.valid(entity))
    {
        message.reply->SetStatus(MessageReply::Status::Failed);
        return;
    }
    
    MessageReply::Status destroy_vk_handle_status = MessageReply::Status::Completed;
    switch (message.resource.Type)
    {
    case resource_type::Buffer:
        destroy_vk_handle_status = destroyBuffer(entity, message.resource);
        if ((VkBufferView)message.resource.VkViewHandle != VK_NULL_HANDLE)
        {
            destroy_vk_handle_status = destroyBufferView(entity, message.resource);
        }
        break;
    case resource_type::BufferView:
        destroy_vk_handle_status = destroyBufferView(entity, message.resource);
        break;
    case resource_type::Image:
        destroy_vk_handle_status = destroyImage(entity, message.resource);
        if ((VkImageView)message.resource.VkViewHandle != VK_NULL_HANDLE)
        {
            destroy_vk_handle_status = destroyImageView(entity, message.resource);
        }
        break;
    case resource_type::ImageView:
        destroy_vk_handle_status = destroyImageView(entity, message.resource);
        break;
    case resource_type::Sampler:
        destroy_vk_handle_status = destroySampler(entity, message.resource);
        break;
    case resource_type::CombinedImageSampler:
        destroy_vk_handle_status = destroyCombinedImageSampler(entity, message.resource);
        break;
    default:
        message.reply->SetStatus(MessageReply::Status::Failed);
        resourceRegistry.destroy(entity); 
        return;
    }

    message.reply->SetStatus(destroy_vk_handle_status);
    // even if things failed, we still want to destroy the entity
    resourceRegistry.destroy(entity);
}

VkBuffer ResourceContextImpl::createBuffer(
    entt::entity new_entity,
    VkBufferCreateInfo&& buffer_info,
    const resource_creation_flags _flags,
    const resource_usage _resource_usage,
    void* user_data_ptr,
    bool has_initial_data)
{
    VkBufferCreateInfo& buffer_create_info = resourceRegistry.emplace<VkBufferCreateInfo>(new_entity, std::move(buffer_info)); 
    const ResourceFlags& flags = resourceRegistry.emplace<ResourceFlags>(new_entity, resource_type::Buffer, _flags, _resource_usage);
    std::string debug_name;
    if (flags.flags & resource_creation_flag_bits::UserDataAsString)
    {
        // working with value instead of ref fine because we're just gonna read it for the debug name setting in a sec
        // don't use macro yet because that contains timestamp info!
        debug_name = resourceRegistry.emplace<std::string>(new_entity, std::string(reinterpret_cast<const char*>(user_data_ptr)));
    }

    VmaAllocationInfo& alloc_info = resourceRegistry.emplace<VmaAllocationInfo>(new_entity);
    VmaAllocation& alloc = resourceRegistry.emplace<VmaAllocation>(new_entity);

    if ((flags.resourceUsage == resource_usage::CPUToGPU || flags.resourceUsage == resource_usage::GPUToCPU || flags.resourceUsage == resource_usage::GPUOnly) && has_initial_data)
    {
        buffer_create_info.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }

    VmaAllocationCreateInfo alloc_create_info
    {
        GetAllocationCreateFlags(flags.flags),
        GetVmaMemoryUsage(flags.resourceUsage),
        0u,
        0u,
        UINT32_MAX,
        VK_NULL_HANDLE,
        user_data_ptr
    };

    VkBuffer buffer_handle = VK_NULL_HANDLE;
    VkResult result = vmaCreateBuffer(allocatorHandle, &buffer_create_info, &alloc_create_info, &buffer_handle, &alloc, &alloc_info);
    VkAssert(result);
    
    if constexpr (RENDERING_CONTEXT_USE_DEBUG_INFO && RENDERING_CONTEXT_VALIDATION_ENABLED)
    {
        if (flags.flags & resource_creation_flag_bits::UserDataAsString)
        {
            const char* debugName = RENDERING_CONTEXT_DEBUG_OBJECT_NAME(debug_name.c_str());
            std::string debugAllocName = std::format("{}_allocation", debugName);
            result = RenderingContext::SetObjectName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(buffer_handle), RENDERING_CONTEXT_DEBUG_OBJECT_NAME(debug_name.c_str()));
            VkAssert(result);
            vmaSetAllocationName(allocatorHandle, alloc, debugAllocName.c_str());
        }
    }

    return buffer_handle;
}

VkBufferView ResourceContextImpl::createBufferView(
    entt::entity new_entity,
    VkBufferViewCreateInfo&& view_info,
    const resource_creation_flags _flags,
    void* user_data_ptr)
{
    const VkBufferViewCreateInfo& local_view_info = resourceRegistry.emplace<VkBufferViewCreateInfo>(new_entity, std::move(view_info));
    VkBufferView buffer_view = VK_NULL_HANDLE;
    VkResult result = vkCreateBufferView(device->vkHandle(), &local_view_info, nullptr, &buffer_view);
    VkAssert(result);

    resourceRegistry.emplace<VkBufferView>(new_entity, buffer_view);

    if constexpr (RENDERING_CONTEXT_USE_DEBUG_INFO && RENDERING_CONTEXT_VALIDATION_ENABLED)
    {
        if (_flags & resource_creation_flag_bits::UserDataAsString)
        {
            const std::string object_name = std::format("{}_buffer_view", reinterpret_cast<const char*>(user_data_ptr));
            result = RenderingContext::SetObjectName(VK_OBJECT_TYPE_BUFFER_VIEW, reinterpret_cast<uint64_t>(buffer_view), RENDERING_CONTEXT_DEBUG_OBJECT_NAME(object_name.c_str()));
            VkAssert(result);
        }
    }

    return buffer_view;
}

void ResourceContextImpl::setBufferDataHostOnly(entt::entity new_entity, InternalResourceDataContainer& dataContainer)
{
    const VmaAllocationInfo& alloc_info = resourceRegistry.get<VmaAllocationInfo>(new_entity);
    const VmaAllocation& alloc = resourceRegistry.get<VmaAllocation>(new_entity);

    void* mapped_address = nullptr;
    if (alloc_info.pMappedData)
    {
        mapped_address = alloc_info.pMappedData;
    }
    else
    {
        VkResult result = vmaMapMemory(allocatorHandle, alloc, &mapped_address);
        VkAssert(result);
    }

    InternalResourceDataContainer::BufferDataVector& dataVector = std::get<InternalResourceDataContainer::BufferDataVector>(dataContainer.DataVector);
    
    size_t offset = 0u;
    for (size_t i = 0u; i < dataVector.size(); ++i)
    {
        void* curr_address = (void*)((size_t)mapped_address + offset);
        std::memcpy(curr_address, dataVector[i].data.get(), dataVector[i].size);
        offset += dataVector[i].size;
    }

    vmaUnmapMemory(allocatorHandle, alloc);
    vmaFlushAllocation(allocatorHandle, alloc, VK_WHOLE_SIZE, 0);
    // free copied memory, finally
    dataVector.clear();
}

VkImage ResourceContextImpl::createImage(
    entt::entity new_entity,
    VkImageCreateInfo&& image_info,
    const resource_creation_flags _flags,
    const resource_usage _resource_usage,
    void* user_data_ptr,
    bool has_initial_data)
{
    VkImageCreateInfo& image_create_info = resourceRegistry.emplace<VkImageCreateInfo>(new_entity, std::move(image_info));
    const ResourceFlags& flags = resourceRegistry.emplace<ResourceFlags>(new_entity, resource_type::Image, _flags, _resource_usage);
    std::string debug_name;
    if (flags.flags & resource_creation_flag_bits::UserDataAsString)
    {
        debug_name = resourceRegistry.emplace<std::string>(new_entity, reinterpret_cast<const char*>(user_data_ptr));
    }

    VmaAllocationInfo& alloc_info = resourceRegistry.emplace<VmaAllocationInfo>(new_entity);
    VmaAllocation& alloc = resourceRegistry.emplace<VmaAllocation>(new_entity);

    if (!(image_create_info.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) || (flags.resourceUsage != resource_usage::GPUOnly))
    {
        image_create_info.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }

    VmaAllocationCreateInfo alloc_create_info
    {
        GetAllocationCreateFlags(flags.flags),
        GetVmaMemoryUsage(flags.resourceUsage),
        0u,
        0u,
        UINT32_MAX,
        VK_NULL_HANDLE,
        user_data_ptr
    };

    VkImage image_handle = VK_NULL_HANDLE;
    VkResult result = vmaCreateImage(allocatorHandle, &image_create_info, &alloc_create_info, &image_handle, &alloc, &alloc_info);
    resourceRegistry.emplace<VkImage>(new_entity, image_handle);
    VkAssert(result);

    if constexpr (RENDERING_CONTEXT_USE_DEBUG_INFO && RENDERING_CONTEXT_VALIDATION_ENABLED)
    {
        if (flags.flags & resource_creation_flag_bits::UserDataAsString)
        {
            const char* debugName = RENDERING_CONTEXT_DEBUG_OBJECT_NAME(debug_name.c_str());
            std::string debugAllocationName = std::format("{}_allocation", debugName);
            result = RenderingContext::SetObjectName(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(image_handle), RENDERING_CONTEXT_DEBUG_OBJECT_NAME(debug_name.c_str()));
            VkAssert(result);
            vmaSetAllocationName(allocatorHandle, alloc, debugAllocationName.c_str());
        }
    }

    return image_handle;
}

VkImageView ResourceContextImpl::createImageView(
    entt::entity new_entity,
    const VkImageViewCreateInfo& view_info,
    const resource_creation_flags resource_flags)
{
    // need a valid parent image to attach to
    VkImage image_handle = resourceRegistry.get<VkImage>(new_entity);
    if (!image_handle)
    {
        return VK_NULL_HANDLE;
    }

    VkImageViewCreateInfo& image_view_create_info = resourceRegistry.emplace<VkImageViewCreateInfo>(new_entity, std::move(view_info));
    image_view_create_info.image = image_handle;

    VkImageView image_view = VK_NULL_HANDLE;
    VkResult result = vkCreateImageView(device->vkHandle(), &image_view_create_info, nullptr, &image_view);
    VkAssert(result);

    if (resource_flags & resource_creation_flag_bits::UserDataAsString)
    {
        const std::string& parent_name = resourceRegistry.get<std::string>(new_entity);
        const std::string object_name = parent_name + "_image_view";
        result = RenderingContext::SetObjectName(VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<uint64_t>(image_view), RENDERING_CONTEXT_DEBUG_OBJECT_NAME(object_name.c_str()));
        VkAssert(result);
    }

    return image_view;
}

VkSampler ResourceContextImpl::createSampler(
    entt::entity new_entity,
    const VkSamplerCreateInfo& sampler_info,
    const resource_creation_flags _flags,
    void* user_data_ptr)
{
    const VkSamplerCreateInfo& sampler_create_info = resourceRegistry.emplace<VkSamplerCreateInfo>(new_entity, std::move(sampler_info));
    VkSampler sampler = VK_NULL_HANDLE;
    VkResult result = vkCreateSampler(device->vkHandle(), &sampler_create_info, nullptr, &sampler);
    VkAssert(result);

    if (_flags & resource_creation_flag_bits::UserDataAsString)
    {
        // can use entity system to find out if this is a combined image sampler or just a sampler, try and find parent name
        // kinda slower than we want but this is only active in debug builds really so w/e
        std::string debug_name;
        if (resourceRegistry.try_get<std::string>(new_entity))
        {
            debug_name = resourceRegistry.get<std::string>(new_entity);
            debug_name += "_sampler";
        }
        else
        {
            debug_name = std::string(reinterpret_cast<const char*>(user_data_ptr));
        }
        result = RenderingContext::SetObjectName(VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<uint64_t>(sampler), RENDERING_CONTEXT_DEBUG_OBJECT_NAME(debug_name.c_str()));
        VkAssert(result);
    }

    return sampler;
}

MessageReply::Status ResourceContextImpl::destroyBuffer(entt::entity entity, GraphicsResource resource)
{
    VkBuffer local_buffer_handle = resourceRegistry.get<VkBuffer>(entity);
    if (!local_buffer_handle)
    {
        return MessageReply::Status::Failed;
    }

    if (local_buffer_handle != (VkBuffer)resource.VkHandle)
    {
        return MessageReply::Status::Failed;
    }

    vkDestroyBuffer(device->vkHandle(), local_buffer_handle, nullptr);
    resourceRegistry.replace<VkBuffer>(entity, VK_NULL_HANDLE);

    return MessageReply::Status::Completed;
}

MessageReply::Status ResourceContextImpl::destroyImage(entt::entity entity, GraphicsResource resource)
{
    VkImage local_image_handle = resourceRegistry.get<VkImage>(entity);
    if (!local_image_handle)
    {
        return MessageReply::Status::Failed;
    }

    if (local_image_handle != (VkImage)resource.VkHandle)
    {
        return MessageReply::Status::Failed;
    }

    vkDestroyImage(device->vkHandle(), local_image_handle, nullptr);
    resourceRegistry.replace<VkImage>(entity, VK_NULL_HANDLE);

    return MessageReply::Status::Completed;
}

MessageReply::Status ResourceContextImpl::destroySampler(entt::entity entity, GraphicsResource resource)
{
    VkSampler local_sampler_handle = resourceRegistry.get<VkSampler>(entity);
    if (!local_sampler_handle)
    {
        return MessageReply::Status::Failed;
    }

    if (local_sampler_handle != (VkSampler)resource.VkHandle)
    {
        return MessageReply::Status::Failed;
    }

    vkDestroySampler(device->vkHandle(), local_sampler_handle, nullptr);
    resourceRegistry.replace<VkSampler>(entity, VK_NULL_HANDLE);

    return MessageReply::Status::Completed;
}

MessageReply::Status ResourceContextImpl::destroyBufferView(entt::entity entity, GraphicsResource resource)
{
    VkBufferView local_buffer_view_handle = resourceRegistry.get<VkBufferView>(entity);
    if (!local_buffer_view_handle)
    {
        return MessageReply::Status::Failed;
    }

    if ((VkBufferView)resource.VkViewHandle != local_buffer_view_handle)
    {
        return MessageReply::Status::Failed;
    }

    vkDestroyBufferView(device->vkHandle(), local_buffer_view_handle, nullptr);
    resourceRegistry.replace<VkBufferView>(entity, VK_NULL_HANDLE);

    return MessageReply::Status::Completed;
}

MessageReply::Status ResourceContextImpl::destroyImageView(entt::entity entity, GraphicsResource resource)
{
    VkImageView local_image_view_handle = resourceRegistry.get<VkImageView>(entity);
    if (!local_image_view_handle)
    {
        return MessageReply::Status::Failed;
    }

    if ((VkImageView)resource.VkViewHandle != local_image_view_handle)
    {
        return MessageReply::Status::Failed;
    }

    vkDestroyImageView(device->vkHandle(), local_image_view_handle, nullptr);
    resourceRegistry.replace<VkImageView>(entity, VK_NULL_HANDLE);

    return MessageReply::Status::Completed;
}

MessageReply::Status ResourceContextImpl::destroyCombinedImageSampler(entt::entity entity, GraphicsResource resource)
{
    destroyImage(entity, resource);
    destroyImageView(entity, resource);
    destroySampler(entity, resource);
    return MessageReply::Status::Completed;
}

void ResourceContextImpl::writeStatsJsonFile(const char* output_file)
{
    char* output;
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

    VkMemoryPropertyFlags GetMemoryPropertyFlags(resource_usage _resource_usage) noexcept
    {
        switch (_resource_usage)
        {
        case resource_usage::GPUOnly:
            return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        case resource_usage::CPUOnly:
            return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        case resource_usage::CPUToGPU:
            [[fallthrough]];
        case resource_usage::GPUToCPU:
            return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        default:
            __assume(0);
        }
    }

    VkFormatFeatureFlags GetFormatFeatureFlagsFromUsage(const VkImageUsageFlags flags) noexcept
    {
        VkFormatFeatureFlags result = 0;
        if (flags & VK_IMAGE_USAGE_SAMPLED_BIT)
        {
            result |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        }
        if (flags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
        {
            result |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
        }
        if (flags & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
        {
            result |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
        }
        if (flags & VK_IMAGE_USAGE_STORAGE_BIT)
        {
            result |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
        }
        return result;
    }

    VmaAllocationCreateFlags GetAllocationCreateFlags(const resource_creation_flags flags) noexcept
    {
        VmaAllocationCreateFlags result = 0u;
        
        if (flags & resource_creation_flag_bits::DedicatedMemory)
        {
            result |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
        }

        if (flags & resource_creation_flag_bits::CreateMapped)
        {
            result |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
        }

        if (flags & resource_creation_flag_bits::PersistentlyMapped)
        {
            result |= VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        }

        if (flags & resource_creation_flag_bits::HostWritesLinear)
        {
            result |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        }

        if (flags & resource_creation_flag_bits::HostWritesRandom)
        {
            result |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        }

        return result;
    }

    VmaMemoryUsage GetVmaMemoryUsage(const resource_usage _resource_usage) noexcept
    {
        switch (_resource_usage)
        {
        case resource_usage::GPUOnly:
            return VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        case resource_usage::CPUOnly:
            return VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        default:
            return VMA_MEMORY_USAGE_AUTO;
        }
    }

}
