#include "ResourceContextImpl.hpp"

#include "ResourceContext.hpp"
#include "../../rendering_context/include/RenderingContext.hpp"
#include "Instance.hpp"

#include <fstream>
#include <format>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#define THSVS_SIMPLER_VULKAN_SYNCHRONIZATION_IMPLEMENTATION
#include <thsvs_simpler_vulkan_synchronization.h>

namespace
{
    std::vector<ThsvsAccessType> thsvsAccessTypesFromBufferUsage(VkBufferUsageFlags _flags);
    VkAccessFlags accessFlagsFromBufferUsage(VkBufferUsageFlags usage_flags);
    std::vector<ThsvsAccessType> thsvsAccessTypesFromImageUsage(VkImageUsageFlags _flags);
    VkAccessFlags accessFlagsFromImageUsage(const VkImageUsageFlags usage_flags);
    VkImageLayout imageLayoutFromUsage(const VkImageUsageFlags usage_flags);
    VkMemoryPropertyFlags GetMemoryPropertyFlags(resource_usage _resource_usage) noexcept;
    VkFormatFeatureFlags GetFormatFeatureFlagsFromUsage(const VkImageUsageFlags flags) noexcept;
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

    VmaAllocatorCreateInfo create_info
    {
        applicationInfo.apiVersion >= VK_API_VERSION_1_1 ? VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT : 0u,
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

    auto& transfer_system = ResourceTransferSystem::GetTransferSystem();
    transfer_system.Initialize(device, allocatorHandle);

    startWorker();

}

void ResourceContextImpl::destroy()
{
    for (auto& resource : resources)
    {
        destroyResource(resource.get());
    }

    resources.clear();
    resourceAllocations.clear();
    resourceNames.clear();
    imageViews.clear();
    allocInfos.clear();
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

void ResourceContextImpl::destroyResource(VulkanResource* rsrc)
{

    decltype(resources)::const_iterator iter = std::find_if(
        std::begin(resources), std::end(resources), 
        [rsrc](const decltype(resources)::value_type& entry)
        {
            return entry.get() == rsrc;
        });

    if (iter == std::end(resources))
    {
        std::cerr << "Tried to erase resource that isn't in internal containers!\n";
        throw std::runtime_error("Tried to erase resource that isn't in internal containers!");
    }

    // for samplers, iterator becomes invalid after erased (and ordering was req'd for threading)
    VulkanResource* samplerResource = iter->get()->Sampler;
    switch (iter->get()->Type)
    {
    case resource_type::Buffer:
        destroyBuffer(iter);
        break;
    case resource_type::Image:
        destroyImage(iter);
        break;
    case resource_type::Sampler:
        destroySampler(iter);
        break;
    case resource_type::CombinedImageSampler:
        destroyImage(iter);
        assert(samplerResource);
        destroyResource(samplerResource);
        break;
    case resource_type::Invalid:
        [[fallthrough]];
    default:
        throw std::runtime_error("Invalid resource type!");
    }

    rsrc = nullptr;
}

void ResourceContextImpl::processCreateBufferMessage(CreateBufferMessage&& message)
{
    const entt::entity new_entity = resourceRegistry.create();
    
    VkBuffer buffer_handle = createBuffer(
        new_entity,
        std::move(message.bufferInfo),
        message.flags,
        message.resourceUsage,
        message.userData,
        message.initialData.has_value());

    resourceRegistry.emplace<VkBuffer>(new_entity, buffer_handle);

    VkBufferView buffer_view = VK_NULL_HANDLE;
    if (message.viewInfo)
    {
        buffer_view = createBufferView(new_entity, std::move(message.viewInfo.value()), message.flags, message.userData);
        resourceRegistry.emplace<VkBufferView>(new_entity, buffer_view);
    }

    if (message.initialData)
    {
        setBufferData(new_entity, buffer_handle, message.initialData.value(), message.resourceUsage);
    }

    message.reply->SetVulkanResource(
        resource_type::Buffer,
        static_cast<uint32_t>(new_entity),
        reinterpret_cast<uint64_t>(buffer_handle),
        reinterpret_cast<uint64_t>(buffer_view),
        0u);

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
    resourceRegistry.emplace<VkImage>(new_entity, image_handle);

    VkImageView image_view = VK_NULL_HANDLE;
    if (message.viewInfo.has_value())
    {
        image_view = createImageView(new_entity, message.viewInfo.value(), message.flags);
        resourceRegistry.emplace<VkImageView>(new_entity, image_view);
    }

    if (message.initialData.has_value())
    {
        const VkImageCreateInfo& image_info = resourceRegistry.get<VkImageCreateInfo>(new_entity);
        setImageInitialData(new_entity, image_handle, image_info, message.initialData.value(), message.flags);
    }

    message.reply->SetVulkanResource(
        resource_type::Image,
        static_cast<uint32_t>(new_entity),
        reinterpret_cast<uint64_t>(image_handle),
        reinterpret_cast<uint64_t>(image_view),
        0u);
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

    VkImageView image_view = createImageView(new_entity, message.viewInfo, message.flags);
    resourceRegistry.emplace<VkImageView>(new_entity, image_view);

    if (message.initialData.has_value())
    {
        const VkImageCreateInfo& image_info = resourceRegistry.get<VkImageCreateInfo>(new_entity);
        setImageInitialData(new_entity, image_handle, image_info, message.initialData.value(), message.flags);
    }

    VkSampler sampler = createSampler(new_entity, message.samplerInfo, message.flags, message.userData);
    resourceRegistry.emplace<VkSampler>(new_entity, sampler);
    
    message.reply->SetVulkanResource(
        resource_type::CombinedImageSampler,
        static_cast<uint32_t>(new_entity),
        reinterpret_cast<uint64_t>(image_handle),
        reinterpret_cast<uint64_t>(image_view),
        reinterpret_cast<uint64_t>(sampler));
}

void ResourceContextImpl::processCreateSamplerMessage(CreateSamplerMessage&& message)
{
    const entt::entity new_entity = resourceRegistry.create();
    VkSampler sampler = createSampler(new_entity, message.samplerInfo, message.flags, message.userData);
    resourceRegistry.emplace<VkSampler>(new_entity, sampler);

    message.reply->SetVulkanResource(
        resource_type::Sampler,
        static_cast<uint32_t>(new_entity),
        0u, 0u, reinterpret_cast<uint64_t>(sampler));
}

void ResourceContextImpl::processSetBufferDataMessage(SetBufferDataMessage&& message)
{
    const VulkanResource& buffer = message.destBuffer;
    const entt::entity entity = entt::entity(buffer.ResourceHandle);
    if (!resourceRegistry.valid(entity))
    {
        message.reply->SetStatus(StatusMessageReply::Status::Failed);
        return;
    }

    const VkBuffer buffer_handle = resourceRegistry.get<VkBuffer>(entity);
    if (!buffer_handle)
    {
        message.reply->SetStatus(StatusMessageReply::Status::Failed);
        return;
    }

    const ResourceFlags& flags = resourceRegistry.get<ResourceFlags>(entity);

    setBufferData(entity, buffer_handle, message.data, flags.resourceUsage);

    message.reply->SetStatus(StatusMessageReply::Status::Success);
}

void ResourceContextImpl::processFillResourceMessage(FillResourceMessage&& message)
{
    const VulkanResource& buffer = message.resource;
    const entt::entity entity = entt::entity(buffer.ResourceHandle);
    if (!resourceRegistry.valid(entity))
    {
        message.reply->SetStatus(StatusMessageReply::Status::Failed);
        return;
    }

    const VkBuffer buffer_handle = resourceRegistry.get<VkBuffer>(entity);
    if (!buffer_handle)
    {
        message.reply->SetStatus(StatusMessageReply::Status::Failed);
        return;
    }

    fillBuffer(entity, buffer_handle, message.value, message.offset, message.size);

    message.reply->SetStatus(StatusMessageReply::Status::Success);
}

void ResourceContextImpl::processMapResourceMessage(MapResourceMessage&& message)
{
}

void ResourceContextImpl::processUnmapResourceMessage(UnmapResourceMessage&& message)
{
}

void ResourceContextImpl::processCopyResourceMessage(CopyResourceMessage&& message)
{
}   

void ResourceContextImpl::processCopyResourceContentsMessage(CopyResourceContentsMessage&& message)
{

}

void ResourceContextImpl::processDestroyResourceMessage(DestroyResourceMessage&& message)
{

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
    const ResourceFlags& flags = resourceRegistry.emplace<ResourceFlags>(new_entity, resource_type::Buffer, _flags, 0x0, _resource_usage);
    ResourceDebugName debug_name;
    if (flags.flags & resource_creation_flag_bits::ResourceCreateUserDataAsString)
    {
        // working with value instead of ref fine because we're just gonna read it for the debug name setting in a sec
        // don't use macro yet because that contains timestamp info!
        debug_name = resourceRegistry.emplace<ResourceDebugName>(new_entity, reinterpret_cast<const char*>(user_data_ptr));
    }

    VmaAllocationInfo& alloc_info = resourceRegistry.emplace<VmaAllocationInfo>(new_entity);
    VmaAllocation& alloc = resourceRegistry.emplace<VmaAllocation>(new_entity);

    if ((flags.resourceUsage == resource_usage::CPUToGPU || flags.resourceUsage == resource_usage::GPUToCPU || flags.resourceUsage == resource_usage::GPUOnly) && has_initial_data)
    {
        buffer_create_info.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }

    VmaAllocationCreateInfo alloc_create_info
    {
        (VmaAllocationCreateFlags)flags.flags,
        (VmaMemoryUsage)flags.resourceUsage,
        GetMemoryPropertyFlags(flags.resourceUsage),
        0u,
        UINT32_MAX,
        VK_NULL_HANDLE,
        user_data_ptr
    };

    VkBuffer buffer_handle = VK_NULL_HANDLE;
    VkResult result = vmaCreateBuffer(allocatorHandle, &buffer_create_info, &alloc_create_info, &buffer_handle, &alloc, &alloc_info);
    VkAssert(result);
    
    if constexpr (VTF_USE_DEBUG_INFO && VTF_VALIDATION_ENABLED)
    {
        if (flags.flags & resource_creation_flag_bits::ResourceCreateUserDataAsString)
        {
            result = RenderingContext::SetObjectName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(buffer_handle), VTF_DEBUG_OBJECT_NAME(debug_name.c_str()));
            VkAssert(result);
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

    if constexpr (VTF_USE_DEBUG_INFO && VTF_VALIDATION_ENABLED)
    {
        if (_flags & resource_creation_flag_bits::ResourceCreateUserDataAsString)
        {
            const std::string object_name = std::format("{}_buffer_view", reinterpret_cast<const char*>(user_data_ptr));
            result = RenderingContext::SetObjectName(VK_OBJECT_TYPE_BUFFER_VIEW, reinterpret_cast<uint64_t>(buffer_view), VTF_DEBUG_OBJECT_NAME(object_name.c_str()));
            VkAssert(result);
        }
    }

    return buffer_view;
}

void ResourceContextImpl::setBufferData(entt::entity new_entity, VkBuffer buffer_handle, InternalResourceDataContainer& dataContainer, resource_usage _resource_usage)
{
    if (_resource_usage == resource_usage::CPUOnly)
    {
        setBufferDataHostOnly(new_entity, dataContainer);
    }
    else
    {
        setBufferDataUploadBuffer(new_entity, dataContainer);
    }
}

void ResourceContextImpl::setBufferDataHostOnly(entt::entity new_entity, InternalResourceDataContainer& dataContainer)
{
    const VmaAllocationInfo& alloc_info = resourceRegistry.get<VmaAllocationInfo>(new_entity);
    const VmaAllocation& alloc = resourceRegistry.get<VmaAllocation>(new_entity);

    void* mapped_address = nullptr;
    VkResult result = vmaMapMemory(allocatorHandle, alloc, &mapped_address);
    VkAssert(result);

    InternalResourceDataContainer::BufferDataVector dataVector = std::get<InternalResourceDataContainer::BufferDataVector>(dataContainer.DataVector);
    size_t offset = 0u;
    for (size_t i = 0u; i < dataVector.size(); ++i)
    {
        void* curr_address = (void*)((size_t)mapped_address + offset);
        std::memcpy(curr_address, dataVector[i].data.get(), dataVector[i].size);
        offset += dataVector[i].size;
    }
    vmaUnmapMemory(allocatorHandle, alloc);
    vmaFlushAllocation(allocatorHandle, alloc, 0u, offset);
    // free copied memory, finally
    dataVector.clear();
}

void ResourceContextImpl::setBufferDataUploadBuffer(entt::entity new_entity, InternalResourceDataContainer& dataContainer)
{
    const VkBufferCreateInfo& buffer_create_info = resourceRegistry.get<VkBufferCreateInfo>(new_entity);
    const VkBuffer buffer_handle = resourceRegistry.get<VkBuffer>(new_entity);

    auto& transfer_system = ResourceTransferSystem::GetTransferSystem();
    UploadBuffer* upload_buffer = transfer_system.CreateUploadBuffer(buffer_create_info.size);

    // note that this is copying to an API managed staging buffer, not just another raw data buffer like our data container. will only be briefly duplicated
    InternalResourceDataContainer::BufferDataVector dataVector = std::get<InternalResourceDataContainer::BufferDataVector>(dataContainer.DataVector);
    std::vector<VkBufferCopy> buffer_copies(dataVector.size());
    VkDeviceSize offset = 0;
    for (size_t i = 0; i < dataVector.size(); ++i)
    {
        upload_buffer->SetData(dataVector[i].data.get(), dataVector[i].size, offset);
        buffer_copies[i].size = dataVector[i].size;
        buffer_copies[i].dstOffset = offset;
        buffer_copies[i].srcOffset = offset;
        offset += dataVector[i].size;
    }

    auto cmd = transfer_system.TransferCmdBuffer();
    vkCmdCopyBuffer(cmd, upload_buffer->Buffer, buffer_handle, static_cast<uint32_t>(buffer_copies.size()), buffer_copies.data());

    constexpr static ThsvsAccessType transfer_access_types[1]
    {
        THSVS_ACCESS_TRANSFER_WRITE
    };

    const std::vector<ThsvsAccessType> possible_accesses = thsvsAccessTypesFromBufferUsage(buffer_create_info.usage);
    
    const ThsvsGlobalBarrier global_barrier
    {
        1u,
        transfer_access_types,
        static_cast<uint32_t>(possible_accesses.size()),
        possible_accesses.data()
    };

    thsvsCmdPipelineBarrier(cmd, &global_barrier, 1u, nullptr, 0u, nullptr);

    // we can clear and free the stored data now
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
    VkImageCreateInfo& image_create_info = resourceRegistry.emplace<VkImageCreateInfo>(new_entity, std::move(message.imageInfo));
    const ResourceFlags& flags = resourceRegistry.emplace<ResourceFlags>(new_entity, resource_type::Image, message.flags, 0x0, message.resourceUsage);
    ResourceDebugName debug_name;
    if (flags.flags & resource_creation_flag_bits::ResourceCreateUserDataAsString)
    {
        debug_name = resourceRegistry.emplace<ResourceDebugName>(new_entity, reinterpret_cast<const char*>(message.userData));
    }

    VmaAllocationInfo& alloc_info = resourceRegistry.emplace<VmaAllocationInfo>(new_entity);
    VmaAllocation& alloc = resourceRegistry.emplace<VmaAllocation>(new_entity);

    if (!(image_create_info.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) || (flags.resourceUsage != resource_usage::GPUOnly))
    {
        image_create_info.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }

    VmaAllocationCreateInfo alloc_create_info
    {
        (VmaAllocationCreateFlags)flags.flags,
        (VmaMemoryUsage)flags.resourceUsage,
        GetMemoryPropertyFlags(flags.resourceUsage),
        0u,
        UINT32_MAX,
        VK_NULL_HANDLE,
        message.userData
    };

    VkImage image_handle = VK_NULL_HANDLE;
    VkResult result = vmaCreateImage(allocatorHandle, &image_create_info, &alloc_create_info, &image_handle, &alloc, &alloc_info);
    resourceRegistry.emplace<VkImage>(new_entity, image_handle);
    VkAssert(result);

    if (flags.flags & resource_creation_flag_bits::ResourceCreateUserDataAsString)
    {
        result = RenderingContext::SetObjectName(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(image_handle), VTF_DEBUG_OBJECT_NAME(debug_name.c_str()));
        VkAssert(result);
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

    if (resource_flags & resource_creation_flag_bits::ResourceCreateUserDataAsString)
    {
        const std::string& parent_name = resourceRegistry.get<ResourceDebugName>(new_entity);
        const std::string object_name = parent_name + "_image_view";
        result = RenderingContext::SetObjectName(VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<uint64_t>(image_view), VTF_DEBUG_OBJECT_NAME(object_name.c_str()));
        VkAssert(result);
    }

    return image_view;
}

void ResourceContextImpl::setImageData(
    entt::entity new_entity,
    VkImage image_handle,
    const VkImageCreateInfo& image_info,
    InternalResourceDataContainer& dataContainer,
    const resource_creation_flags _flags)
{
    constexpr static ThsvsAccessType transfer_access_types[1]
    {
        THSVS_ACCESS_TRANSFER_WRITE
    };

    // things will get really broken with images if this is true
    // will handle concurrent sharing mode in the future if we need to
    assert(image_info.sharingMode != VK_SHARING_MODE_EXCLUSIVE);

    // required, unlike with buffers. forces a layout transition.
    const ThsvsImageBarrier pre_transfer_barrier
    {
        0u,
        nullptr,
        1u,
        transfer_access_types,
        THSVS_IMAGE_LAYOUT_GENERAL,
        THSVS_IMAGE_LAYOUT_OPTIMAL,
        VK_FALSE,
        VK_QUEUE_FAMILY_IGNORED,
        VK_QUEUE_FAMILY_IGNORED,
        image_handle,
        VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0u, image_info.mipLevels, 0u, image_info.arrayLayers }
    };

    auto post_transfer_access_types = thsvsAccessTypesFromImageUsage(image_info.usage);

    ThsvsImageBarrier post_transfer_barrier
    {
        1u,
        transfer_access_types,
        static_cast<uint32_t>(post_transfer_access_types.size()),
        post_transfer_access_types.data(),
        THSVS_IMAGE_LAYOUT_OPTIMAL,
        THSVS_IMAGE_LAYOUT_OPTIMAL,
        VK_FALSE,
        VK_QUEUE_FAMILY_IGNORED,
        VK_QUEUE_FAMILY_IGNORED,
        image_handle,
        VkImageSubresourceRange { VK_IMAGE_ASPECT_COLOR_BIT, 0u, image_info.mipLevels, 0u, image_info.arrayLayers }
    };

    auto& transfer_system = ResourceTransferSystem::GetTransferSystem();
    VmaAllocation& alloc = resourceRegistry.get<VmaAllocation>(new_entity);
    UploadBuffer* upload_buffer = transfer_system.CreateUploadBuffer(alloc->GetSize());

    InternalResourceDataContainer::ImageDataVector imageDataVector = std::get<InternalResourceDataContainer::ImageDataVector>(dataContainer.DataVector);

    std::vector<VkBufferImageCopy> buffer_image_copies;
    buffer_image_copies.reserve(imageDataVector.size());
    size_t copy_offset = 0u;

    const uint32_t num_layers = dataContainer.NumLayers.value();

    for (uint32_t i = 0u; i < imageDataVector.size(); ++i)
    {
        VkBufferImageCopy copy;
        copy.bufferOffset = copy_offset;
        copy.bufferRowLength = 0u;
        copy.bufferImageHeight = 0u;
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.baseArrayLayer = imageDataVector[i].arrayLayer;
        copy.imageSubresource.layerCount = num_layers;
        copy.imageSubresource.mipLevel = imageDataVector[i].mipLevel;
        copy.imageOffset = VkOffset3D{ 0, 0, 0 };
        copy.imageExtent = VkExtent3D{ imageDataVector[i].width, imageDataVector[i].height, 1u };
        buffer_image_copies.emplace_back(std::move(copy));
        assert(imageDataVector[i].mipLevel < image_info.mipLevels);
        assert(imageDataVector[i].arrayLayer < image_info.arrayLayers);
    }

    auto cmd = transfer_system.TransferCmdBuffer();
    
    thsvsCmdPipelineBarrier(cmd, nullptr, 0u, nullptr, 1u, &pre_transfer_barrier);
    vkCmdCopyBufferToImage(
        cmd,
        upload_buffer->Buffer,
        image_handle,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        static_cast<uint32_t>(buffer_image_copies.size()),
        buffer_image_copies.data());
    thsvsCmdPipelineBarrier(cmd, nullptr, 0u, nullptr, 1u, &post_transfer_barrier);
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

    if (_flags & resource_creation_flag_bits::ResourceCreateUserDataAsString)
    {
        // can use entity system to find out if this is a combined image sampler or just a sampler, try and find parent name
        // kinda slower than we want but this is only active in debug builds really so w/e
        std::string debug_name;
        if (resourceRegistry.try_get<ResourceDebugName>(new_entity))
        {
            debug_name = resourceRegistry.get<ResourceDebugName>(new_entity);
            debug_name += "_sampler";
        }
        else
        {
            debug_name = std::string(reinterpret_cast<const char*>(user_data_ptr));
        }
        result = RenderingContext::SetObjectName(VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<uint64_t>(sampler), VTF_DEBUG_OBJECT_NAME(debug_name.c_str()));
        VkAssert(result);
    }

    return sampler;
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

void ResourceContextImpl::createBufferResourceCopy(VulkanResource* src, VulkanResource** dst)
{
    const VkBufferCreateInfo* create_info = reinterpret_cast<const VkBufferCreateInfo*>(src->Info);
    const VkBufferViewCreateInfo* view_info = nullptr;
    if (reinterpret_cast<VkBufferView>(src->ViewHandle) != VK_NULL_HANDLE)
    {
        view_info = reinterpret_cast<const VkBufferViewCreateInfo*>(src->ViewInfo);
    }
    *dst = createBuffer(create_info, view_info, 0, nullptr, resourceInfos.resourceMemoryType.at(src), 0, nullptr);
}

void ResourceContextImpl::createImageResourceCopy(VulkanResource* src, VulkanResource** dst)
{
    const VkImageCreateInfo* image_info = reinterpret_cast<const VkImageCreateInfo*>(src->Info);
    const VkImageViewCreateInfo* view_info = nullptr;
    if (reinterpret_cast<VkImageView>(src->ViewHandle) != VK_NULL_HANDLE)
    {
        view_info = reinterpret_cast<const VkImageViewCreateInfo*>(src->ViewInfo);
    }
    *dst = createImage(image_info, view_info, 0, nullptr, resourceInfos.resourceMemoryType.at(src), resourceInfos.resourceFlags.at(src), nullptr);
    copyResourceContents(src, *dst);
}

void ResourceContextImpl::createSamplerResourceCopy(VulkanResource * src, VulkanResource** dst)
{
    const VkSamplerCreateInfo* sampler_info = reinterpret_cast<const VkSamplerCreateInfo*>(src->Info);
    *dst = createSampler(sampler_info, resource_creation_flags(), nullptr);
}

void ResourceContextImpl::createCombinedImageSamplerResourceCopy(VulkanResource* src, VulkanResource** dest)
{
    createImageResourceCopy(src, dest);
    VulkanResource** sampler_to_create = &(*dest)->Sampler;
    createSamplerResourceCopy(src->Sampler, sampler_to_create);
    (*dest)->Type = resource_type::CombinedImageSampler;
}

void ResourceContextImpl::copyBufferContentsToBuffer(VulkanResource* src, VulkanResource* dst)
{

    const VkBufferCreateInfo* src_info = reinterpret_cast<const VkBufferCreateInfo*>(src->Info);
    const VkBufferCreateInfo* dst_info = reinterpret_cast<const VkBufferCreateInfo*>(dst->Info);
    assert(src_info->size <= dst_info->size);
    const VkBufferCopy copy
    {
        0u,
        0u,
        src_info->size
    };

    const VkBufferMemoryBarrier pre_transfer_barriers[2]
    {
        VkBufferMemoryBarrier
        {
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            nullptr,
            accessFlagsFromBufferUsage(src_info->usage),
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkBuffer)src->Handle,
            0,
            src_info->size
        },
        VkBufferMemoryBarrier
        {
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            nullptr,
            accessFlagsFromBufferUsage(dst_info->usage),
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkBuffer)dst->Handle,
            0,
            dst_info->size
        }
    };

    const VkBufferMemoryBarrier post_transfer_barriers[2]
    {
        VkBufferMemoryBarrier
        {
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            nullptr,
            VK_ACCESS_TRANSFER_READ_BIT,
            accessFlagsFromBufferUsage(src_info->usage),
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkBuffer)src->Handle,
            0,
            src_info->size
        },
        VkBufferMemoryBarrier
        {
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            nullptr,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            accessFlagsFromBufferUsage(dst_info->usage),
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkBuffer)dst->Handle,
            0,
            dst_info->size
        }
    };

    
    auto& transfer_system = ResourceTransferSystem::GetTransferSystem();
    auto cmd = transfer_system.TransferCmdBuffer();

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, nullptr, 2u, pre_transfer_barriers, 0u, nullptr);
    vkCmdCopyBuffer(cmd, (VkBuffer)src->Handle, (VkBuffer)dst->Handle, 1, &copy);
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0u, 0u, nullptr, 2u, post_transfer_barriers, 0u, nullptr);
    
}

