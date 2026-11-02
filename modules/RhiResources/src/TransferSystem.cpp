#include "TransferSystem.hpp"
#include "DebugUtilFns.hpp"
#include "ImageDataFormatsVulkan.hpp"
#include "ResourceContext.hpp"
#include "CommandPool.hpp"
#include "Device.hpp"
#include "ResourceMessageReply.hpp"
#include "ResourceMessageTypesInternal.hpp"
#include "ResourceTypes.hpp"
#include "RhiResult.hpp"
#include "RhiSystem.hpp"
#include "UploadBuffer.hpp"
#include "VulkanConversions.hpp"
#include "containers/mwsrQueue.hpp"
#include "vulkan/vulkan_core.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <thread>
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
    // batches in flight before the transfer thread waits on the GPU for the oldest one
    constexpr int32_t k_BatchCount = 4;
    // a full batch is submitted at once, so that one long burst of messages still reaches the GPU in pieces
    constexpr int32_t k_MaxTransfersPerBatch = 64;
    // how long the transfer thread sleeps on the timeline while batches are in flight and the queue is empty. it is
    // also the longest a new message waits in that state
    constexpr uint64_t k_IdleWaitNs = 500'000u;

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

ResourceTransferSystem::ResourceTransferSystem() = default;

ResourceTransferSystem::~ResourceTransferSystem()
{
    destroy();
}

ResourceContextError ResourceTransferSystem::Initialize(
    const rhi::Device* _device,
    VkInstance instance,
    uint32_t vulkan_api_version,
    mwsrQueue<ResourceMessagePayloadType>* context_queue)
{
    if (initialized)
    {
        return ResourceContextError::AlreadyInitialized;
    }

    device = _device;
    contextQueue = context_queue;

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
    if (result != VK_SUCCESS)
    {
        allocatorHandle = VK_NULL_HANDLE;
        device = nullptr;
        return ResourceContextError::AllocatorCreationFailed;
    }

    const VkSemaphoreTypeCreateInfo timelineTypeInfo
    {
        VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
        nullptr,
        VK_SEMAPHORE_TYPE_TIMELINE,
        0u
    };
    const VkSemaphoreCreateInfo timelineInfo
    {
        VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        &timelineTypeInfo,
        0u
    };

    result = vkCreateSemaphore(device->Handle().As<VkDevice>(), &timelineInfo, nullptr, &timeline);
    if (result != VK_SUCCESS)
    {
        timeline = VK_NULL_HANDLE;
        destroy();
        return ResourceContextError::TransferSetupFailed;
    }

    rhi::RhiSystem::SetObjectName(VK_OBJECT_TYPE_SEMAPHORE, reinterpret_cast<uint64_t>(timeline), "ResourceContext_TransferTimeline");

    freeBatches.reserve(k_BatchCount);
    inflightBatches.reserve(k_BatchCount);
    for (int32_t i = 0; i < k_BatchCount; ++i)
    {
        TransferBatch& batch = freeBatches.emplace_back();
        batch.Pool = std::make_unique<rhi::CommandPool>(device->Handle(), rhi::CommandPool::Type::Transfer, transferQueueFamily);
        if (batch.Pool->Handle().As<VkCommandPool>() == VK_NULL_HANDLE || !batch.Pool->AllocateCommandBuffers(1).IsSuccess())
        {
            destroy();
            return ResourceContextError::TransferSetupFailed;
        }

        batch.Replies.reserve(k_MaxTransfersPerBatch);
        batch.Staging.reserve(k_MaxTransfersPerBatch);
    }

    initialized = true;
    startWorker();
    return ResourceContextError::Success;
}

void ResourceTransferSystem::EnqueueTransfer(TransferPayloadType&& payload)
{
    messageQueue.push(std::move(payload));
}

uint64_t ResourceTransferSystem::TimelineHandle() const noexcept
{
    return reinterpret_cast<uint64_t>(timeline);
}

