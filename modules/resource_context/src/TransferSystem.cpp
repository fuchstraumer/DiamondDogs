#include "TransferSystem.hpp"
#include "CommandPool.hpp"
#include "LogicalDevice.hpp"
#include "PhysicalDevice.hpp"
#include "Instance.hpp"
#include "Fence.hpp"
#include "Semaphore.hpp"
#include "vkAssert.hpp"
#include "UploadBuffer.hpp"
#include "../../rendering_context/include/RenderingContext.hpp"
#include "VkDebugUtils.hpp"
#include <array>

#define THSVS_SIMPLER_VULKAN_SYNCHRONIZATION_IMPLEMENTATION
#include <thsvs_simpler_vulkan_synchronization.h>

constexpr static size_t MAX_QUEUED_UPLOAD_BUFFERS = 128u;
constexpr static size_t k_defaultNumCommandBuffersPerPool = 8u;

constexpr static std::array<VkDeviceSize, 4u> stagingPoolSizeRanges
{
    (VkDeviceSize)128e6,
    (VkDeviceSize)256e6,
    (VkDeviceSize)512e6,
    (VkDeviceSize)1024e6 // This should not be in anything but builds I use for fun. Could end in disaster, heh
};

constexpr static VkBufferCreateInfo k_defaultStagingBufferCreateInfo
{
    VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    nullptr,
    0,
    4096u,
    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    VK_SHARING_MODE_EXCLUSIVE,
    0,
    nullptr
};

constexpr static VmaAllocationCreateInfo k_defaultAllocationCreateInfo
{
    VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
    VMA_MEMORY_USAGE_AUTO,
    0,
    0u,
    UINT32_MAX,
    VK_NULL_HANDLE,
    nullptr
};

constexpr static VkDebugUtilsLabelEXT queue_debug_label
{
    VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
    nullptr,
    "TransferQueue",
    { 244.0f / 255.0f, 158.0f / 255.0f, 66.0f / 255.0f, 1.0f }
};

VkCommandPoolCreateInfo getCreateInfo(const vpr::Device* device)
{
    constexpr static VkCommandPoolCreateInfo pool_info
    {
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        nullptr,
        VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        0
    };
    VkCommandPoolCreateInfo result = pool_info;
    result.queueFamilyIndex = device->QueueFamilyIndices().Transfer;
    return result;
}

ResourceTransferSystem::ResourceTransferSystem() : transferCmdPool(nullptr), device(nullptr), fence(nullptr) {}

ResourceTransferSystem::~ResourceTransferSystem()
{
    destroy();
}

void ResourceTransferSystem::destroy()
{
    ForceCompleteTransfers();
    for (auto pool : uploadPools)
    {
        vmaDestroyPool(allocatorHandle, pool);
    }
    uploadPools.clear();
    vmaDestroyAllocator(allocatorHandle);
    transferCmdPool.reset();
    fence.reset();
    device = nullptr;
}

void ResourceTransferSystem::flushTransfersIfNeeded()
{
    /*
        A bit of reasoning:
        - upload pools greater than one means we created a pool specifically to hold one massive allocation. once we
          reach this point, it should be safe to free that as it was created to return an upload buffer, we set it's data, we're done
        - uploadBuffers being greater than 50 just means we have a ton of pending transfers, and we're going to be accumulating a ton
          of RAM usage. flush the pending transfers to free up what memory we can
    */
    if (uploadPools.size() > 2u || uploadBuffers.size() > MAX_QUEUED_UPLOAD_BUFFERS)
    {
        ForceCompleteTransfers();
        std::cerr << "Had to flush transfers, incurs high cost and indicates overload of transfer system\n";
    }
}


std::unique_ptr<UploadBuffer> ResourceTransferSystem::createUploadBufferImpl(const size_t buffer_sz)
{

    std::unique_ptr<UploadBuffer> created_buffer = std::make_unique<UploadBuffer>(device, allocatorHandle);
    created_buffer->Size = buffer_sz;

    VkResult created_alloc = VK_SUCCESS;
    VmaAllocationCreateInfo alloc_create_info = k_defaultAllocationCreateInfo;

    do
    {
        VkBufferCreateInfo create_info = k_defaultStagingBufferCreateInfo;
        create_info.size = buffer_sz;
        alloc_create_info.pool = uploadPools.back();

        created_alloc = vmaCreateBuffer(
            allocatorHandle,
            &create_info,
            &alloc_create_info,
            &created_buffer->Buffer,
            &created_buffer->Allocation,
            nullptr);

        if (created_alloc == VK_ERROR_OUT_OF_DEVICE_MEMORY)
        {
            // create a new pool
            if (buffer_sz > lastPoolSize)
            {
                auto next_sz_iter = std::lower_bound(std::begin(stagingPoolSizeRanges), std::end(stagingPoolSizeRanges), buffer_sz);
                if (next_sz_iter == std::end(stagingPoolSizeRanges))
                {
                    throw std::out_of_range("Requested staging buffer size was bigger than maximum staging allocation size!");
                }
                lastPoolSize = *next_sz_iter;
                uploadPools.emplace_back(createPool());
            }
        }

    } while (created_alloc != VK_SUCCESS);

    return std::move(created_buffer);
}