void ResourceContextImpl::copyImageContentsToImage(VulkanResource* src, VulkanResource* dst, const VkImageSubresourceRange& src_range, const VkImageSubresourceRange& dst_range,
    const std::vector<VkImageCopy>& image_copies)
{

    const VkImageCreateInfo& src_info = resourceInfos.imageInfos.at(src);
    assert(src_info.sharingMode == VK_SHARING_MODE_CONCURRENT);
    const VkImageCreateInfo& dst_info = resourceInfos.imageInfos.at(dst);

    // these will be used to transition back to the right layout after the transfer
    // (and to specify right layout we're transitioning from)
    const auto src_accesses = thsvsAccessTypesFromImageUsage(src_info.usage);
    const auto dst_accesses = thsvsAccessTypesFromImageUsage(dst_info.usage);

    constexpr static ThsvsAccessType transfer_access_type_write[1]
    {
        THSVS_ACCESS_TRANSFER_WRITE
    };

    constexpr static ThsvsAccessType transfer_access_type_read[1]
    {
        THSVS_ACCESS_TRANSFER_READ
    };

    const ThsvsImageBarrier pre_transfer_barriers[2]
    {
        ThsvsImageBarrier
        {
            static_cast<uint32_t>(src_accesses.size()),
            src_accesses.data(),
            1u,
            transfer_access_type_read,
            THSVS_IMAGE_LAYOUT_OPTIMAL,
            THSVS_IMAGE_LAYOUT_OPTIMAL,
            VK_FALSE,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkImage)src->Handle,
            src_range
        },
        ThsvsImageBarrier
        {
            static_cast<uint32_t>(dst_accesses.size()),
            dst_accesses.data(),
            1u,
            transfer_access_type_write,
            THSVS_IMAGE_LAYOUT_OPTIMAL,
            THSVS_IMAGE_LAYOUT_OPTIMAL,
            VK_FALSE,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkImage)dst->Handle,
            dst_range
        }
    };

    const ThsvsImageBarrier post_transfer_barriers[2]
    {
        ThsvsImageBarrier
        {
            1u,
            transfer_access_type_read,
            static_cast<uint32_t>(src_accesses.size()),
            src_accesses.data(),
            THSVS_IMAGE_LAYOUT_OPTIMAL,
            THSVS_IMAGE_LAYOUT_OPTIMAL,
            VK_FALSE,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkImage)src->Handle,
            src_range
        },
        ThsvsImageBarrier
        {
            1u,
            transfer_access_type_write,
            static_cast<uint32_t>(dst_accesses.size()),
            dst_accesses.data(),
            THSVS_IMAGE_LAYOUT_OPTIMAL,
            THSVS_IMAGE_LAYOUT_OPTIMAL,
            VK_FALSE,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkImage)dst->Handle,
            dst_range
        }
    };

    const VkImageLayout src_layout = imageLayoutFromUsage(src_info.usage);
    const VkImageLayout dst_layout = imageLayoutFromUsage(dst_info.usage);

    auto& transfer_system = ResourceTransferSystem::GetTransferSystem();
    auto cmd = transfer_system.TransferCmdBuffer();

    thsvsCmdPipelineBarrier(cmd, nullptr, 0u, nullptr, 2u, pre_transfer_barriers);
    vkCmdCopyImage(cmd, (VkImage)src->Handle, src_layout, (VkImage)dst->Handle, dst_layout, static_cast<uint32_t>(image_copies.size()), image_copies.data());
    thsvsCmdPipelineBarrier(cmd, nullptr, 0u, nullptr, 2u, post_transfer_barriers);

}

