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

constexpr static VkDebugUtilsLabelEXT k_QueueDebugLabel
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

MessageReply::Status ResourceTransferSystem::TransferCommand::WaitForCompletion(uint64_t timeout_ns)
{
    MessageReply::Status status = reply->WaitForCompletion(timeout_ns);
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

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VkResult result = vkBeginCommandBuffer(CmdBuffer(), &beginInfo);
    assert(result == VK_SUCCESS);

    const rhi::VkDebugUtilsFunctions& debugFns = device->GetDebugUtilFns();
    if (debugFns.vkCmdBeginDebugUtilsLabel != nullptr)
    {
        debugFns.vkCmdBeginDebugUtilsLabel(CmdBuffer(), &k_QueueDebugLabel);
    }

    // every transfer is ordered after the transfers before it on this queue. submissions on one queue give no memory
    // dependency, so an upload followed by a copy of the same buffer would race without this
    RecordTransferOrderingBarrier(CmdBuffer());
}

ResourceTransferSystem::ResourceTransferSystem() = default;

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
    VmaAllocatorCreateInfo createInfo = {};
    createInfo.flags = VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT;
    createInfo.physicalDevice = device->GetPhysicalDevice().As<VkPhysicalDevice>();
    createInfo.device = device->Handle().As<VkDevice>();
    createInfo.instance = instance;
    createInfo.vulkanApiVersion = vulkan_api_version;

    VkResult result = vmaCreateAllocator(&createInfo, &allocatorHandle);
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
        batch.Fence->Wait();
        for (TransferCommand& command : batch.Commands)
        {
            [[maybe_unused]] MessageReply::Status status = command.WaitForCompletion(std::numeric_limits<uint64_t>::max());
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
    static constexpr std::chrono::milliseconds k_MessageProcessingTimeout = std::chrono::milliseconds(2);
    // 1ms chosen to give us less time than the message processing, but still enough for GPU to do work
    // PCI bus goes WHIRRRRRRRRRRRRRRRRR
    static constexpr std::chrono::milliseconds k_CommandSubmissionTimeout = std::chrono::milliseconds(1);

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
        didWork = processMessages(k_MessageProcessingTimeout);

        // Submit any pending commands
        if (!commands.empty())
        {
            submitTransferCommands();
            didWork = true;
        }

        // Wait for in-flight commands to complete
        if (!inflightCommandBatches.empty())
        {
            waitForCommandsToComplete(k_CommandSubmissionTimeout);
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
    using Clock = std::chrono::steady_clock;
    const Clock::time_point start = Clock::now();
    bool processedMessages = false;

    // fixed: dispatch went through explicit specializations of a member template declared in class scope, which only
    // MSVC accepts. an overload set on the process functions does the same job
    auto messageVisitor =
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

        std::visit(messageVisitor, std::move(*message));
        processedMessages = true;

        if ((Clock::now() - start) > timeout)
        {
            return processedMessages;
        }
    }
}

void ResourceTransferSystem::submitTransferCommands()
{
    std::vector<VkCommandBuffer> cmdBuffers;
    std::vector<VkSemaphore> signalSemaphores;
    cmdBuffers.reserve(commands.size());
    signalSemaphores.reserve(commands.size());
    const std::vector<uint64_t> signalValues(commands.size(), k_TransferCompleteSemaphoreValue);

    for (auto& command : commands)
    {
        cmdBuffers.emplace_back(command.CmdBuffer());
        signalSemaphores.emplace_back(command.Semaphore());
    }

    VkTimelineSemaphoreSubmitInfo timelineInfo = {};
    timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    timelineInfo.pSignalSemaphoreValues = signalValues.data();
    timelineInfo.signalSemaphoreValueCount = static_cast<uint32_t>(signalValues.size());

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pNext = &timelineInfo;
    submitInfo.commandBufferCount = static_cast<uint32_t>(cmdBuffers.size());
    submitInfo.pCommandBuffers = cmdBuffers.data();
    submitInfo.signalSemaphoreCount = static_cast<uint32_t>(signalSemaphores.size());
    submitInfo.pSignalSemaphores = signalSemaphores.data();

    // fixed: the fence was created after the submit and never passed to it, so waiting on it never returned
    std::unique_ptr<rhi::Fence> batchFence = std::make_unique<rhi::Fence>(device->Handle());
    VkResult result = vkQueueSubmit(transferQueue, 1, &submitInfo, batchFence->Handle().As<VkFence>());
    assert(result == VK_SUCCESS);

    inflightCommandBatches.emplace_back(std::move(commands), std::move(batchFence));
    commands.clear();
}