void ResourceTransferSystem::Initialize(const vpr::Device* dvc)
{

    if (initialized)
    {
        return;
    }

    device = dvc;
    
    const auto& applicationInfo = device->ParentInstance()->ApplicationInfo();
    VmaAllocatorCreateFlags create_flags = VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT;
    if (applicationInfo.apiVersion < VK_API_VERSION_1_1)
    {
        create_flags |= VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT;
    }

    VmaAllocatorCreateInfo create_info = {};
    create_info.flags = create_flags;
    create_info.physicalDevice = device->GetPhysicalDevice().vkHandle();
    create_info.device = device->vkHandle();
    create_info.instance = device->ParentInstance()->vkHandle();
    create_info.vulkanApiVersion = applicationInfo.apiVersion;

    VkResult result = vmaCreateAllocator(&create_info, &allocatorHandle);
    VkAssert(result);

    uploadPools.emplace_back(createPool());

    transferCmdPool = std::make_unique<vpr::CommandPool>(dvc->vkHandle(), getCreateInfo(dvc));
    transferCmdPool->AllocateCmdBuffers(1, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    fence = std::make_unique<vpr::Fence>(dvc->vkHandle(), 0);

    constexpr static VkCommandBufferBeginInfo begin_info
    {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        nullptr,
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        nullptr
    };

    VkResult result = vkBeginCommandBuffer(transferCmdPool->GetCmdBuffer(0), &begin_info);
    VkAssert(result);
    initialized = true;

    if constexpr (VTF_VALIDATION_ENABLED && VTF_USE_DEBUG_INFO)
    {
        device->DebugUtilsHandler().vkCmdBeginDebugUtilsLabel(transferCmdPool->GetCmdBuffer(0), &queue_debug_label);
    }

    if constexpr (VTF_VALIDATION_ENABLED && VTF_USE_DEBUG_INFO)
    {
        static const std::string base_name{ "ResourceCtxt_TransferSystem_" };
        static const std::string pool_name = base_name + std::string("_CommandPool");
        static const std::string fence_name = base_name + std::string("_Fence");
        static const std::string cmd_buffer_name = base_name + std::string("_CmdBuffer");
        RenderingContext::SetObjectName(VK_OBJECT_TYPE_COMMAND_POOL, (uint64_t)transferCmdPool->vkHandle(), pool_name.c_str());
        RenderingContext::SetObjectName(VK_OBJECT_TYPE_FENCE, (uint64_t)fence->vkHandle(), fence_name.c_str());
        RenderingContext::SetObjectName(VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t)transferCmdPool->GetCmdBuffer(0u), cmd_buffer_name.c_str());
    }

}

void ResourceTransferSystem::ForceCompleteTransfers()
{

    if (!initialized)
    {
        throw std::runtime_error("Transfer system was not properly initialized!");
    }

    if (!cmdBufferDirty)
    {
        return;
    }

    if constexpr (VTF_VALIDATION_ENABLED && VTF_USE_DEBUG_INFO)
    {
        device->DebugUtilsHandler().vkCmdEndDebugUtilsLabel(transferCmdPool->GetCmdBuffer(0));
    }

    auto& pool = *transferCmdPool;
    if (vkEndCommandBuffer(pool[0]) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to end Transfer command buffer!");
    }

    VkTimelineSemaphoreSubmitInfo timeline_info
    {
        VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
        nullptr,
        0u,
        nullptr,
        static_cast<uint32_t>(transferSignalValues.size()),
        transferSignalValues.data(),
    };

    VkSubmitInfo submission
    {
        VK_STRUCTURE_TYPE_SUBMIT_INFO,
        &timeline_info,
        0,
        nullptr,
        nullptr,
        1,
        &pool[0],
        static_cast<uint32_t>(transferSignalSemaphores.size()),
        transferSignalSemaphores.data()
    };

    VkResult result = vkQueueSubmit(device->TransferQueue(), 1, &submission, fence->vkHandle());
    VkAssert(result);
    result = vkWaitForFences(device->vkHandle(), 1, &fence->vkHandle(), VK_TRUE, UINT64_MAX);
    VkAssert(result);
    result = vkResetFences(device->vkHandle(), 1, &fence->vkHandle());
    VkAssert(result);

    transferCmdPool->ResetCmdBuffer(0);

    constexpr static VkCommandBufferBeginInfo begin_info
    {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        nullptr,
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        nullptr
    };

    result = vkBeginCommandBuffer(pool[0], &begin_info);
    VkAssert(result);
    cmdBufferDirty = false;

    if constexpr (VTF_VALIDATION_ENABLED && VTF_USE_DEBUG_INFO)
    {
        device->DebugUtilsHandler().vkCmdBeginDebugUtilsLabel(transferCmdPool->GetCmdBuffer(0), &queue_debug_label);
    }

    if (uploadBuffers.empty())
    {
        return;
    }

    for (auto& buff : uploadBuffers)
    {
        vmaDestroyBuffer(allocatorHandle, buff->Buffer, buff->Allocation);
        buff.reset();
    }

    uploadBuffers.clear();
    uploadBuffers.reserve(MAX_QUEUED_UPLOAD_BUFFERS);

    while (uploadPools.size() > 1u)
    {
        vmaDestroyPool(allocator, uploadPools.back());
        uploadPools.pop_back();

        // Shrink back down one size
        auto new_pool_size_iter = std::upper_bound(std::begin(stagingPoolSizeRanges), std::end(stagingPoolSizeRanges), lastPoolSize);
        if (new_pool_size_iter != std::end(stagingPoolSizeRanges))
        {
            lastPoolSize = *new_pool_size_iter;
        }
    }

}