void ResourceContextImpl::copyBufferContentsToImage(VulkanResource* src, VulkanResource* dst, const VkDeviceSize src_offset, const VkImageSubresourceRange& dst_range,
    const std::vector<VkBufferImageCopy>& copy_params)
{
    assert((dst->Type == resource_type::Image || dst->Type == resource_type::CombinedImageSampler) && (src->Type == resource_type::Buffer));

    const VkBufferCreateInfo& src_info = resourceInfos.bufferInfos.at(src);
    const VkImageCreateInfo& dst_info = resourceInfos.imageInfos.at(dst);

    // these will be used to transition back to the right layout after the transfer
    // (and to specify right layout we're transitioning from)
    const auto src_accesses = thsvsAccessTypesFromBufferUsage(src_info.usage);
    const auto dst_accesses = thsvsAccessTypesFromImageUsage(dst_info.usage);

    constexpr static ThsvsAccessType transfer_access_type_write[1]
    {
        THSVS_ACCESS_TRANSFER_WRITE
    };

    constexpr static ThsvsAccessType transfer_access_type_read[1]
    {
        THSVS_ACCESS_TRANSFER_READ
    };

    const ThsvsBufferBarrier pre_transfer_buffer_barrier[1]
    {
        ThsvsBufferBarrier
        {
            static_cast<uint32_t>(src_accesses.size()),
            src_accesses.data(),
            1u,
            transfer_access_type_read,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkBuffer)src->Handle,
            src_offset,
            src_info.size
        }
    };

    const ThsvsImageBarrier pre_transfer_image_barrier[1]
    {
        ThsvsImageBarrier
        {
            static_cast<uint32_t>(dst_accesses.size()),
            dst_accesses.data(),
            1u,
            transfer_access_type_write,
            THSVS_IMAGE_LAYOUT_OPTIMAL,
            THSVS_IMAGE_LAYOUT_OPTIMAL,
            VK_FALSE,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkImage)dst->Handle,
            dst_range
        }
    };

    const ThsvsBufferBarrier post_transfer_buffer_barrier[1]
    {
        ThsvsBufferBarrier
        {
            1u,
            transfer_access_type_read,
            static_cast<uint32_t>(src_accesses.size()),
            src_accesses.data(),
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkBuffer)src->Handle,
            src_offset,
            src_info.size
        }
    };

    const ThsvsImageBarrier post_transfer_image_barrier[1]{
        ThsvsImageBarrier{
            1u,
            transfer_access_type_write,
            static_cast<uint32_t>(dst_accesses.size()),
            dst_accesses.data(),
            THSVS_IMAGE_LAYOUT_OPTIMAL,
            THSVS_IMAGE_LAYOUT_OPTIMAL,
            VK_FALSE,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkImage)dst->Handle,
            dst_range
        }
    };

    const VkImageLayout dst_layout = imageLayoutFromUsage(dst_info.usage);

    auto& transfer_system = ResourceTransferSystem::GetTransferSystem();
    auto cmd = transfer_system.TransferCmdBuffer();

    thsvsCmdPipelineBarrier(cmd, nullptr, 1u, pre_transfer_buffer_barrier, 1u, pre_transfer_image_barrier);
    vkCmdCopyBufferToImage(cmd, (VkBuffer)src->Handle, (VkImage)dst->Handle, dst_layout, static_cast<uint32_t>(copy_params.size()), copy_params.data());
    thsvsCmdPipelineBarrier(cmd, nullptr, 1u, post_transfer_buffer_barrier, 1u, post_transfer_image_barrier);

}

