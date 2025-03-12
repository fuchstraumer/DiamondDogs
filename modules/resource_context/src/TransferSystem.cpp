#include "TransferSystem.hpp"
#include "ResourceContext.hpp"
#include "CommandPool.hpp"
#include "LogicalDevice.hpp"
#include "PhysicalDevice.hpp"
#include "Instance.hpp"
#include "Fence.hpp"
#include "vkAssert.hpp"
#include "UploadBuffer.hpp"
#include "../../rendering_context/include/RenderingContext.hpp"
#include "VkDebugUtils.hpp"

#include <array>
#include <mutex>
#include <unordered_map>

#define THSVS_SIMPLER_VULKAN_SYNCHRONIZATION_IMPLEMENTATION
#include <thsvs_simpler_vulkan_synchronization.h>

constexpr static size_t MAX_QUEUED_UPLOAD_BUFFERS = 128u;
constexpr static size_t k_defaultNumCommandBuffersPerPool = 8u;

// Literally the only time we will ever take a mutex is during init, and very briefly!
std::mutex transferSystemInitMutex;
std::unordered_map<ResourceTransferSystem*, uint32_t> transferSystemToQueueIndexMap;

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
        VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        0
    };
    VkCommandPoolCreateInfo result = pool_info;
    result.queueFamilyIndex = device->QueueFamilyIndices().Transfer;
    return result;
}

void vmaAllocateDeviceMemoryCallback(VmaAllocator allocator, uint32_t memoryType, VkDeviceMemory memory, VkDeviceSize size, void* user_data)
{

}

void vmaFreeDeviceMemoryCallback(VmaAllocator allocator, uint32_t memoryType, VkDeviceMemory memory, VkDeviceSize size, void* user_data)
{

}

struct DeviceMemoryCallbackUserData
{
    ResourceTransferSystem* transferSystem;
    vpr::Device* device;
};

const static VmaDeviceMemoryCallbacks k_deviceMemoryCallbacks =
{
    vmaAllocateDeviceMemoryCallback,
    vmaFreeDeviceMemoryCallback,
    nullptr, // will set this when initializing the allocator
};

namespace
{
    std::vector<ThsvsAccessType> thsvsAccessTypesFromBufferUsage(VkBufferUsageFlags _flags);
    std::vector<ThsvsAccessType> thsvsAccessTypesFromImageUsage(VkImageUsageFlags _flags);
    VkAccessFlags accessFlagsFromBufferUsage(VkBufferUsageFlags usage_flags);
    VkAccessFlags accessFlagsFromImageUsage(const VkImageUsageFlags usage_flags);
    VkImageLayout imageLayoutFromUsage(const VkImageUsageFlags usage_flags);
    VkImageAspectFlags imageAspectFlagsFromUsage(const VkImageUsageFlags usage_flags);
    VkPipelineStageFlags pipelineStageFlagsFromBufferUsage(const VkBufferUsageFlags usage_flags);
}

ResourceTransferSystem::TransferCommand::TransferCommand(
    const vpr::Device* _device,
    VmaAllocator _allocator,
    std::shared_ptr<ResourceTransferReply>&& _reply) :
    device(_device),
    reply(std::move(_reply)),
    allocatorHandle(_allocator)
{
    createCommandPool();
    uploadBuffer = std::make_unique<UploadBuffer>(device, allocatorHandle);
}

ResourceTransferSystem::TransferCommand::TransferCommand(
    const vpr::Device* _device,
    std::shared_ptr<ResourceTransferReply>&& _reply) :
    reply(std::move(_reply)),
    allocatorHandle(VK_NULL_HANDLE),
    device(_device), // still need this to create command pool
    uploadBuffer(nullptr)
{
    createCommandPool();
}

ResourceTransferSystem::TransferCommand::TransferCommand(TransferCommand&& other) noexcept :
    device(other.device),
    reply(std::move(other.reply)),
    allocatorHandle(other.allocatorHandle),
    commandPool(std::move(other.commandPool)),
    uploadBuffer(std::move(other.uploadBuffer))
{}

ResourceTransferSystem::TransferCommand& ResourceTransferSystem::TransferCommand::operator=(TransferCommand&& other) noexcept
{
    if (this != &other)
    {
        device = other.device;
        reply = std::move(other.reply);
        allocatorHandle = other.allocatorHandle;
        commandPool = std::move(other.commandPool);
        uploadBuffer = std::move(other.uploadBuffer);
    }
    return *this;
}

