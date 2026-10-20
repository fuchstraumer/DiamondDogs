#include "TransferSystem.hpp"
#include "DebugUtilFns.hpp"
#include "ImageDataFormatsVulkan.hpp"
#include "ResourceContext.hpp"
#include "CommandPool.hpp"
#include "Device.hpp"
#include "Fence.hpp"
#include "ResourceMessageReply.hpp"
#include "ResourceMessageTypesInternal.hpp"
#include "ResourceTypes.hpp"
#include "RhiResult.hpp"
#include "RhiSystem.hpp"
#include "UploadBuffer.hpp"
#include "VulkanConversions.hpp"
#include "threading/ExponentialBackoffSleeper.hpp"
#include "vulkan/vulkan_core.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

constexpr static VkDebugUtilsLabelEXT queue_debug_label
{
    VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
    nullptr,
    "TransferQueue",
    { 244.0f / 255.0f, 158.0f / 255.0f, 66.0f / 255.0f, 1.0f }
};

namespace
{
    void RecordTransferOrderingBarrier(VkCommandBuffer cmd) noexcept;
    void RecordImageLayoutTransition(
        VkCommandBuffer cmd,
        VkImage image,
        const VkImageSubresourceRange& range,
        VkImageLayout old_layout,
        VkImageLayout new_layout,
        VkPipelineStageFlags2 src_stage,
        VkAccessFlags2 src_access,
        VkPipelineStageFlags2 dst_stage,
        VkAccessFlags2 dst_access) noexcept;
    [[nodiscard]] VkImageAspectFlags AspectMaskForImage(const ImageCreateInfo& create_info) noexcept;
    [[nodiscard]] VkImageSubresourceRange WholeImageRange(const ImageCreateInfo& create_info) noexcept;
    [[nodiscard]] VkExtent3D ImageExtent(const ImageCreateInfo& create_info) noexcept;
}

ResourceTransferSystem::TransferCommand::TransferCommand(
    const rhi::Device* _device,
    uint32_t queue_family_index,
    VmaAllocator _allocator,
    std::shared_ptr<ResourceTransferReply>&& _reply) :
    device(_device),
    allocatorHandle(_allocator),
    reply(std::move(_reply))
{
    createCommandPool(queue_family_index);
    uploadBuffer = std::make_unique<UploadBuffer>(allocatorHandle);
}

ResourceTransferSystem::TransferCommand::TransferCommand(
    const rhi::Device* _device,
    uint32_t queue_family_index,
    std::shared_ptr<ResourceTransferReply>&& _reply) :
    device(_device), // still need this to create command pool
    allocatorHandle(VK_NULL_HANDLE),
    reply(std::move(_reply)),
    uploadBuffer(nullptr)
{
    createCommandPool(queue_family_index);
}

ResourceTransferSystem::TransferCommand::TransferCommand(TransferCommand&& other) noexcept :
    device(other.device),
    allocatorHandle(other.allocatorHandle),
    reply(std::move(other.reply)),
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
        reply->WaitForCompletion(std::numeric_limits<uint64_t>::max());
    }
}

VkCommandBuffer ResourceTransferSystem::TransferCommand::CmdBuffer() const noexcept
{
    return commandPool->GetCommandBuffer(0).As<VkCommandBuffer>();
}

void ResourceTransferSystem::TransferCommand::EndRecording()
{
    const rhi::VkDebugUtilsFunctions& debugFns = device->GetDebugUtilFns();
    if (debugFns.vkCmdEndDebugUtilsLabel != nullptr)
    {
        debugFns.vkCmdEndDebugUtilsLabel(CmdBuffer());
    }
    VkResult result = vkEndCommandBuffer(CmdBuffer());
    assert(result == VK_SUCCESS);
}

MessageReply::Status ResourceTransferSystem::TransferCommand::WaitForCompletion(uint64_t timeoutNs)
{
    MessageReply::Status status = reply->WaitForCompletion(timeoutNs);
    if (status == MessageReply::Status::Timeout)
    {
        return status;
    }

    // completed or failed: the GPU is done with the pool and the upload buffer either way
    reply.reset();
    uploadBuffer.reset();
    commandPool.reset();
    return status;
}