void ResourceContextImpl::copyImageContentsToBuffer(VulkanResource* src, VulkanResource* dst)
{
    assert((src->Type == resource_type::Image || src->Type == resource_type::CombinedImageSampler) && (dst->Type == resource_type::Buffer));

    throw std::runtime_error("Not yet implemented!");
}

void ResourceContextImpl::destroyBuffer(resource_iter_t iter)
{
    VulkanResource* rsrc = iter->get();
    if (rsrc->ViewHandle != 0)
    {
        vkDestroyBufferView(device->vkHandle(), (VkBufferView)rsrc->ViewHandle, nullptr);
    }
    vmaDestroyBuffer(allocatorHandle, (VkBuffer)rsrc->Handle, resourceAllocations.at(rsrc));
    resourceInfos.bufferInfos.erase(rsrc);
    resourceInfos.bufferViewInfos.erase(rsrc);
    resourceAllocations.erase(rsrc);
    resourceInfos.resourceMemoryType.erase(rsrc);
    resourceInfos.resourceFlags.erase(rsrc);
    resources.erase(iter);
}

void ResourceContextImpl::destroyImage(resource_iter_t iter)
{
    VulkanResource* rsrc = iter->get();
    if (rsrc->ViewHandle != 0)
    {
        vkDestroyImageView(device->vkHandle(), (VkImageView)rsrc->ViewHandle, nullptr);
    }
    vmaDestroyImage(allocatorHandle, (VkImage)rsrc->Handle, resourceAllocations.at(rsrc));
    resources.erase(iter);
    resourceInfos.imageInfos.erase(rsrc);
    resourceInfos.imageViewInfos.erase(rsrc);
    resourceAllocations.erase(rsrc);
    resourceInfos.resourceMemoryType.erase(rsrc);
    resourceInfos.resourceFlags.erase(rsrc);
}