void ResourceTransferSystem::waitForCommandsToComplete(std::chrono::milliseconds timeout)
{
    using Clock = std::chrono::steady_clock;
    const Clock::time_point waitStart = Clock::now();
    const Clock::time_point waitEnd = waitStart + timeout;
    for (auto batchIter = inflightCommandBatches.begin(); batchIter != inflightCommandBatches.end();)
    {
        const Clock::time_point nowOuter = Clock::now();
        if (nowOuter >= waitEnd)
        {
            break;
        }

        for (auto iter = batchIter->Commands.begin(); iter != batchIter->Commands.end();)
        {
            const Clock::time_point nowInner = Clock::now();
            if (nowInner >= waitEnd)
            {
                break;
            }

            // Calculate remaining time in nanoseconds
            const int64_t remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(waitEnd - nowInner).count();

            MessageReply::Status status = iter->WaitForCompletion(static_cast<uint64_t>(remaining));
            if (status == MessageReply::Status::Timeout)
            {
                // we'll get em next time
                // (maybe worth checking to see if we've hit a TDR?)
                break;
            }
            
            // I wonder how much time we burn in erasing the command and running the destructor?
            // Probably doesn't matter since commands are still in flight, but still
            iter = batchIter->Commands.erase(iter);
        }

        if (batchIter->Commands.empty())
        {
            // every command signalled, so the fence is signalled too
            batchIter = inflightCommandBatches.erase(batchIter);
        }
        else
        {
            ++batchIter;
        }
    }
}

// fixed, in every process function below: each one built its TransferCommand as a local, so the recorded command was
// destroyed at the end of the function and never submitted (the destructor then waited a second and threw). each
// command now moves into `commands`, which the worker submits

void ResourceTransferSystem::processSetBufferDataMessage(TransferSystemSetBufferDataMessage&& message)
{
    const VkBuffer bufferHandle = message.bufferInfo.bufferHandle;

    TransferCommand transferCommand(device, transferQueueFamily, allocatorHandle, std::move(message.reply));

    // note that this is copying to an API managed staging buffer, not just another raw data buffer like our data container. will only be briefly duplicated
    InternalResourceDataContainer::BufferDataVector& dataVector = std::get<InternalResourceDataContainer::BufferDataVector>(message.data.DataVector);

    UploadBuffer* uploadBuffer = transferCommand.GetUploadBuffer();
    std::vector<VkBufferCopy> bufferCopies = uploadBuffer->SetData(dataVector);

    VkCommandBuffer cmd = transferCommand.CmdBuffer();
    vkCmdCopyBuffer(cmd, uploadBuffer->Buffer, bufferHandle, static_cast<uint32_t>(bufferCopies.size()), bufferCopies.data());

    // no barrier after the copy: other queues see the write through the reply's semaphore, which consumers wait on
    transferCommand.EndRecording();
    dataVector.clear();
    commands.emplace_back(std::move(transferCommand));
}