uint64_t ResourceTransferSystem::CompletedTicket() const noexcept
{
    return completedTicket.load(std::memory_order_acquire);
}

TransferSystemCounters ResourceTransferSystem::Counters() const noexcept
{
    return TransferSystemCounters
    {
        transfersRecorded.load(std::memory_order_relaxed),
        transfersFailed.load(std::memory_order_relaxed),
        batchesSubmitted.load(std::memory_order_relaxed),
        completionReads.load(std::memory_order_relaxed),
        bytesStaged.load(std::memory_order_relaxed),
        messageQueue.BlockedPushCount()
    };
}

void ResourceTransferSystem::startWorker()
{
    exitRequested = false;
    workerThread = std::thread(&ResourceTransferSystem::workerThreadJob, this);
}

void ResourceTransferSystem::stopWorker()
{
    if (workerThread.joinable())
    {
        // messages pushed before this one are processed by the worker, any pushed after it by processRemainingAndWait
        messageQueue.push(TransferSystemExitMessage{});
        workerThread.join();
    }
}

void ResourceTransferSystem::destroy()
{
    if (initialized)
    {
        stopWorker();
        processRemainingAndWait();
    }

    // the staging buffers belong to this allocator, so the batches go first
    recording.reset();
    freeBatches.clear();
    inflightBatches.clear();

    if (timeline != VK_NULL_HANDLE)
    {
        vkDestroySemaphore(device->Handle().As<VkDevice>(), timeline, nullptr);
        timeline = VK_NULL_HANDLE;
    }

    if (allocatorHandle != VK_NULL_HANDLE)
    {
        vmaDestroyAllocator(allocatorHandle);
        allocatorHandle = VK_NULL_HANDLE;
    }

    device = nullptr;
    initialized = false;
}

void ResourceTransferSystem::processRemainingAndWait()
{
    // only called with the worker stopped, so this thread is the queue's only reader
    exitRequested = false;
    drainMessages();
    submitBatch();

    if (lastSignaledValue == 0u)
    {
        return;
    }

    const VkSemaphoreWaitInfo waitInfo
    {
        VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        nullptr,
        0u,
        1u,
        &timeline,
        &lastSignaledValue
    };

    [[maybe_unused]] const VkResult result = vkWaitSemaphores(device->Handle().As<VkDevice>(), &waitInfo, std::numeric_limits<uint64_t>::max());
    pollCompletion();
}

void ResourceTransferSystem::workerThreadJob()
{
    while (!exitRequested)
    {
        // nothing recorded and nothing in flight: sleep on the queue until a message arrives
        const bool idle = !recording.has_value() && inflightBatches.empty() && highestConsumedTicket <= lastSignaledValue;
        if (idle)
        {
            dispatchMessage(messageQueue.pop());
        }

        drainMessages();
        submitBatch();
        pollCompletion();

        if (!exitRequested && !inflightBatches.empty())
        {
            waitForOldestBatch(k_IdleWaitNs);
        }
    }
}

void ResourceTransferSystem::dispatchMessage(TransferPayloadType&& message)
{
    std::visit([this](auto&& alternative) { processMessage(std::forward<decltype(alternative)>(alternative)); }, std::move(message));
}

void ResourceTransferSystem::drainMessages()
{
    while (!exitRequested)
    {
        std::optional<TransferPayloadType> message = messageQueue.try_pop();
        if (!message.has_value())
        {
            return;
        }

        dispatchMessage(std::move(*message));
    }
}

VkCommandBuffer ResourceTransferSystem::beginTransfer()
{
    if (recording.has_value() && recording->TransferCount >= k_MaxTransfersPerBatch)
    {
        submitBatch();
    }

    if (!recording.has_value() && !startBatch())
    {
        return VK_NULL_HANDLE;
    }

    const VkCommandBuffer cmd = recording->Pool->GetCommandBuffer(0).As<VkCommandBuffer>();

    // every transfer is ordered after the transfers before it on this queue, in this batch and in earlier ones. an
    // upload followed by a copy of the same buffer would race without this
    RecordTransferOrderingBarrier(cmd);
    return cmd;
}