VkSemaphore ResourceTransferSystem::TransferCommand::Semaphore() const
{
    return reinterpret_cast<VkSemaphore>(reply->SemaphoreHandle());
}

UploadBuffer* ResourceTransferSystem::TransferCommand::GetUploadBuffer() noexcept
{
    return uploadBuffer.get();
}

void ResourceTransferSystem::TransferCommand::createCommandPool(uint32_t queue_family_index)
{
    commandPool = std::make_unique<rhi::CommandPool>(device->Handle(), rhi::CommandPool::Type::Transfer, queue_family_index);
    rhi::Result allocResult = commandPool->AllocateCommandBuffers(1);
    assert(allocResult.IsSuccess());

    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VkResult result = vkBeginCommandBuffer(CmdBuffer(), &begin_info);
    assert(result == VK_SUCCESS);

    const rhi::VkDebugUtilsFunctions& debugFns = device->GetDebugUtilFns();
    if (debugFns.vkCmdBeginDebugUtilsLabel != nullptr)
    {
        debugFns.vkCmdBeginDebugUtilsLabel(CmdBuffer(), &queue_debug_label);
    }

    // every transfer is ordered after the transfers before it on this queue. submissions on one queue give no memory
    // dependency, so an upload followed by a copy of the same buffer would race without this
    RecordTransferOrderingBarrier(CmdBuffer());
}

ResourceTransferSystem::ResourceTransferSystem() {}

ResourceTransferSystem::~ResourceTransferSystem()
{
    destroy();
}

void ResourceTransferSystem::Initialize(const rhi::Device* _device, VkInstance instance, uint32_t vulkan_api_version)
{
    if (initialized)
    {
        return;
    }

    device = _device;

    // a dedicated transfer queue when the device has one. otherwise the second graphics queue, and the general queue
    // only as the last resort: that queue is shared with the application, which then races our submits
    if (device->GetTransferQueueCount() > 0u)
    {
        transferQueue = device->GetTransferQueue(0).As<VkQueue>();
        transferQueueFamily = device->GetQueueFamilyIndices().Transfer;
    }
    else
    {
        // todo-ship: a device with one queue needs our submits serialized with the application's
        const uint32_t queueIndex = device->GetGraphicsQueueCount() > 1u ? 1u : 0u;
        transferQueue = device->GetGraphicsQueue(queueIndex).As<VkQueue>();
        transferQueueFamily = device->GetQueueFamilyIndices().Graphics;
    }

    // we run this on a single thread, so we don't need to have the library sync for us!
    VmaAllocatorCreateInfo create_info = {};
    create_info.flags = VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT;
    create_info.physicalDevice = device->GetPhysicalDevice().As<VkPhysicalDevice>();
    create_info.device = device->Handle().As<VkDevice>();
    create_info.instance = instance;
    create_info.vulkanApiVersion = vulkan_api_version;

    VkResult result = vmaCreateAllocator(&create_info, &allocatorHandle);
    assert(result == VK_SUCCESS);

    initialized = true;
    StartWorker();
}

void ResourceTransferSystem::StartWorker()
{
    shouldExitWorker.store(false);
    workerThread = std::thread(&ResourceTransferSystem::workerThreadJob, this);
}

void ResourceTransferSystem::StopWorker()
{
    if (workerThread.joinable())
    {
        shouldExitWorker.store(true);
        workerThread.join();
    }
}

void ResourceTransferSystem::destroy()
{
    if (!initialized)
    {
        return;
    }

    StopWorker();
    processRemainingAndWait();
    vmaDestroyAllocator(allocatorHandle);
    allocatorHandle = VK_NULL_HANDLE;
    device = nullptr;
    initialized = false;
}

void ResourceTransferSystem::ForceCompleteTransfers()
{
    // fixed: this joined the worker without telling it to exit (a deadlock), and destroy() called it and so started the
    // worker again on a system that was being destroyed
    const bool restartWorker = workerThread.joinable();
    StopWorker();
    processRemainingAndWait();
    if (restartWorker)
    {
        StartWorker();
    }
}