ResourceTransferSystem::TransferCommand::~TransferCommand()
{
    // really shoudn't get to this point but .... just in case
    // (ideally reply is reset after submission success)
    if (reply && !reply->IsCompleted())
    {
        constexpr uint64_t k_timeoutNs = 1000000000u;
        MessageReply::Status waitForSubmitStatus = reply->WaitForCompletion(k_timeoutNs);
        if (waitForSubmitStatus != MessageReply::Status::Completed)
        {
            throw std::runtime_error("Transfer command failed to complete, and we were unable to wait for it to complete!");
        }
    }
}

VkCommandBuffer ResourceTransferSystem::TransferCommand::CmdBuffer() const
{
    return commandPool->GetCmdBuffer(0);
}

void ResourceTransferSystem::TransferCommand::EndRecording()
{
    if constexpr (RENDERING_CONTEXT_VALIDATION_ENABLED && RENDERING_CONTEXT_USE_DEBUG_INFO)
    {
        device->DebugUtilsHandler().vkCmdEndDebugUtilsLabel(commandPool->GetCmdBuffer(0));
    }
    VkResult result = vkEndCommandBuffer(commandPool->GetCmdBuffer(0));
    VkAssert(result);
}

MessageReply::Status ResourceTransferSystem::TransferCommand::WaitForCompletion(uint64_t timeoutNs)
{
    MessageReply::Status status = reply->WaitForCompletion(timeoutNs);

    if (status == MessageReply::Status::Timeout)
    {
        return status;
    }
    else if (status != MessageReply::Status::Completed)
    {
        throw std::runtime_error("Transfer command failed to complete, and we were unable to wait for it to complete!");
    }

    reply.reset();
    uploadBuffer.reset();
    vkResetCommandBuffer(commandPool->GetCmdBuffer(0), 0);
    vkResetCommandPool(device->vkHandle(), commandPool->vkHandle(), 0);
    commandPool.reset();
    return status;
}

VkSemaphore ResourceTransferSystem::TransferCommand::Semaphore() const
{
    return (VkSemaphore)reply->SemaphoreHandle();
}

UploadBuffer* ResourceTransferSystem::TransferCommand::GetUploadBuffer() noexcept
{
    return uploadBuffer.get();
}