void ResourceTransferSystem::endTransfer(std::shared_ptr<ResourceTransferReply>&& reply, std::unique_ptr<UploadBuffer>&& staging, uint64_t ticket)
{
    if (staging != nullptr)
    {
        bytesStaged.fetch_add(static_cast<int64_t>(staging->Size), std::memory_order_relaxed);
        recording->Staging.push_back(std::move(staging));
    }

    recording->Replies.push_back(std::move(reply));
    recording->TransferCount += 1;
    highestConsumedTicket = std::max(highestConsumedTicket, ticket);
    transfersRecorded.fetch_add(1, std::memory_order_relaxed);
}

void ResourceTransferSystem::failTransfer(std::shared_ptr<ResourceTransferReply>&& reply, uint64_t ticket)
{
    reply->setStatus(MessageReply::Status::Failed);
    highestConsumedTicket = std::max(highestConsumedTicket, ticket);
    transfersFailed.fetch_add(1, std::memory_order_relaxed);
}

bool ResourceTransferSystem::startBatch()
{
    if (freeBatches.empty())
    {
        // every batch is in flight. this is the one place the transfer thread waits on the GPU without a timeout
        waitForOldestBatch(std::numeric_limits<uint64_t>::max());
    }

    if (freeBatches.empty())
    {
        return false;
    }

    TransferBatch batch = std::move(freeBatches.back());
    freeBatches.pop_back();

    // the timeline passed this batch's value before it came back, so the GPU is done with its command buffer
    if (!batch.Pool->Reset().IsSuccess())
    {
        freeBatches.push_back(std::move(batch));
        return false;
    }

    const VkCommandBuffer cmd = batch.Pool->GetCommandBuffer(0).As<VkCommandBuffer>();
    const VkCommandBufferBeginInfo beginInfo
    {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        nullptr,
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        nullptr
    };

    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS)
    {
        freeBatches.push_back(std::move(batch));
        return false;
    }

    const rhi::VkDebugUtilsFunctions& debugFns = device->GetDebugUtilFns();
    if (debugFns.vkCmdBeginDebugUtilsLabel != nullptr)
    {
        debugFns.vkCmdBeginDebugUtilsLabel(cmd, &k_QueueDebugLabel);
    }

    recording = std::move(batch);
    return true;
}