void ResourceTransferSystem::processRemainingAndWait()
{
    // only called with the worker stopped, so this thread is the queue's only reader
    while (processMessages(std::chrono::milliseconds::max()))
    {
    }

    if (!commands.empty())
    {
        submitTransferCommands();
    }

    for (InflightCommandBatch& batch : inflightCommandBatches)
    {
        batch.fence->Wait();
        for (TransferCommand& command : batch.commands)
        {
            command.WaitForCompletion(std::numeric_limits<uint64_t>::max());
        }
    }
    inflightCommandBatches.clear();
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
        didWork = processMessages(message_processing_timeout);

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
            sleeper.sleepAndBackoff();
        }
    }
}

bool ResourceTransferSystem::processMessages(std::chrono::milliseconds timeout)
{
    using clock = std::chrono::steady_clock;
    const clock::time_point start = clock::now();
    bool processedMessages = false;

    // fixed: dispatch went through explicit specializations of a member template declared in class scope, which only
    // MSVC accepts. an overload set on the process functions does the same job
    auto MessageVisitor =
        [this](auto&& message)
        {
            using T = std::decay_t<decltype(message)>;
            if constexpr (std::is_same_v<T, TransferSystemSetBufferDataMessage>)
            {
                processSetBufferDataMessage(std::move(message));
            }
            else if constexpr (std::is_same_v<T, TransferSystemSetImageDataMessage>)
            {
                processSetImageDataMessage(std::move(message));
            }
            else if constexpr (std::is_same_v<T, TransferSystemFillBufferMessage>)
            {
                processFillBufferMessage(std::move(message));
            }
            else if constexpr (std::is_same_v<T, TransferSystemCopyBufferToBufferMessage>)
            {
                processCopyBufferToBufferMessage(std::move(message));
            }
            else if constexpr (std::is_same_v<T, TransferSystemCopyImageToImageMessage>)
            {
                processCopyImageToImageMessage(std::move(message));
            }
            else if constexpr (std::is_same_v<T, TransferSystemCopyImageToBufferMessage>)
            {
                processCopyImageToBufferMessage(std::move(message));
            }
            else if constexpr (std::is_same_v<T, TransferSystemCopyBufferToImageMessage>)
            {
                processCopyBufferToImageMessage(std::move(message));
            }
        };

    // We will process messages until we've exceeded our time limit, or the queue is empty
    while (true)
    {
        std::optional<TransferPayloadType> message = messageQueue.try_pop();
        if (!message.has_value())
        {
            // No messages to process, break out of the loop and let caller know we didn't do any work
            return processedMessages;
        }

        std::visit(MessageVisitor, std::move(*message));
        processedMessages = true;

        if ((clock::now() - start) > timeout)
        {
            return processedMessages;
        }
    }
}

void ResourceTransferSystem::submitTransferCommands()
{
    std::vector<VkCommandBuffer> cmd_buffers;
    std::vector<VkSemaphore> signal_semaphores;
    cmd_buffers.reserve(commands.size());
    signal_semaphores.reserve(commands.size());
    const std::vector<uint64_t> signal_values(commands.size(), k_TransferCompleteSemaphoreValue);

    for (auto& command : commands)
    {
        cmd_buffers.emplace_back(command.CmdBuffer());
        signal_semaphores.emplace_back(command.Semaphore());
    }

    VkTimelineSemaphoreSubmitInfo timeline_info = {};
    timeline_info.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    timeline_info.pSignalSemaphoreValues = signal_values.data();
    timeline_info.signalSemaphoreValueCount = static_cast<uint32_t>(signal_values.size());

    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.pNext = &timeline_info;
    submit_info.commandBufferCount = static_cast<uint32_t>(cmd_buffers.size());
    submit_info.pCommandBuffers = cmd_buffers.data();
    submit_info.signalSemaphoreCount = static_cast<uint32_t>(signal_semaphores.size());
    submit_info.pSignalSemaphores = signal_semaphores.data();

    // fixed: the fence was created after the submit and never passed to it, so waiting on it never returned
    std::unique_ptr<rhi::Fence> batchFence = std::make_unique<rhi::Fence>(device->Handle());
    VkResult result = vkQueueSubmit(transferQueue, 1, &submit_info, batchFence->Handle().As<VkFence>());
    assert(result == VK_SUCCESS);

    inflightCommandBatches.emplace_back(std::move(commands), std::move(batchFence));
    commands.clear();
}