void ResourceTransferSystem::TransferCommand::createCommandPool()
{
    VkCommandPoolCreateInfo pool_info = getCreateInfo(device);
    commandPool = std::make_unique<vpr::CommandPool>(device->vkHandle(), pool_info);
    commandPool->AllocateCmdBuffers(1, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    
    if constexpr (RENDERING_CONTEXT_VALIDATION_ENABLED && RENDERING_CONTEXT_USE_DEBUG_INFO)
    {
        device->DebugUtilsHandler().vkCmdBeginDebugUtilsLabel(commandPool->GetCmdBuffer(0), &queue_debug_label);
    }

    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VkResult result = vkBeginCommandBuffer(commandPool->GetCmdBuffer(0), &begin_info);
    VkAssert(result);

}

ResourceTransferSystem::ResourceTransferSystem() : device(nullptr) {}

ResourceTransferSystem::~ResourceTransferSystem()
{
    destroy();
}

void ResourceTransferSystem::Initialize(const vpr::Device* dvc)
{

    if (initialized)
    {
        return;
    }

    device = dvc;
    
    const auto& applicationInfo = device->ParentInstance()->ApplicationInfo();
    // we run this on a single thread, so we don't need to have the library sync for us!
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

    initialized = true;
    shouldExitWorker.store(false);
    workerThread = std::thread(&ResourceTransferSystem::workerThreadJob, this);

}

void ResourceTransferSystem::SetExitWorker(bool value)
{
    shouldExitWorker.store(value, std::memory_order_seq_cst);
}

void ResourceTransferSystem::StartWorker()
{
    shouldExitWorker.store(false);
    workerThread = std::thread(&ResourceTransferSystem::workerThreadJob, this);
}

void ResourceTransferSystem::StopWorker()
{
    shouldExitWorker.store(true);
    workerThread.join();
    ForceCompleteTransfers();
}

void ResourceTransferSystem::destroy()
{
    ForceCompleteTransfers();
    vmaDestroyAllocator(allocatorHandle);
    device = nullptr;
}

void ResourceTransferSystem::ForceCompleteTransfers()
{

    if (!shouldExitWorker.load())
    {
        workerThread.join();
        shouldExitWorker.store(true);
    }

    // wait for all inflight command batches to complete using a fence
    for (auto& batch : inflightCommandBatches)
    {
        VkFence batch_fence = batch.fence->vkHandle();
        VkResult result = vkWaitForFences(device->vkHandle(), 1, &batch_fence, VK_TRUE, UINT64_MAX);
        VkAssert(result);
    }

    inflightCommandBatches.clear();

    // existing inflight commands completed, if there are any commands in queue to be submitted submit them as well and wait
    if (!commands.empty())
    {
        submitTransferCommands();
        for (auto& batch : inflightCommandBatches)
        {
            VkFence batch_fence = batch.fence->vkHandle();
            VkResult result = vkWaitForFences(device->vkHandle(), 1, &batch_fence, VK_TRUE, UINT64_MAX);
            VkAssert(result);
        }
    }

    inflightCommandBatches.clear();

    StartWorker();
}

void ResourceTransferSystem::EnqueueTransfer(TransferPayloadType&& payload)
{
    messageQueue.push(std::move(payload));
}

void ResourceTransferSystem::workerThreadJob()
{
    // 2ms chosen as it's about half of one frame at 240hz, which is small but still enough to do work CPU-side on this thread
    // Rest of the 2ms will be used to submit commands and wait for them to complete
    static constexpr std::chrono::milliseconds message_processing_timeout = std::chrono::milliseconds(2);
    // 1ms chosen to give us less time than the message processing, but still enough for GPU to do work
    // PCI bus goes WHIRRRRRRRRRRRRRRRRR
    static constexpr std::chrono::milliseconds command_submission_timeout = std::chrono::milliseconds(1);
    
    // Create a backoff sleeper with custom parameters for transfer system
    // Use a smaller min sleep time (1ms) since transfers are often time-sensitive
    foundation::ExponentialBackoffSleeper sleeper(
        std::chrono::milliseconds(1),  // min sleep
        std::chrono::milliseconds(30), // max sleep
        0.2f,                          // less jitter (20%)
        1.5f                           // gentler backoff multiplier
    );

    while (!shouldExitWorker.load())
    {
        bool didWork = false;
        
        // Process messages with timeout
        if (!messageQueue.empty())
        {
            processMessages(message_processing_timeout);
            didWork = true;
        }
        
        // Submit any pending commands
        if (!commands.empty())
        {
            submitTransferCommands();
            didWork = true;
        }
        
        // Wait for in-flight commands to complete
        if (!inflightCommandBatches.empty())
        {
            waitForCommandsToComplete(command_submission_timeout);
            didWork = true;
        }
        
        // Adjust sleep behavior based on whether we did any work
        if (didWork)
        {
            sleeper.reset();
        }
        else
        {
            sleeper.backoff();
        }
        
        // Sleep for the calculated duration
        sleeper.sleep();
    }
}

void ResourceTransferSystem::processMessages(std::chrono::milliseconds timeout)
{
    using clock = std::chrono::high_resolution_clock;
    const clock::time_point start = clock::now();
    
    
    // We will process messages until we've exceeded our time limit, or the queue is empty
    while (!messageQueue.empty())
    {
        // Process next message
        std::visit([this](auto&& msg) { processMessage(std::forward<decltype(msg)>(msg)); }, std::move(messageQueue.pop()));

        // Check if we've exceeded time limit
        const auto now = clock::now();
        if ((now - start) > timeout)
        {
            break;
        }
    }

}

void ResourceTransferSystem::submitTransferCommands()
{
    std::vector<VkCommandBuffer> cmd_buffers;
    std::vector<VkSemaphore> signal_semaphores;
    const std::vector<uint64_t> signal_values(commands.size(), k_TransferCompleteSemaphoreValue);

    for (auto& command : commands)
    {
        cmd_buffers.emplace_back(command.CmdBuffer());
        signal_semaphores.emplace_back(command.Semaphore());
    }

    VkTimelineSemaphoreSubmitInfo timeline_info = {};
    std::memset(&timeline_info, 0, sizeof(VkTimelineSemaphoreSubmitInfo));
    timeline_info.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    timeline_info.pSignalSemaphoreValues = signal_values.data();
    timeline_info.signalSemaphoreValueCount = static_cast<uint32_t>(signal_values.size());

    VkSubmitInfo submit_info = {};
    std::memset(&submit_info, 0, sizeof(VkSubmitInfo));
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.pNext = &timeline_info;
    submit_info.commandBufferCount = static_cast<uint32_t>(cmd_buffers.size());
    submit_info.pCommandBuffers = cmd_buffers.data();
    submit_info.signalSemaphoreCount = static_cast<uint32_t>(signal_semaphores.size());
    submit_info.pSignalSemaphores = signal_semaphores.data();

    VkResult result = vkQueueSubmit(device->TransferQueue(transferQueueIndex), 1, &submit_info, VK_NULL_HANDLE);
    VkAssert(result);

    inflightCommandBatches.emplace_back(std::move(commands), std::make_unique<vpr::Fence>(device->vkHandle(), 0));
}

void ResourceTransferSystem::waitForCommandsToComplete(std::chrono::milliseconds timeout)
{
    using clock = std::chrono::high_resolution_clock;
    const clock::time_point wait_start = clock::now();
    const clock::time_point wait_end = wait_start + timeout;
    for (auto batch_iter = inflightCommandBatches.begin(); batch_iter != inflightCommandBatches.end();)
    {
        const clock::time_point now_outer = clock::now();
        if (now_outer >= wait_end)  
        {
            break;
        }

        for (auto iter = batch_iter->commands.begin(); iter != batch_iter->commands.end();)
        {
            const clock::time_point now_inner = clock::now();
            if (now_inner >= wait_end)
            {
                break;
            }

            // Calculate remaining time in nanoseconds
            const uint64_t remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(wait_end - now_inner).count();
            
            MessageReply::Status status = iter->WaitForCompletion(remaining);
            if (status == MessageReply::Status::Completed)
            {
                // I wonder how much time we burn in erasing the command and running the destructor?
                // Probably doesn't matter since commands are still in flight, but still
                iter = batch_iter->commands.erase(iter);
            }
            else if (status == MessageReply::Status::Timeout)
            {
                // we'll get em next time
                // (maybe worth checking to see if we've hit a TDR?)
                break;
            }
            else
            {
                // I actually don't think we can hit this? 
                ++iter;
            }
        }

        if (batch_iter->commands.empty())
        {
            batch_iter = inflightCommandBatches.erase(batch_iter);
        }
        else
        {
            ++batch_iter;
        }
    }
}

void ResourceTransferSystem::processSetBufferDataMessage(TransferSystemSetBufferDataMessage&& message)
{
    const VkBufferCreateInfo& buffer_create_info = message.bufferInfo.createInfo;
    const VkBuffer buffer_handle = message.bufferInfo.bufferHandle;

    TransferCommand transfer_command(device, allocatorHandle, std::move(message.reply));

    // note that this is copying to an API managed staging buffer, not just another raw data buffer like our data container. will only be briefly duplicated
    InternalResourceDataContainer::BufferDataVector& dataVector = std::get<InternalResourceDataContainer::BufferDataVector>(message.data.DataVector);
    
    UploadBuffer* upload_buffer = transfer_command.GetUploadBuffer();
    std::vector<VkBufferCopy> buffer_copies = upload_buffer->SetData(dataVector);

    VkCommandBuffer cmd = transfer_command.CmdBuffer();
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
    transfer_command.EndRecording();
    dataVector.clear();
}

void ResourceTransferSystem::processSetImageDataMessage(TransferSystemSetImageDataMessage&& message)
{
    constexpr static ThsvsAccessType transfer_access_types[1]
    {
        THSVS_ACCESS_TRANSFER_WRITE
    };

    const VkImageCreateInfo& image_info = message.imageInfo.createInfo;
    const VkImage image_handle = message.imageInfo.imageHandle;

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

    TransferCommand transfer_command(device, allocatorHandle, std::move(message.reply));
    InternalResourceDataContainer::ImageDataVector& imageDataVector = std::get<InternalResourceDataContainer::ImageDataVector>(message.data.DataVector);
    
    UploadBuffer* upload_buffer = transfer_command.GetUploadBuffer();
    std::vector<VkBufferImageCopy> buffer_image_copies = upload_buffer->SetData(imageDataVector, image_info.arrayLayers);

    VkCommandBuffer cmd = transfer_command.CmdBuffer();
   
    thsvsCmdPipelineBarrier(cmd, nullptr, 0u, nullptr, 1u, &pre_transfer_barrier);
    vkCmdCopyBufferToImage(
        cmd,
        upload_buffer->Buffer,
        image_handle,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        static_cast<uint32_t>(buffer_image_copies.size()),
        buffer_image_copies.data());
    thsvsCmdPipelineBarrier(cmd, nullptr, 0u, nullptr, 1u, &post_transfer_barrier);

    transfer_command.EndRecording();

    imageDataVector.clear();
}

void ResourceTransferSystem::processFillBufferMessage(TransferSystemFillBufferMessage&& message)
{
    TransferSystemReqBufferInfo buffer_info = message.bufferInfo;
    VkBuffer buffer_handle = buffer_info.bufferHandle;
    VkBufferCreateInfo buffer_create_info = buffer_info.createInfo;

    // Fill command is considered a transfer command, if this bit isn't set buffer can't be used as a fill target
    if (!(buffer_create_info.usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT))
    {
        message.reply->SetStatus(MessageReply::Status::Failed);
        return;
    }

    TransferCommand transfer_command(device, std::move(message.reply));
    VkCommandBuffer cmd = transfer_command.CmdBuffer();

    constexpr static ThsvsAccessType transfer_access_types[1]
    {
        THSVS_ACCESS_TRANSFER_WRITE
    };

    const std::vector<ThsvsAccessType> possible_accesses = thsvsAccessTypesFromBufferUsage(buffer_create_info.usage);

    const ThsvsGlobalBarrier pre_fill_barrier
    {
        static_cast<uint32_t>(possible_accesses.size()),
        possible_accesses.data(),
        1u,
        transfer_access_types
    };

    const ThsvsGlobalBarrier post_fill_barrier
    {
        1u,
        transfer_access_types,
        static_cast<uint32_t>(possible_accesses.size()),
        possible_accesses.data()
    };
    
    thsvsCmdPipelineBarrier(cmd, &pre_fill_barrier, 0u, nullptr, 0u, nullptr);
    vkCmdFillBuffer(cmd, buffer_handle, message.offset, message.size, message.value);
    thsvsCmdPipelineBarrier(cmd, &post_fill_barrier, 0u, nullptr, 0u, nullptr);

    transfer_command.EndRecording();
}

void ResourceTransferSystem::processCopyBufferToBufferMessage(TransferSystemCopyBufferToBufferMessage&& message)
{
    TransferSystemReqBufferInfo src_buffer_info = message.srcBufferInfo;
    VkBuffer src_handle = src_buffer_info.bufferHandle;
    VkBufferCreateInfo src_info = src_buffer_info.createInfo;

    TransferSystemReqBufferInfo dst_buffer_info = message.dstBufferInfo;
    VkBuffer dst_handle = dst_buffer_info.bufferHandle;
    VkBufferCreateInfo dst_info = dst_buffer_info.createInfo;

    assert(src_info.size <= dst_info.size);
    const VkBufferCopy copy
    {
        0u,
        0u,
        src_info.size
    };

    const VkBufferMemoryBarrier pre_transfer_barriers[2]
    {
        VkBufferMemoryBarrier
        {
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            nullptr,
            accessFlagsFromBufferUsage(src_info.usage),
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            src_handle,
            0u,
            src_info.size
        },
        VkBufferMemoryBarrier
        {
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            nullptr,
            accessFlagsFromBufferUsage(dst_info.usage),
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            dst_handle,
            0u,
            dst_info.size
        }
    };

    const VkBufferMemoryBarrier post_transfer_barriers[2]
    {
        VkBufferMemoryBarrier
        {
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            nullptr,
            VK_ACCESS_TRANSFER_READ_BIT,
            accessFlagsFromBufferUsage(src_info.usage),
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            src_handle,
            0u,
            src_info.size
        },
        VkBufferMemoryBarrier
        {
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            nullptr,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            accessFlagsFromBufferUsage(dst_info.usage),
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            dst_handle,
            0u,
            dst_info.size
        }
    };

    
    TransferCommand transfer_command(device, std::move(message.reply));
    VkCommandBuffer cmd = transfer_command.CmdBuffer();

    // use src stage for pre transfer barrier since it might be used by a shader or other work
    VkPipelineStageFlags src_stage = pipelineStageFlagsFromBufferUsage(src_info.usage);
    vkCmdPipelineBarrier(cmd, src_stage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, nullptr, 2u, pre_transfer_barriers, 0u, nullptr);
    vkCmdCopyBuffer(cmd, src_handle, dst_handle, 1, &copy);
    // and dst stage for post transfer barrier since that's what we care about after the transfer
    VkPipelineStageFlags dst_stage = pipelineStageFlagsFromBufferUsage(dst_info.usage);
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, dst_stage, 0u, 0u, nullptr, 2u, post_transfer_barriers, 0u, nullptr);

    transfer_command.EndRecording();
}