void ResourceTransferSystem::submitBatch()
{
    if (!recording.has_value() && highestConsumedTicket <= lastSignaledValue)
    {
        return;
    }

    const uint64_t signalValue = highestConsumedTicket;
    VkCommandBufferSubmitInfo commandInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, nullptr, VK_NULL_HANDLE, 0u };
    uint32_t commandCount = 0u;

    if (recording.has_value())
    {
        const VkCommandBuffer cmd = recording->Pool->GetCommandBuffer(0).As<VkCommandBuffer>();
        const rhi::VkDebugUtilsFunctions& debugFns = device->GetDebugUtilFns();
        if (debugFns.vkCmdEndDebugUtilsLabel != nullptr)
        {
            debugFns.vkCmdEndDebugUtilsLabel(cmd);
        }

        if (vkEndCommandBuffer(cmd) == VK_SUCCESS)
        {
            commandInfo.commandBuffer = cmd;
            commandCount = 1u;
        }
        else
        {
            // nothing of this batch runs. its tickets are still signaled below, so their waits end
            for (std::shared_ptr<ResourceTransferReply>& reply : recording->Replies)
            {
                failTransfer(std::move(reply), signalValue);
            }

            recording->Replies.clear();
            recording->Staging.clear();
            recording->TransferCount = 0;
            freeBatches.push_back(std::move(*recording));
            recording.reset();
        }
    }

    const VkSemaphoreSubmitInfo signalInfo
    {
        VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        nullptr,
        timeline,
        signalValue,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        0u
    };
    const VkSubmitInfo2 submitInfo
    {
        VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        nullptr,
        0u,
        0u,
        nullptr,
        commandCount,
        commandCount != 0u ? &commandInfo : nullptr,
        1u,
        &signalInfo
    };

    const VkResult result = vkQueueSubmit2(transferQueue, 1u, &submitInfo, VK_NULL_HANDLE);
    lastSignaledValue = signalValue;
    batchesSubmitted.fetch_add(1, std::memory_order_relaxed);

    if (result != VK_SUCCESS)
    {
        if (recording.has_value())
        {
            for (std::shared_ptr<ResourceTransferReply>& reply : recording->Replies)
            {
                failTransfer(std::move(reply), signalValue);
            }

            recording->Replies.clear();
            recording->Staging.clear();
            recording->TransferCount = 0;
            freeBatches.push_back(std::move(*recording));
            recording.reset();
        }

        // the host signals the value instead, once the earlier submits are done: a host signal must not pass a pending
        // GPU signal. on a lost device both fail, and the waits end with an error
        [[maybe_unused]] const VkResult idleResult = vkQueueWaitIdle(transferQueue);
        const VkSemaphoreSignalInfo hostSignal{ VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO, nullptr, timeline, signalValue };
        [[maybe_unused]] const VkResult signalResult = vkSignalSemaphore(device->Handle().As<VkDevice>(), &hostSignal);
        return;
    }

    if (!recording.has_value())
    {
        return;
    }

    for (const std::shared_ptr<ResourceTransferReply>& reply : recording->Replies)
    {
        reply->setSubmitted();
    }

    recording->SignalValue = signalValue;
    inflightBatches.push_back(std::move(*recording));
    recording.reset();
}

void ResourceTransferSystem::pollCompletion()
{
    const uint64_t previous = completedTicket.load(std::memory_order_relaxed);
    if (lastSignaledValue <= previous)
    {
        return;
    }

    uint64_t value = 0u;
    if (vkGetSemaphoreCounterValue(device->Handle().As<VkDevice>(), timeline, &value) != VK_SUCCESS)
    {
        return;
    }

    completionReads.fetch_add(1, std::memory_order_relaxed);

    while (!inflightBatches.empty() && inflightBatches.front().SignalValue <= value)
    {
        TransferBatch& batch = inflightBatches.front();
        for (const std::shared_ptr<ResourceTransferReply>& reply : batch.Replies)
        {
            reply->setStatus(MessageReply::Status::Completed);
        }

        batch.Replies.clear();
        batch.Staging.clear();
        batch.TransferCount = 0;
        freeBatches.push_back(std::move(batch));
        inflightBatches.erase(inflightBatches.begin());
    }

    if (value > previous)
    {
        completedTicket.store(value, std::memory_order_release);

        // never blocks: when the queue is full, the worker has messages to process, and it checks retirements after each
        [[maybe_unused]] const bool pushed = contextQueue->try_push(TransferProgressMessage{});
    }
}

void ResourceTransferSystem::waitForOldestBatch(uint64_t timeout_ns)
{
    if (inflightBatches.empty())
    {
        return;
    }

    const uint64_t waitValue = inflightBatches.front().SignalValue;
    const VkSemaphoreWaitInfo waitInfo
    {
        VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        nullptr,
        0u,
        1u,
        &timeline,
        &waitValue
    };

    const VkResult result = vkWaitSemaphores(device->Handle().As<VkDevice>(), &waitInfo, timeout_ns);
    if (result == VK_SUCCESS)
    {
        pollCompletion();
    }
}