void ResourceContextImpl::destroySampler(resource_iter_t iter)
{
    VulkanResource* rsrc = iter->get();
    vkDestroySampler(device->vkHandle(), (VkSampler)rsrc->Handle, nullptr);
    resourceInfos.samplerInfos.erase(rsrc);
    resources.erase(iter);
}

void* ResourceContextImpl::map(VulkanResource* resource, size_t size, size_t offset)
{
    resource_usage alloc_usage{ resource_usage::InvalidResourceUsage };
    VmaAllocation alloc{ VK_NULL_HANDLE };

    alloc = resourceAllocations.at(resource);
    alloc_usage = resourceInfos.resourceMemoryType.at(resource);
    if (alloc_usage == resource_usage::GPUToCPU)
    {
        vmaInvalidateAllocation(allocatorHandle, alloc, offset, size == 0u ? VK_WHOLE_SIZE : size);
    }
    
    void* mapped_ptr = nullptr;
    VkResult result = vmaMapMemory(allocatorHandle, alloc, &mapped_ptr);
    VkAssert(result);
    return mapped_ptr;
}

void ResourceContextImpl::unmap(VulkanResource* resource, size_t size, size_t offset)
{
    VmaAllocation alloc{ VK_NULL_HANDLE };
    resource_usage alloc_usage{ resource_usage::InvalidResourceUsage };

    alloc = resourceAllocations.at(resource);
    alloc_usage = resourceInfos.resourceMemoryType.at(resource);

    vmaUnmapMemory(allocatorHandle, alloc);
    if (alloc_usage == resource_usage::CPUToGPU)
    {
        vmaFlushAllocation(allocatorHandle, alloc, offset, size == 0u ? VK_WHOLE_SIZE : size);
    }
}