void ResourceTransferSystem::waitForCommandsToComplete(std::chrono::milliseconds timeout)
{
    using clock = std::chrono::steady_clock;
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
            if (status == MessageReply::Status::Timeout)
            {
                // we'll get em next time
                // (maybe worth checking to see if we've hit a TDR?)
                break;
            }
            // I wonder how much time we burn in erasing the command and running the destructor?
            // Probably doesn't matter since commands are still in flight, but still
            iter = batch_iter->commands.erase(iter);
        }

        if (batch_iter->commands.empty())
        {
            // every command signalled, so the fence is signalled too
            batch_iter = inflightCommandBatches.erase(batch_iter);
        }
        else
        {
            ++batch_iter;
        }
    }
}

// fixed, in every process function below: each one built its TransferCommand as a local, so the recorded command was
// destroyed at the end of the function and never submitted (the destructor then waited a second and threw). each
// command now moves into `commands`, which the worker submits

void ResourceTransferSystem::processSetBufferDataMessage(TransferSystemSetBufferDataMessage&& message)
{
    const VkBuffer buffer_handle = message.bufferInfo.bufferHandle;

    TransferCommand transfer_command(device, transferQueueFamily, allocatorHandle, std::move(message.reply));

    // note that this is copying to an API managed staging buffer, not just another raw data buffer like our data container. will only be briefly duplicated
    InternalResourceDataContainer::BufferDataVector& dataVector = std::get<InternalResourceDataContainer::BufferDataVector>(message.data.DataVector);

    UploadBuffer* upload_buffer = transfer_command.GetUploadBuffer();
    std::vector<VkBufferCopy> buffer_copies = upload_buffer->SetData(dataVector);

    VkCommandBuffer cmd = transfer_command.CmdBuffer();
    vkCmdCopyBuffer(cmd, upload_buffer->Buffer, buffer_handle, static_cast<uint32_t>(buffer_copies.size()), buffer_copies.data());

    // no barrier after the copy: other queues see the write through the reply's semaphore, which consumers wait on
    transfer_command.EndRecording();
    dataVector.clear();
    commands.emplace_back(std::move(transfer_command));
}