void ResourceTransferSystem::processMessage(TransferSystemSetBufferDataMessage&& message)
{
    const VkBuffer bufferHandle = message.BufferInfo.BufferHandle;

    // note that this is copying to an API managed staging buffer, not just another raw data buffer like our data container. will only be briefly duplicated
    InternalResourceDataContainer::BufferDataVector& dataVector = std::get<InternalResourceDataContainer::BufferDataVector>(message.Data.DataVector);

    std::unique_ptr<UploadBuffer> staging = std::make_unique<UploadBuffer>(allocatorHandle);
    const std::optional<std::vector<VkBufferCopy>> bufferCopies = staging->SetData(dataVector);
    if (!bufferCopies.has_value())
    {
        failTransfer(std::move(message.Reply), message.Ticket);
        return;
    }

    const VkCommandBuffer cmd = beginTransfer();
    if (cmd == VK_NULL_HANDLE)
    {
        failTransfer(std::move(message.Reply), message.Ticket);
        return;
    }

    vkCmdCopyBuffer(cmd, staging->Buffer, bufferHandle, static_cast<uint32_t>(bufferCopies->size()), bufferCopies->data());

    // no barrier after the copy: other queues see the write through the transfer timeline, which consumers wait on
    dataVector.clear();
    endTransfer(std::move(message.Reply), std::move(staging), message.Ticket);
}