void ResourceTransferSystem::processCopyImageToImageMessage(TransferSystemCopyImageToImageMessage&& message)
{
    TransferSystemReqImageInfo src_image_info = message.srcImageInfo;
    VkImage src_handle = src_image_info.imageHandle;
    VkImageCreateInfo src_info = src_image_info.createInfo;
    const VkImageAspectFlags src_aspect_flags = imageAspectFlagsFromUsage(src_info.usage);
    const VkImageSubresourceRange src_range = VkImageSubresourceRange
    {
        src_aspect_flags, 0u, src_info.mipLevels, 0u, src_info.arrayLayers
    };

    TransferSystemReqImageInfo dst_image_info = message.dstImageInfo;
    VkImage dst_handle = dst_image_info.imageHandle;
    VkImageCreateInfo dst_info = dst_image_info.createInfo;
    const VkImageAspectFlags dst_aspect_flags = imageAspectFlagsFromUsage(dst_info.usage);
    const VkImageSubresourceRange dst_range = VkImageSubresourceRange
    {
        dst_aspect_flags, 0u, dst_info.mipLevels, 0u, dst_info.arrayLayers
    };

    if (src_aspect_flags != dst_aspect_flags ||
        src_info.arrayLayers != dst_info.arrayLayers ||
        src_info.extent.width != dst_info.extent.width ||
        src_info.extent.height != dst_info.extent.height ||
        src_info.extent.depth != dst_info.extent.depth ||
        src_info.mipLevels != dst_info.mipLevels)
    {
        // if these traits don't match, we can't copy (for now)
        // may need to investigate and return to this later, but most of these traits
        // shouldn't be a problem. may need to specify copy region/layer somehow
        message.reply->SetStatus(MessageReply::Status::Failed);
        return;
    }

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
            src_handle,
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
            dst_handle,
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
            src_handle,
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
            dst_handle,
            dst_range
        }
    };

    const VkImageLayout src_layout = imageLayoutFromUsage(src_info.usage);
    const VkImageLayout dst_layout = imageLayoutFromUsage(dst_info.usage);

    TransferCommand transfer_command(device, std::move(message.reply));

    std::vector<VkImageCopy> image_copies(src_info.arrayLayers);
    // Resize to hold copies for all mips and array layers
    image_copies.resize(src_info.mipLevels * src_info.arrayLayers);
    size_t copy_idx = 0;
    
    for (uint32_t mip = 0; mip < src_info.mipLevels; ++mip)
    {
        // Calculate extent for this mip level
        VkExtent3D mip_extent
        {
            std::max(src_info.extent.width >> mip, 1u),
            std::max(src_info.extent.height >> mip, 1u),
            std::max(src_info.extent.depth >> mip, 1u)
        };

        for (uint32_t layer = 0; layer < src_info.arrayLayers; ++layer)
        {
            VkImageSubresourceLayers src_subresource
            {
                src_aspect_flags,
                mip,
                layer,
                1u  // Copy one layer at a time
            };

            VkImageSubresourceLayers dst_subresource
            {
                dst_aspect_flags,
                mip,
                layer,
                1u
            };

            image_copies[copy_idx++] = VkImageCopy
            {
                src_subresource,
                VkOffset3D { 0u, 0u, 0u },
                dst_subresource, 
                VkOffset3D { 0u, 0u, 0u },
                mip_extent
            };
        }
    }

    VkCommandBuffer cmd = transfer_command.CmdBuffer();
    thsvsCmdPipelineBarrier(cmd, nullptr, 0u, nullptr, 2u, pre_transfer_barriers);
    vkCmdCopyImage(
        cmd,
        src_handle, src_layout,
        dst_handle, dst_layout,
        static_cast<uint32_t>(image_copies.size()), image_copies.data());
    thsvsCmdPipelineBarrier(cmd, nullptr, 0u, nullptr, 2u, post_transfer_barriers);

    transfer_command.EndRecording();
}