void ResourceTransferSystem::processSetImageDataMessage(TransferSystemSetImageDataMessage&& message)
{
    const ImageCreateInfo& image_info = message.imageInfo.createInfo;
    const VkImage image_handle = message.imageInfo.imageHandle;
    const VkImageSubresourceRange range = WholeImageRange(image_info);
    const VkImageLayout final_layout = ImageLayoutAfterTransfer(image_info.Usage);

    TransferCommand transfer_command(device, transferQueueFamily, allocatorHandle, std::move(message.reply));
    InternalResourceDataContainer::ImageDataVector& imageDataVector = std::get<InternalResourceDataContainer::ImageDataVector>(message.data.DataVector);

    UploadBuffer* upload_buffer = transfer_command.GetUploadBuffer();
    std::vector<VkBufferImageCopy> buffer_image_copies = upload_buffer->SetData(imageDataVector, range.aspectMask);

    VkCommandBuffer cmd = transfer_command.CmdBuffer();

    // required, unlike with buffers. forces a layout transition. stages are limited to the ones a transfer queue has
    RecordImageLayoutTransition(cmd, image_handle, range,
        message.imageInfo.currentLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    vkCmdCopyBufferToImage(
        cmd,
        upload_buffer->Buffer,
        image_handle,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        static_cast<uint32_t>(buffer_image_copies.size()),
        buffer_image_copies.data());
    RecordImageLayoutTransition(cmd, image_handle, range,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, final_layout,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE);

    transfer_command.EndRecording();
    imageDataVector.clear();
    commands.emplace_back(std::move(transfer_command));
}

void ResourceTransferSystem::processFillBufferMessage(TransferSystemFillBufferMessage&& message)
{
    const VkBuffer buffer_handle = message.bufferInfo.bufferHandle;

    TransferCommand transfer_command(device, transferQueueFamily, std::move(message.reply));
    VkCommandBuffer cmd = transfer_command.CmdBuffer();
    vkCmdFillBuffer(cmd, buffer_handle, message.offset, message.size, message.value);
    transfer_command.EndRecording();
    commands.emplace_back(std::move(transfer_command));
}

void ResourceTransferSystem::processCopyBufferToBufferMessage(TransferSystemCopyBufferToBufferMessage&& message)
{
    const BufferCreateInfo& src_info = message.srcBufferInfo.createInfo;
    const BufferCreateInfo& dst_info = message.dstBufferInfo.createInfo;
    if (src_info.Size > dst_info.Size)
    {
        message.reply->SetStatus(MessageReply::Status::Failed);
        return;
    }

    const VkBufferCopy copy
    {
        0u,
        0u,
        src_info.Size
    };

    TransferCommand transfer_command(device, transferQueueFamily, std::move(message.reply));
    VkCommandBuffer cmd = transfer_command.CmdBuffer();
    vkCmdCopyBuffer(cmd, message.srcBufferInfo.bufferHandle, message.dstBufferInfo.bufferHandle, 1, &copy);
    transfer_command.EndRecording();
    commands.emplace_back(std::move(transfer_command));
}

void ResourceTransferSystem::processCopyImageToImageMessage(TransferSystemCopyImageToImageMessage&& message)
{
    const ImageCreateInfo& src_info = message.srcImageInfo.createInfo;
    const ImageCreateInfo& dst_info = message.dstImageInfo.createInfo;
    const VkImage src_handle = message.srcImageInfo.imageHandle;
    const VkImage dst_handle = message.dstImageInfo.imageHandle;
    const VkImageSubresourceRange src_range = WholeImageRange(src_info);
    const VkImageSubresourceRange dst_range = WholeImageRange(dst_info);

    if (src_range.aspectMask != dst_range.aspectMask ||
        src_info.ArrayLayers != dst_info.ArrayLayers ||
        src_info.Width != dst_info.Width ||
        src_info.Height != dst_info.Height ||
        src_info.Depth != dst_info.Depth ||
        src_info.MipLevels != dst_info.MipLevels)
    {
        // if these traits don't match, we can't copy (for now)
        // may need to investigate and return to this later, but most of these traits
        // shouldn't be a problem. may need to specify copy region/layer somehow
        message.reply->SetStatus(MessageReply::Status::Failed);
        return;
    }

    const VkExtent3D extent = ImageExtent(src_info);
    std::vector<VkImageCopy> image_copies;
    image_copies.reserve(src_info.MipLevels);
    for (uint32_t mip = 0; mip < src_info.MipLevels; ++mip)
    {
        // all layers of a mip level go in one region
        const VkExtent3D mip_extent
        {
            std::max(extent.width >> mip, 1u),
            std::max(extent.height >> mip, 1u),
            std::max(extent.depth >> mip, 1u)
        };
        const VkImageSubresourceLayers subresource{ src_range.aspectMask, mip, 0u, src_info.ArrayLayers };
        image_copies.push_back(VkImageCopy{ subresource, VkOffset3D{ 0, 0, 0 }, subresource, VkOffset3D{ 0, 0, 0 }, mip_extent });
    }

    const VkImageLayout src_final = ImageLayoutAfterTransfer(src_info.Usage);
    const VkImageLayout dst_final = ImageLayoutAfterTransfer(dst_info.Usage);

    TransferCommand transfer_command(device, transferQueueFamily, std::move(message.reply));
    VkCommandBuffer cmd = transfer_command.CmdBuffer();
    RecordImageLayoutTransition(cmd, src_handle, src_range,
        message.srcImageInfo.currentLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    RecordImageLayoutTransition(cmd, dst_handle, dst_range,
        message.dstImageInfo.currentLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    // fixed: the copy named the usage layouts (shader read, attachment) instead of the transfer layouts
    vkCmdCopyImage(
        cmd,
        src_handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        dst_handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        static_cast<uint32_t>(image_copies.size()), image_copies.data());
    RecordImageLayoutTransition(cmd, src_handle, src_range,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, src_final,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE);
    RecordImageLayoutTransition(cmd, dst_handle, dst_range,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, dst_final,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE);

    transfer_command.EndRecording();
    commands.emplace_back(std::move(transfer_command));
}

void ResourceTransferSystem::processCopyImageToBufferMessage(TransferSystemCopyImageToBufferMessage&& message)
{
    // Not implemented and I really don't want to do it right now lol
    message.reply->SetStatus(MessageReply::Status::Failed);
}

void ResourceTransferSystem::processCopyBufferToImageMessage(TransferSystemCopyBufferToImageMessage&& message)
{
    const ImageCreateInfo& dst_info = message.imageInfo.createInfo;
    const VkImage dst_handle = message.imageInfo.imageHandle;
    const VkImageSubresourceRange dst_range = WholeImageRange(dst_info);

    // copies the whole buffer into mip 0 of every layer, tightly packed
    const VkBufferImageCopy copy_params
    {
        0u,
        0u,
        0u,
        VkImageSubresourceLayers{ dst_range.aspectMask, 0u, 0u, dst_info.ArrayLayers },
        VkOffset3D{ 0, 0, 0 },
        ImageExtent(dst_info)
    };

    TransferCommand transfer_command(device, transferQueueFamily, std::move(message.reply));
    VkCommandBuffer cmd = transfer_command.CmdBuffer();
    RecordImageLayoutTransition(cmd, dst_handle, dst_range,
        message.imageInfo.currentLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    // fixed: the copy named the usage layout instead of TRANSFER_DST_OPTIMAL
    vkCmdCopyBufferToImage(cmd, message.bufferInfo.bufferHandle, dst_handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &copy_params);
    RecordImageLayoutTransition(cmd, dst_handle, dst_range,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, ImageLayoutAfterTransfer(dst_info.Usage),
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE);

    transfer_command.EndRecording();
    commands.emplace_back(std::move(transfer_command));
}

namespace
{
    void RecordTransferOrderingBarrier(VkCommandBuffer cmd) noexcept
    {
        const VkMemoryBarrier2 barrier
        {
            VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            nullptr,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT
        };
        const VkDependencyInfo dependency{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr, 0, 1, &barrier, 0, nullptr, 0, nullptr };
        vkCmdPipelineBarrier2(cmd, &dependency);
    }

    void RecordImageLayoutTransition(
        VkCommandBuffer cmd,
        VkImage image,
        const VkImageSubresourceRange& range,
        VkImageLayout old_layout,
        VkImageLayout new_layout,
        VkPipelineStageFlags2 src_stage,
        VkAccessFlags2 src_access,
        VkPipelineStageFlags2 dst_stage,
        VkAccessFlags2 dst_access) noexcept
    {
        const VkImageMemoryBarrier2 barrier
        {
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            nullptr,
            src_stage,
            src_access,
            dst_stage,
            dst_access,
            old_layout,
            new_layout,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            image,
            range
        };
        const VkDependencyInfo dependency{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr, 0, 0, nullptr, 0, nullptr, 1, &barrier };
        vkCmdPipelineBarrier2(cmd, &dependency);
    }

    VkImageAspectFlags AspectMaskForImage(const ImageCreateInfo& create_info) noexcept
    {
        return ToVkImageAspect(AspectFromVkFormat(ToVkFormat(create_info.Format)));
    }

    VkImageSubresourceRange WholeImageRange(const ImageCreateInfo& create_info) noexcept
    {
        return VkImageSubresourceRange{ AspectMaskForImage(create_info), 0u, create_info.MipLevels, 0u, create_info.ArrayLayers };
    }

    VkExtent3D ImageExtent(const ImageCreateInfo& create_info) noexcept
    {
        return VkExtent3D{ create_info.Width, std::max(create_info.Height, 1u), std::max(create_info.Depth, 1u) };
    }
}