void ResourceContextImpl::processMessages()
{
    constexpr size_t k_numMessagesBeforeTransferUpdate = 16u;
    while (!shouldExitWorker.load())
    {
        while (!messageQueue.empty())
        {
            ResourceMessagePayloadType message = messageQueue.pop();
            std::visit([this](auto&& arg) { this->processMessage(std::forward<decltype(arg)>(arg)); }, message);
        }

    }
}

namespace
{
    std::vector<ThsvsAccessType> thsvsAccessTypesFromBufferUsage(VkBufferUsageFlags _flags)
    {
        std::vector<ThsvsAccessType> results;
        if (_flags & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
        {
            results.emplace_back(THSVS_ACCESS_ANY_SHADER_READ_UNIFORM_BUFFER);
        }
        if (_flags & VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
        {
            results.emplace_back(THSVS_ACCESS_INDEX_BUFFER);
        }
        if (_flags & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
        {
            results.emplace_back(THSVS_ACCESS_VERTEX_BUFFER);
        }
        if (_flags & VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
        {
            results.emplace_back(THSVS_ACCESS_INDIRECT_BUFFER);
        }
        if (_flags & VK_BUFFER_USAGE_CONDITIONAL_RENDERING_BIT_EXT)
        {
            results.emplace_back(THSVS_ACCESS_CONDITIONAL_RENDERING_READ_EXT);
        }
        if (_flags & VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT)
        {
            results.emplace_back(THSVS_ACCESS_ANY_SHADER_READ_SAMPLED_IMAGE_OR_UNIFORM_TEXEL_BUFFER);
        }
        if ((_flags & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) || (_flags & VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT))
        {
            results.emplace_back(THSVS_ACCESS_ANY_SHADER_WRITE);
        }
        if (_flags & VK_BUFFER_USAGE_TRANSFER_DST_BIT)
        {
            results.emplace_back(THSVS_ACCESS_TRANSFER_WRITE);
        }
        if (_flags & VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
        {
            results.emplace_back(THSVS_ACCESS_TRANSFER_READ);
        }

        if (results.empty())
        {
            // Didn't match any flags. Go super general.
            results.emplace_back(THSVS_ACCESS_ANY_SHADER_WRITE);
            results.emplace_back(THSVS_ACCESS_ANY_SHADER_READ_OTHER);
        }

        return results;
    }

    VkAccessFlags accessFlagsFromBufferUsage(VkBufferUsageFlags usage_flags)
    {
        if (usage_flags & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
        {
            return VK_ACCESS_UNIFORM_READ_BIT;
        }
        else if (usage_flags & VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
        {
            return VK_ACCESS_INDEX_READ_BIT;
        }
        else if (usage_flags & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
        {
            return VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        }
        else if (usage_flags & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
        {
            return VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        }
        else if (usage_flags & VK_BUFFER_USAGE_CONDITIONAL_RENDERING_BIT_EXT)
        {
            return VK_ACCESS_CONDITIONAL_RENDERING_READ_BIT_EXT;
        }
        else if ((usage_flags & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) || (usage_flags & VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT))
        {
            return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        }
        else if (usage_flags & VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT)
        {
            return VK_ACCESS_SHADER_READ_BIT;
        }
        else
        {
            return VK_ACCESS_MEMORY_READ_BIT;
        }
    }

    std::vector<ThsvsAccessType> thsvsAccessTypesFromImageUsage(VkImageUsageFlags _flags)
    {
        std::vector<ThsvsAccessType> results;

        if (_flags & VK_IMAGE_USAGE_SAMPLED_BIT)
        {
            results.emplace_back(THSVS_ACCESS_ANY_SHADER_READ_SAMPLED_IMAGE_OR_UNIFORM_TEXEL_BUFFER);
        }
        if (_flags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
        {
            results.emplace_back(THSVS_ACCESS_COLOR_ATTACHMENT_READ_WRITE);
        }
        if (_flags & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
        {
            results.emplace_back(THSVS_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ);
            results.emplace_back(THSVS_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE);
        }
        if (_flags & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)
        {
            results.emplace_back(THSVS_ACCESS_COLOR_ATTACHMENT_READ);
        }
        if (_flags & VK_IMAGE_USAGE_SHADING_RATE_IMAGE_BIT_NV)
        {
            results.emplace_back(THSVS_ACCESS_SHADING_RATE_READ_NV);
        }
        if (_flags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
        {
            results.emplace_back(THSVS_ACCESS_TRANSFER_READ);
        }
        /*
        if (_flags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        {
            results.emplace_back(THSVS_ACCESS_TRANSFER_WRITE);
        }
        */
        if (_flags & VK_IMAGE_USAGE_FRAGMENT_DENSITY_MAP_BIT_EXT)
        {
            results.emplace_back(THSVS_ACCESS_FRAGMENT_DENSITY_MAP_READ_EXT);
        }

        if (results.empty() || (_flags & VK_IMAGE_USAGE_STORAGE_BIT))
        {
            results.emplace_back(THSVS_ACCESS_ANY_SHADER_READ_OTHER);
            results.emplace_back(THSVS_ACCESS_ANY_SHADER_WRITE);
        }

        return results;
    }

    VkAccessFlags accessFlagsFromImageUsage(const VkImageUsageFlags usage_flags)
    {
        if (usage_flags & VK_IMAGE_USAGE_SAMPLED_BIT)
        {
            return VK_ACCESS_SHADER_READ_BIT;
        }
        else if (usage_flags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
        {
            return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        }
        else if (usage_flags & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
        {
            return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        }
        else if (usage_flags & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)
        {
            return VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
        }
        else
        {
            return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_MEMORY_READ_BIT;
        }
    }

    VkImageLayout imageLayoutFromUsage(const VkImageUsageFlags usage_flags)
    {
        if (usage_flags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
        {
            return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
        else if (usage_flags & VK_IMAGE_USAGE_SAMPLED_BIT)
        {
            return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        else if (usage_flags & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
        {
            return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        }
        else
        {
            return VK_IMAGE_LAYOUT_GENERAL;
        }
    }

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

}