void ResourceTransferSystem::processCopyImageToBufferMessage(TransferSystemCopyImageToBufferMessage&& message)
{
    // Not implemented and I really don't want to do it right now lol
    message.reply->SetStatus(MessageReply::Status::Failed);
}

void ResourceTransferSystem::processCopyBufferToImageMessage(TransferSystemCopyBufferToImageMessage&& message)
{
    TransferSystemReqBufferInfo src_buffer_info = message.bufferInfo;
    VkBuffer src_handle = src_buffer_info.bufferHandle;
    VkBufferCreateInfo src_info = src_buffer_info.createInfo;

    TransferSystemReqImageInfo dst_image_info = message.imageInfo;
    VkImage dst_handle = dst_image_info.imageHandle;
    VkImageCreateInfo dst_info = dst_image_info.createInfo;
    const VkImageAspectFlags dst_aspect_flags = imageAspectFlagsFromUsage(dst_info.usage);
    const VkImageSubresourceRange dst_range = VkImageSubresourceRange
    {
        dst_aspect_flags, 0u, dst_info.mipLevels, 0u, dst_info.arrayLayers
    };

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
            src_handle,
            0u,
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
            dst_handle,
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
            src_handle,
            0u,
            src_info.size
        }
    };

    const ThsvsImageBarrier post_transfer_image_barrier[1]
    {
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
            dst_handle,
            dst_range
        }
    };

    const VkImageLayout dst_layout = imageLayoutFromUsage(dst_info.usage);
    TransferCommand transfer_command(device, std::move(message.reply));

    VkCommandBuffer cmd = transfer_command.CmdBuffer();

    // I have no idea if this is going to work. We might need to pass in more metadata from the message?
    VkBufferImageCopy copy_params[1]
    {
        VkBufferImageCopy
        {
            0u,
            0u,
            0u,
            VkImageSubresourceLayers
            {
                dst_aspect_flags,
                0u,
                0u,
                1u
            },
            VkOffset3D { 0u, 0u, 0u },
            dst_info.extent
        }
    };

    thsvsCmdPipelineBarrier(cmd, nullptr, 1u, pre_transfer_buffer_barrier, 1u, pre_transfer_image_barrier);
    vkCmdCopyBufferToImage(cmd, src_handle, dst_handle, dst_layout, 1u, copy_params);
    thsvsCmdPipelineBarrier(cmd, nullptr, 1u, post_transfer_buffer_barrier, 1u, post_transfer_image_barrier);

    transfer_command.EndRecording();
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

    VkImageAspectFlags imageAspectFlagsFromUsage(const VkImageUsageFlags usage_flags)
    {
        if (usage_flags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
        {
            return VK_IMAGE_ASPECT_COLOR_BIT;
        }
        else if (usage_flags & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
        {
            return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        }
        else
        {
            return VK_IMAGE_ASPECT_COLOR_BIT;
        }
    }

    VkPipelineStageFlags pipelineStageFlagsFromBufferUsage(const VkBufferUsageFlags usage_flags)
    {
        // any of these flags mean the buffer could be used by a shader, potentially anywhere in the pipeline
        static const VkBufferUsageFlags shader_resource_flags = 
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT |
            VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
            VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
            VK_BUFFER_USAGE_CONDITIONAL_RENDERING_BIT_EXT |
            VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT |
            VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT |
            VK_BUFFER_USAGE_PUSH_DESCRIPTORS_DESCRIPTOR_BUFFER_BIT_EXT;
        // any of these flags mean the buffer is a vertex or index buffer, so needed by start of vertex input
        static const VkBufferUsageFlags vertex_buffer_flags =
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        // yay, just simple ol' transfer flags!
        static const VkBufferUsageFlags transfer_flags =
            VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        // here be dragons
        static const VkBufferUsageFlags raytracing_flags =
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR;

        // proceed down if statement from most specialized, then to more general flags proceeding from top of pipe down
        if (usage_flags & VK_BUFFER_USAGE_CONDITIONAL_RENDERING_BIT_EXT)
        {
            return VK_PIPELINE_STAGE_CONDITIONAL_RENDERING_BIT_EXT;
        }
        else if (usage_flags & raytracing_flags)
        {
            return VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        }
        else if (usage_flags & shader_resource_flags)
        {
            return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        }
        else if (usage_flags & vertex_buffer_flags)
        {
            return VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
        }
        else if (usage_flags & transfer_flags)
        {
            return VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else
        {
            return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        }
    }

    VkPipelineStageFlags pipelineStageFlagsFromImageUsage(const VkImageUsageFlags usage_flags)
    {
        return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }
}