void ResourceTransferSystem::EnqueueTransfer(TransferPayloadType&& payload)
{
    transferQueue.push(std::move(payload));
}

void ResourceTransferSystem::processMessages()
{
    using clock = std::chrono::high_resolution_clock;
    const clock::time_point start = clock::now();
    // 2ms chosen as it's about half of one frame at 240hz, which is small but still enough to do work CPU-side on this thread
    // Rest of the 2ms will be used to submit commands and wait for them to complete
    const std::chrono::milliseconds timeout = std::chrono::milliseconds(2);

    // We will process messages until we've exceeded our time limit, or the queue is empty
    while (!transferQueue.empty())
    {
        // Process next message
        TransferPayloadType&& payload = transferQueue.pop();
        std::visit([this](auto&& msg) { processMessage(std::forward<decltype(msg)>(msg)); }, std::move(payload));

        // Check if we've exceeded time limit
        const auto now = clock::now();
        if ((now - start) > timeout)
        {
            break;
        }
    }

    // Message queue empty or time limit reached, now we're going to submit the commands. We don't have a time limit on this step,
    // as if there are any commands to submit we want to get them at least in-flight on the GPU before we take another run at this


    // Commands submitted, now we wait for one at front of queue to complete for no more than 1ms
    // Using semaphore value, which also means that the value should be visible to the client. Once it's complete,
    // we set that reply's status to be complete!
}

void ResourceTransferSystem::processSetBufferDataMessage(TransferSystemSetBufferDataMessage&& message)
{
    const VkBufferCreateInfo& buffer_create_info = message.bufferInfo.createInfo;
    const VkBuffer buffer_handle = message.bufferInfo.bufferHandle;

    TransferCommand transfer_command(device, std::move(message.data.DataVector), message.reply);

    UploadBuffer* upload_buffer = transfer_system.CreateUploadBuffer(buffer_create_info.size);

    // note that this is copying to an API managed staging buffer, not just another raw data buffer like our data container. will only be briefly duplicated
    InternalResourceDataContainer::BufferDataVector dataVector = std::get<InternalResourceDataContainer::BufferDataVector>(message.data.DataVector);
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


void ResourceTransferSystem::processSetImageDataMessage(TransferSystemSetImageDataMessage&& message)
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

    VmaAllocation& alloc = resourceRegistry.get<VmaAllocation>(new_entity);
    UploadBuffer* upload_buffer = transferSystem.CreateUploadBuffer(alloc->GetSize());

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

    auto cmd = transferSystem.TransferCmdBuffer();
    
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

void ResourceTransferSystem::processFillBufferMessage(TransferSystemFillBufferMessage&& message)
{

}

void ResourceTransferSystem::processCopyBufferToBufferMessage(TransferSystemCopyBufferToBufferMessage&& message)
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

void ResourceTransferSystem::processCopyImageToImageMessage(TransferSystemCopyImageToImageMessage&& message)
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

void ResourceTransferSystem::processCopyImageToBufferMessage(TransferSystemCopyImageToBufferMessage&& message)
{

}

void ResourceTransferSystem::processCopyBufferToImageMessage(TransferSystemCopyBufferToImageMessage&& message)
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

VmaPool ResourceTransferSystem::createPool()
{

    uint32_t memory_type_index{ 0u };
    VkResult create_result = vmaFindMemoryTypeIndexForBufferInfo(
        allocatorHandle,
        &k_defaultStagingBufferCreateInfo,
        &k_defaultAllocationCreateInfo,
        &memory_type_index);
    VkAssert(create_result);

    const VmaPoolCreateInfo pool_info
    {
        memory_type_index,
        VMA_POOL_CREATE_LINEAR_ALGORITHM_BIT,
        lastPoolSize, // block size: 256mb of staging memory. if we use more than this, we have a problem
        0u, // min block count
        0u, // max block count
        0u
    };

    VmaPool result;
    create_result = vmaCreatePool(allocatorHandle, &pool_info, &result);
    VkAssert(create_result);

    return result;
}