void ResourceTransferSystem::processSetImageDataMessage(TransferSystemSetImageDataMessage&& message)
{
    const ImageCreateInfo& imageInfo = message.imageInfo.createInfo;
    const VkImage imageHandle = message.imageInfo.imageHandle;
    const VkImageSubresourceRange range = WholeImageRange(imageInfo);
    const VkImageLayout finalLayout = ImageLayoutAfterTransfer(imageInfo.Usage);

    TransferCommand transferCommand(device, transferQueueFamily, allocatorHandle, std::move(message.reply));
    InternalResourceDataContainer::ImageDataVector& imageDataVector = std::get<InternalResourceDataContainer::ImageDataVector>(message.data.DataVector);

    UploadBuffer* uploadBuffer = transferCommand.GetUploadBuffer();
    std::vector<VkBufferImageCopy> bufferImageCopies = uploadBuffer->SetData(imageDataVector, range.aspectMask);

    VkCommandBuffer cmd = transferCommand.CmdBuffer();

    // required, unlike with buffers. forces a layout transition. stages are limited to the ones a transfer queue has
    RecordImageLayoutTransition(cmd, imageHandle, range,
        message.imageInfo.currentLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    vkCmdCopyBufferToImage(
        cmd,
        uploadBuffer->Buffer,
        imageHandle,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        static_cast<uint32_t>(bufferImageCopies.size()),
        bufferImageCopies.data());
    RecordImageLayoutTransition(cmd, imageHandle, range,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, finalLayout,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE);

    transferCommand.EndRecording();
    imageDataVector.clear();
    commands.emplace_back(std::move(transferCommand));
}

void ResourceTransferSystem::processFillBufferMessage(TransferSystemFillBufferMessage&& message)
{
    const VkBuffer bufferHandle = message.bufferInfo.bufferHandle;

    TransferCommand transferCommand(device, transferQueueFamily, std::move(message.reply));
    VkCommandBuffer cmd = transferCommand.CmdBuffer();
    vkCmdFillBuffer(cmd, bufferHandle, message.offset, message.size, message.value);
    transferCommand.EndRecording();
    commands.emplace_back(std::move(transferCommand));
}

void ResourceTransferSystem::processCopyBufferToBufferMessage(TransferSystemCopyBufferToBufferMessage&& message)
{
    const BufferCreateInfo& srcInfo = message.srcBufferInfo.createInfo;
    const BufferCreateInfo& dstInfo = message.dstBufferInfo.createInfo;
    if (srcInfo.Size > dstInfo.Size)
    {
        message.reply->setStatus(MessageReply::Status::Failed);
        return;
    }

    const VkBufferCopy copy
    {
        0u,
        0u,
        srcInfo.Size
    };

    TransferCommand transferCommand(device, transferQueueFamily, std::move(message.reply));
    VkCommandBuffer cmd = transferCommand.CmdBuffer();
    vkCmdCopyBuffer(cmd, message.srcBufferInfo.bufferHandle, message.dstBufferInfo.bufferHandle, 1, &copy);
    transferCommand.EndRecording();
    commands.emplace_back(std::move(transferCommand));
}

void ResourceTransferSystem::processCopyImageToImageMessage(TransferSystemCopyImageToImageMessage&& message)
{
    const ImageCreateInfo& srcInfo = message.srcImageInfo.createInfo;
    const ImageCreateInfo& dstInfo = message.dstImageInfo.createInfo;
    const VkImage srcHandle = message.srcImageInfo.imageHandle;
    const VkImage dstHandle = message.dstImageInfo.imageHandle;
    const VkImageSubresourceRange srcRange = WholeImageRange(srcInfo);
    const VkImageSubresourceRange dstRange = WholeImageRange(dstInfo);

    if (srcRange.aspectMask != dstRange.aspectMask ||
        srcInfo.ArrayLayers != dstInfo.ArrayLayers ||
        srcInfo.Width != dstInfo.Width ||
        srcInfo.Height != dstInfo.Height ||
        srcInfo.Depth != dstInfo.Depth ||
        srcInfo.MipLevels != dstInfo.MipLevels)
    {
        // if these traits don't match, we can't copy (for now)
        // may need to investigate and return to this later, but most of these traits
        // shouldn't be a problem. may need to specify copy region/layer somehow
        message.reply->setStatus(MessageReply::Status::Failed);
        return;
    }

    const VkExtent3D extent = ImageExtent(srcInfo);
    std::vector<VkImageCopy> imageCopies;
    imageCopies.reserve(srcInfo.MipLevels);
    for (uint32_t mip = 0; mip < srcInfo.MipLevels; ++mip)
    {
        // all layers of a mip level go in one region
        const VkExtent3D mipExtent
        {
            std::max(extent.width >> mip, 1u),
            std::max(extent.height >> mip, 1u),
            std::max(extent.depth >> mip, 1u)
        };
        const VkImageSubresourceLayers subresource{ srcRange.aspectMask, mip, 0u, srcInfo.ArrayLayers };
        imageCopies.push_back(VkImageCopy{ subresource, VkOffset3D{ 0, 0, 0 }, subresource, VkOffset3D{ 0, 0, 0 }, mipExtent });
    }

    const VkImageLayout srcFinal = ImageLayoutAfterTransfer(srcInfo.Usage);
    const VkImageLayout dstFinal = ImageLayoutAfterTransfer(dstInfo.Usage);

    TransferCommand transferCommand(device, transferQueueFamily, std::move(message.reply));
    VkCommandBuffer cmd = transferCommand.CmdBuffer();
    RecordImageLayoutTransition(cmd, srcHandle, srcRange,
        message.srcImageInfo.currentLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    RecordImageLayoutTransition(cmd, dstHandle, dstRange,
        message.dstImageInfo.currentLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    // fixed: the copy named the usage layouts (shader read, attachment) instead of the transfer layouts
    vkCmdCopyImage(
        cmd,
        srcHandle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        dstHandle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        static_cast<uint32_t>(imageCopies.size()), imageCopies.data());
    RecordImageLayoutTransition(cmd, srcHandle, srcRange,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, srcFinal,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE);
    RecordImageLayoutTransition(cmd, dstHandle, dstRange,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, dstFinal,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE);

    transferCommand.EndRecording();
    commands.emplace_back(std::move(transferCommand));
}

void ResourceTransferSystem::processCopyImageToBufferMessage(TransferSystemCopyImageToBufferMessage&& message)
{
    // Not implemented and I really don't want to do it right now lol
    message.reply->setStatus(MessageReply::Status::Failed);
}

void ResourceTransferSystem::processCopyBufferToImageMessage(TransferSystemCopyBufferToImageMessage&& message)
{
    const ImageCreateInfo& dstInfo = message.imageInfo.createInfo;
    const VkImage dstHandle = message.imageInfo.imageHandle;
    const VkImageSubresourceRange dstRange = WholeImageRange(dstInfo);

    // copies the whole buffer into mip 0 of every layer, tightly packed
    const VkBufferImageCopy copyParams
    {
        0u,
        0u,
        0u,
        VkImageSubresourceLayers{ dstRange.aspectMask, 0u, 0u, dstInfo.ArrayLayers },
        VkOffset3D{ 0, 0, 0 },
        ImageExtent(dstInfo)
    };

    TransferCommand transferCommand(device, transferQueueFamily, std::move(message.reply));
    VkCommandBuffer cmd = transferCommand.CmdBuffer();
    RecordImageLayoutTransition(cmd, dstHandle, dstRange,
        message.imageInfo.currentLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    // fixed: the copy named the usage layout instead of TRANSFER_DST_OPTIMAL
    vkCmdCopyBufferToImage(cmd, message.bufferInfo.bufferHandle, dstHandle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &copyParams);
    RecordImageLayoutTransition(cmd, dstHandle, dstRange,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, ImageLayoutAfterTransfer(dstInfo.Usage),
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE);

    transferCommand.EndRecording();
    commands.emplace_back(std::move(transferCommand));
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