void ResourceTransferSystem::processMessage(TransferSystemSetImageDataMessage&& message)
{
    const ImageCreateInfo& imageInfo = message.ImageInfo.CreateInfo;
    const VkImage imageHandle = message.ImageInfo.ImageHandle;
    const VkImageSubresourceRange range = WholeImageRange(imageInfo);
    const VkImageLayout finalLayout = ImageLayoutAfterTransfer(imageInfo.Usage);

    InternalResourceDataContainer::ImageDataVector& imageDataVector = std::get<InternalResourceDataContainer::ImageDataVector>(message.Data.DataVector);

    std::unique_ptr<UploadBuffer> staging = std::make_unique<UploadBuffer>(allocatorHandle);
    const std::optional<std::vector<VkBufferImageCopy>> bufferImageCopies = staging->SetData(imageDataVector, range.aspectMask);
    if (!bufferImageCopies.has_value())
    {
        failTransfer(std::move(message.Reply), message.Ticket);
        return;
    }

    const VkCommandBuffer cmd = beginTransfer();
    if (cmd == VK_NULL_HANDLE)
    {
        failTransfer(std::move(message.Reply), message.Ticket);
        return;
    }

    // required, unlike with buffers. forces a layout transition. stages are limited to the ones a transfer queue has
    RecordImageLayoutTransition(cmd, imageHandle, range,
        message.ImageInfo.CurrentLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    vkCmdCopyBufferToImage(
        cmd,
        staging->Buffer,
        imageHandle,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        static_cast<uint32_t>(bufferImageCopies->size()),
        bufferImageCopies->data());
    RecordImageLayoutTransition(cmd, imageHandle, range,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, finalLayout,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE);

    imageDataVector.clear();
    endTransfer(std::move(message.Reply), std::move(staging), message.Ticket);
}

void ResourceTransferSystem::processMessage(TransferSystemFillBufferMessage&& message)
{
    const VkCommandBuffer cmd = beginTransfer();
    if (cmd == VK_NULL_HANDLE)
    {
        failTransfer(std::move(message.Reply), message.Ticket);
        return;
    }

    vkCmdFillBuffer(cmd, message.BufferInfo.BufferHandle, message.Offset, message.Size, message.Value);
    endTransfer(std::move(message.Reply), nullptr, message.Ticket);
}

void ResourceTransferSystem::processMessage(TransferSystemCopyBufferToBufferMessage&& message)
{
    // the context worker checked that the source fits in the destination
    const VkBufferCopy copy
    {
        0u,
        0u,
        message.SrcBufferInfo.CreateInfo.Size
    };

    const VkCommandBuffer cmd = beginTransfer();
    if (cmd == VK_NULL_HANDLE)
    {
        failTransfer(std::move(message.Reply), message.Ticket);
        return;
    }

    vkCmdCopyBuffer(cmd, message.SrcBufferInfo.BufferHandle, message.DstBufferInfo.BufferHandle, 1, &copy);
    endTransfer(std::move(message.Reply), nullptr, message.Ticket);
}

void ResourceTransferSystem::processMessage(TransferSystemCopyImageToImageMessage&& message)
{
    // the context worker checked that the two images have the same shape
    const ImageCreateInfo& srcInfo = message.SrcImageInfo.CreateInfo;
    const ImageCreateInfo& dstInfo = message.DstImageInfo.CreateInfo;
    const VkImage srcHandle = message.SrcImageInfo.ImageHandle;
    const VkImage dstHandle = message.DstImageInfo.ImageHandle;
    const VkImageSubresourceRange srcRange = WholeImageRange(srcInfo);
    const VkImageSubresourceRange dstRange = WholeImageRange(dstInfo);

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

    const VkCommandBuffer cmd = beginTransfer();
    if (cmd == VK_NULL_HANDLE)
    {
        failTransfer(std::move(message.Reply), message.Ticket);
        return;
    }

    RecordImageLayoutTransition(cmd, srcHandle, srcRange,
        message.SrcImageInfo.CurrentLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    RecordImageLayoutTransition(cmd, dstHandle, dstRange,
        message.DstImageInfo.CurrentLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
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

    endTransfer(std::move(message.Reply), nullptr, message.Ticket);
}

void ResourceTransferSystem::processMessage(TransferSystemCopyImageToBufferMessage&& message)
{
    const ImageCreateInfo& srcInfo = message.ImageInfo.CreateInfo;
    const VkImage srcHandle = message.ImageInfo.ImageHandle;
    const VkImageSubresourceRange srcRange = WholeImageRange(srcInfo);

    // the mirror of the buffer to image copy: mip 0 of every layer, tightly packed from offset 0. the caller sizes the
    // buffer (width * height * depth * layers * texel size); there is no format size table to check it here
    const VkBufferImageCopy copyParams
    {
        0u,
        0u,
        0u,
        VkImageSubresourceLayers{ srcRange.aspectMask, 0u, 0u, srcInfo.ArrayLayers },
        VkOffset3D{ 0, 0, 0 },
        ImageExtent(srcInfo)
    };

    const VkCommandBuffer cmd = beginTransfer();
    if (cmd == VK_NULL_HANDLE)
    {
        failTransfer(std::move(message.Reply), message.Ticket);
        return;
    }

    RecordImageLayoutTransition(cmd, srcHandle, srcRange,
        message.ImageInfo.CurrentLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    vkCmdCopyImageToBuffer(cmd, srcHandle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, message.BufferInfo.BufferHandle, 1u, &copyParams);
    RecordImageLayoutTransition(cmd, srcHandle, srcRange,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, ImageLayoutAfterTransfer(srcInfo.Usage),
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE);
    // no barrier for the buffer: the host sees the write once the timeline passes the ticket (GPUToCPU memory is
    // host-coherent or invalidated on map)

    endTransfer(std::move(message.Reply), nullptr, message.Ticket);
}

void ResourceTransferSystem::processMessage(TransferSystemCopyBufferToImageMessage&& message)
{
    const ImageCreateInfo& dstInfo = message.ImageInfo.CreateInfo;
    const VkImage dstHandle = message.ImageInfo.ImageHandle;
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

    const VkCommandBuffer cmd = beginTransfer();
    if (cmd == VK_NULL_HANDLE)
    {
        failTransfer(std::move(message.Reply), message.Ticket);
        return;
    }

    RecordImageLayoutTransition(cmd, dstHandle, dstRange,
        message.ImageInfo.CurrentLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    // fixed: the copy named the usage layout instead of TRANSFER_DST_OPTIMAL
    vkCmdCopyBufferToImage(cmd, message.BufferInfo.BufferHandle, dstHandle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &copyParams);
    RecordImageLayoutTransition(cmd, dstHandle, dstRange,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, ImageLayoutAfterTransfer(dstInfo.Usage),
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE);

    endTransfer(std::move(message.Reply), nullptr, message.Ticket);
}

void ResourceTransferSystem::processMessage([[maybe_unused]] TransferSystemExitMessage&& message) noexcept
{
    exitRequested = true;
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
