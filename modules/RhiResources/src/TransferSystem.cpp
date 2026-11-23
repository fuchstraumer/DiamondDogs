#include "TransferSystem.hpp"
#include "DebugUtilFns.hpp"
#include "ImageDataFormatsVulkan.hpp"
#include "ResourceContext.hpp"
#include "CommandPool.hpp"
#include "Device.hpp"
#include "FutureTable.hpp"
#include "ResourceFuture.hpp"
#include "ResourceMessageTypesInternal.hpp"
#include "ResourceTypes.hpp"
#include "RhiResult.hpp"
#include "RhiSystem.hpp"
#include "UploadBuffer.hpp"
#include "VulkanConversions.hpp"
#include "containers/RingAllocator.hpp"
#include "containers/SmallVector.hpp"
#include "containers/mwsrQueue.hpp"
#include "vulkan/vulkan_core.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <thread>
#include <tuple>
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
    // the ranges that the ring can hold at once. every transfer in a batch holds one until its ticket completes
    constexpr int32_t k_MaxRingRanges = 2 * k_BatchCount * k_MaxTransfersPerBatch;
    // the buffer offset alignment of a buffer upload. buffer copies need none, and 16 keeps the memcpy of each range aligned
    constexpr uint64_t k_BufferStagingAlignment = 16u;

    void RecordTransferOrderingBarrier(VkCommandBuffer cmd) noexcept;
    /** @brief Records one barrier for each range, all in one command. A call with more than k_MaxTransferRegions ranges
     *  records them in several commands. The ranges must not name one subresource twice. */
    void RecordImageLayoutTransitions(
        VkCommandBuffer cmd,
        VkImage image,
        std::span<const VkImageSubresourceRange> ranges,
        VkImageLayout old_layout,
        VkImageLayout new_layout,
        VkPipelineStageFlags2 src_stage,
        VkAccessFlags2 src_access,
        VkPipelineStageFlags2 dst_stage,
        VkAccessFlags2 dst_access) noexcept;
    /** @brief Returns every aspect that the format has. A barrier names all of them. */
    [[nodiscard]] VkImageAspectFlags AspectMaskForImage(const ImageCreateInfo& create_info) noexcept;
    /** @brief Returns the one aspect that a copy between a buffer and the image can use: the depth aspect when the image
     *  has depth and stencil. */
    [[nodiscard]] VkImageAspectFlags BufferCopyAspect(VkImageAspectFlags image_aspect) noexcept;
    [[nodiscard]] VkImageSubresourceRange TouchedRangeOf(VkImageAspectFlags aspect, int32_t mip_level, int32_t base_layer, int32_t layer_count) noexcept;
    /** @brief Sorts the ranges, and merges the ones that share a mip level and have layers that overlap or touch. The
     *  merged ranges are written to the start of `ranges`. Returns how many there are. Allocates nothing. */
    [[nodiscard]] int32_t MergeTouchedRanges(std::span<VkImageSubresourceRange> ranges) noexcept;
    [[nodiscard]] VkBufferImageCopy ToVkBufferImageCopy(const BufferImageRegion& region, VkImageAspectFlags aspect) noexcept;
    [[nodiscard]] VkImageCopy ToVkImageCopy(const ImageCopyRegion& region, VkImageAspectFlags src_aspect, VkImageAspectFlags dst_aspect) noexcept;
    /** @brief Writes one wait info for each distinct semaphore into `out`, with the largest value that any wait gives
     *  for it, and returns the count. A timeline that two waits name needs only the larger value. */
    [[nodiscard]] uint32_t MergeWaits(
        const std::array<rhi::TransferWait, rhi::k_MaxTransferWaits>& waits,
        int32_t wait_count,
        std::array<VkSemaphoreSubmitInfo, rhi::k_MaxTransferWaits>& out) noexcept;

    // an op of a set has at most two image sides, and each side is one subresource range
    constexpr size_t k_MaxSetTransitions = 2u * static_cast<size_t>(k_MaxTransferSetOps);

    enum class SetBarrierPhase : uint8_t
    {
        Before,
        After,
    };

    /** @brief One layout transition that a transfer set op needs on one subresource range of an image. */
    struct SetTransition
    {
        uint64_t Image;
        VkImageSubresourceRange Range;
        VkImageLayout OldLayout;
        VkImageLayout NewLayout;
        // true when the op reads the image (TRANSFER_SRC), false when it writes it (TRANSFER_DST)
        bool IsRead;
    };

    /** @brief The arrays that recording one range needs, on the stack. They are not initialized: each function fills
     *  only the entries that it uses. */
    struct SetBarrierScratch
    {
        std::array<SetTransition, k_MaxSetTransitions> Transitions;
        std::array<VkImageSubresourceRange, k_MaxSetTransitions> Ranges;
        std::array<VkImageMemoryBarrier2, k_MaxSetTransitions> Barriers;
    };

    [[nodiscard]] SetTransition MakeSetTransition(VkImage image, const VkImageSubresourceRange& range, VkImageLayout caller_layout, bool is_read, SetBarrierPhase phase) noexcept;
    /** @brief Writes the transitions of the image sides of every op into `out` and returns how many. `out` needs room for
     *  two for each op. */
    [[nodiscard]] size_t CollectSetTransitions(std::span<const ResolvedTransferOp> range_ops, SetBarrierPhase phase, std::span<SetTransition> out) noexcept;
    [[nodiscard]] bool SetTransitionIsBefore(const SetTransition& first, const SetTransition& second) noexcept;
    /** @brief Returns true when the two transitions can share a barrier: one image, the same layouts and the same role. */
    [[nodiscard]] bool SetTransitionsShareBarrier(const SetTransition& first, const SetTransition& second) noexcept;
    [[nodiscard]] VkImageMemoryBarrier2 MakeSetImageBarrier(const SetTransition& transition, const VkImageSubresourceRange& range, SetBarrierPhase phase) noexcept;
    /** @brief Builds one barrier for each group of transitions that share an image, layouts and role, with their ranges
     *  merged. Returns the barriers, which are in `scratch`. Allocates nothing. */
    [[nodiscard]] std::span<const VkImageMemoryBarrier2> BuildSetImageBarriers(std::span<const ResolvedTransferOp> range_ops, SetBarrierPhase phase, SetBarrierScratch& scratch) noexcept;
    /** @brief Records one vkCmdPipelineBarrier2 with the image barriers, and with a memory barrier from transfer writes
     *  to later transfer reads and writes when `include_memory_barrier` is true. */
    void RecordSetDependency(VkCommandBuffer cmd, bool include_memory_barrier, std::span<const VkImageMemoryBarrier2> image_barriers) noexcept;
    void RecordTransferSetOp(VkCommandBuffer cmd, const ResolvedTransferOp& op, const StagingDestination& staging) noexcept;
    void RecordUploadBufferOp(VkCommandBuffer cmd, const ResolvedTransferOp& op, const StagingDestination& staging) noexcept;
    void RecordUploadImageOp(VkCommandBuffer cmd, const ResolvedTransferOp& op, const StagingDestination& staging) noexcept;
    void RecordFillBufferOp(VkCommandBuffer cmd, const ResolvedTransferOp& op) noexcept;
    void RecordCopyBufferOp(VkCommandBuffer cmd, const ResolvedTransferOp& op) noexcept;
    void RecordCopyBufferToImageOp(VkCommandBuffer cmd, const ResolvedTransferOp& op) noexcept;
    void RecordCopyImageToBufferOp(VkCommandBuffer cmd, const ResolvedTransferOp& op) noexcept;
    void RecordCopyImageOp(VkCommandBuffer cmd, const ResolvedTransferOp& op) noexcept;
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
    mwsrQueue<ResourceMessagePayloadType>* context_queue,
    rhi::FutureTable* future_table,
    uint64_t staging_ring_size)
{
    if (initialized)
    {
        return ResourceContextError::AlreadyInitialized;
    }

    device = _device;
    contextQueue = context_queue;
    futures = future_table;

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

    const ResourceContextError ringResult = createStagingRing(staging_ring_size);
    if (ringResult != ResourceContextError::Success)
    {
        destroy();
        return ringResult;
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

    (void)device->SetObjectName(VK_OBJECT_TYPE_SEMAPHORE, reinterpret_cast<uint64_t>(timeline), "ResourceContext_TransferTimeline");

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

        batch.Futures.reserve(k_MaxTransfersPerBatch);
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
        messageQueue.BlockedPushCount(),
        ringStalls.load(std::memory_order_relaxed),
        dedicatedUploads.load(std::memory_order_relaxed),
        ringBytesInUse.load(std::memory_order_relaxed),
        transferSetsRecorded.load(std::memory_order_relaxed),
        barriersRecorded.load(std::memory_order_relaxed)
    };
}

ResourceContextError ResourceTransferSystem::createStagingRing(uint64_t size)
{
    const VkBufferCreateInfo bufferInfo
    {
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0,
        nullptr
    };
    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;

    VmaAllocationInfo resultInfo{};
    const VkResult result = vmaCreateBuffer(allocatorHandle, &bufferInfo, &allocationInfo, &ringBuffer, &ringAllocation, &resultInfo);
    if (result != VK_SUCCESS)
    {
        ringBuffer = VK_NULL_HANDLE;
        ringAllocation = VK_NULL_HANDLE;
        return ResourceContextError::TransferSetupFailed;
    }

    // a ring that is not mapped is useless. destroy() frees the buffer
    if (resultInfo.pMappedData == nullptr)
    {
        return ResourceContextError::TransferSetupFailed;
    }

    std::byte* mappedPtr = static_cast<std::byte*>(resultInfo.pMappedData);
    const size_t mappedSize = static_cast<size_t>(resultInfo.size);

    ringMapped = std::span<std::byte>(mappedPtr, mappedSize);
    ring.emplace(size, k_MaxRingRanges);
    (void)device->SetObjectName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(ringBuffer), "ResourceContext_StagingRing");
    return ResourceContextError::Success;
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

    ring.reset();
    if (ringBuffer != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(allocatorHandle, ringBuffer, ringAllocation);
        ringBuffer = VK_NULL_HANDLE;
        ringAllocation = VK_NULL_HANDLE;
        ringMapped = std::span<std::byte>{};
    }

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
    waitForSignaledTickets();
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

VkCommandBuffer ResourceTransferSystem::beginTransfer(const TransferWaitSet& waits)
{
    const bool batchIsFull = recording.has_value() && recording->TransferCount >= k_MaxTransfersPerBatch;

    // the transfers before a waiting one must not wait with it, so they are submitted now. an empty batch has nothing
    // to submit, and the waiting transfer uses it
    const bool mustSubmitEarlierTransfers = waits.Count > 0 && recording.has_value() && recording->TransferCount > 0;
    if (batchIsFull || mustSubmitEarlierTransfers)
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

std::optional<ResourceTransferSystem::StagingGrant> ResourceTransferSystem::acquireStaging(uint64_t size, uint64_t alignment, uint64_t ticket)
{
    if (size == 0u)
    {
        return std::nullopt;
    }

    // a transfer that can never fit in the ring gets a buffer of its own
    if (size > ring->Capacity())
    {
        return createDedicatedStaging(size);
    }

    const std::optional<RingRange> range = allocateFromRing(size, alignment, ticket);
    if (!range.has_value())
    {
        return std::nullopt;
    }

    StagingGrant grant;
    std::span<std::byte> mappedSubspan = ringMapped.subspan(range->Offset, range->Size);
    grant.Destination = StagingDestination{ ringBuffer, range->Offset, mappedSubspan };
    grant.Size = range->Size;
    return grant;
}

std::optional<ResourceTransferSystem::StagingGrant> ResourceTransferSystem::createDedicatedStaging(uint64_t size)
{
    StagingGrant grant;
    grant.Dedicated = std::make_unique<UploadBuffer>(allocatorHandle);
    if (!grant.Dedicated->Create(size))
    {
        return std::nullopt;
    }

    (void)device->SetObjectName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(grant.Dedicated->Buffer), "TransferSystem_UploadBuffer");
    grant.Destination = grant.Dedicated->Destination();
    grant.Size = size;
    dedicatedUploads.fetch_add(1, std::memory_order_relaxed);
    return grant;
}

std::optional<RingRange> ResourceTransferSystem::allocateFromRing(uint64_t size, uint64_t alignment, uint64_t ticket)
{
    std::optional<RingRange> range = ring->Allocate(size, alignment, ticket);
    if (range.has_value())
    {
        return range;
    }

    // batches that finished on the GPU may not be polled yet. this is not a stall
    pollCompletion();
    range = ring->Allocate(size, alignment, ticket);
    if (range.has_value())
    {
        return range;
    }

    ringStalls.fetch_add(1, std::memory_order_relaxed);

    while (!range.has_value())
    {
        if (!waitForRingSpace())
        {
            // nothing is left to wait for, and a ring this size must have fit the range. this is a defect, or a lost device
            assert(false && "the staging ring is empty and still refuses a range that fits in it");
            return std::nullopt;
        }

        range = ring->Allocate(size, alignment, ticket);
    }

    return range;
}

bool ResourceTransferSystem::waitForRingSpace()
{
    const uint64_t completedBefore = completedTicket.load(std::memory_order_relaxed);

    // the oldest ranges may belong to transfers that are recorded and not submitted
    submitBatch();

    if (!inflightBatches.empty())
    {
        waitForOldestBatch(std::numeric_limits<uint64_t>::max());
    }
    else
    {
        waitForSignaledTickets();
    }

    return completedTicket.load(std::memory_order_relaxed) > completedBefore;
}

void ResourceTransferSystem::waitForSignaledTickets()
{
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

void ResourceTransferSystem::flushStaging(const StagingGrant& staging)
{
    if (staging.Dedicated != nullptr)
    {
        staging.Dedicated->Flush();
        return;
    }

    // the ring memory may not be coherent
    vmaFlushAllocation(allocatorHandle, ringAllocation, staging.Destination.Offset, staging.Size);
}

void ResourceTransferSystem::endTransfer(int32_t future_slot, StagingGrant&& staging, uint64_t ticket, const TransferWaitSet& waits)
{
    bytesStaged.fetch_add(static_cast<int64_t>(staging.Size), std::memory_order_relaxed);

    if (staging.Dedicated != nullptr)
    {
        recording->Staging.push_back(std::move(staging.Dedicated));
    }

    recording->Futures.push_back(future_slot);
    recording->TransferCount += 1;
    highestConsumedTicket = std::max(highestConsumedTicket, ticket);
    transfersRecorded.fetch_add(1, std::memory_order_relaxed);

    // the batch holds this one transfer, because beginTransfer submitted the batch that was open
    if (waits.Count > 0)
    {
        recording->Waits = waits.Items;
        recording->WaitCount = waits.Count;
        submitBatch();
    }
}

void ResourceTransferSystem::failTransfer(int32_t future_slot, uint64_t ticket, rhi::ResourceError error)
{
    futures->Fail(future_slot, error);
    highestConsumedTicket = std::max(highestConsumedTicket, ticket);
    transfersFailed.fetch_add(1, std::memory_order_relaxed);
}

void ResourceTransferSystem::failRecordingBatch(uint64_t ticket, rhi::ResourceError error)
{
    for (const int32_t futureSlot : recording->Futures)
    {
        failTransfer(futureSlot, ticket, error);
    }

    recording->Futures.clear();
    recording->Staging.clear();
    recording->TransferCount = 0;
    freeBatches.push_back(std::move(*recording));
    recording.reset();
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

    batch.WaitCount = 0;

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
    std::array<VkSemaphoreSubmitInfo, rhi::k_MaxTransferWaits> waitInfos{};
    uint32_t waitCount = 0u;

    if (recording.has_value())
    {
        waitCount = MergeWaits(recording->Waits, recording->WaitCount, waitInfos);

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
            failRecordingBatch(signalValue, rhi::ResourceError::RecordingFailed);
            waitCount = 0u;
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
        waitCount,
        waitCount != 0u ? waitInfos.data() : nullptr,
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
            failRecordingBatch(signalValue, rhi::ResourceError::SubmitFailed);
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

    for (const int32_t futureSlot : recording->Futures)
    {
        futures->SetSubmitted(futureSlot);
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
        for (const int32_t futureSlot : batch.Futures)
        {
            futures->Complete(futureSlot);
        }

        batch.Futures.clear();
        batch.Staging.clear();
        batch.TransferCount = 0;
        freeBatches.push_back(std::move(batch));
        inflightBatches.erase(inflightBatches.begin());
    }

    // the timeline passed these tickets, so the GPU is done reading the ranges that were staged for them
    ring->ReleaseThrough(value);
    ringBytesInUse.store(static_cast<int64_t>(ring->UsedBytes()), std::memory_order_relaxed);

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

    std::optional<StagingGrant> staging = acquireStaging(StagedBufferSize(dataVector), k_BufferStagingAlignment, message.Ticket);
    if (!staging.has_value())
    {
        failTransfer(message.Future, message.Ticket, rhi::ResourceError::StagingFailed);
        return;
    }

    const foundation::SmallVector<VkBufferCopy, 16> bufferCopies = WriteBufferData(staging->Destination, dataVector);
    flushStaging(*staging);

    const VkCommandBuffer cmd = beginTransfer(message.Waits);
    if (cmd == VK_NULL_HANDLE)
    {
        failTransfer(message.Future, message.Ticket, rhi::ResourceError::RecordingFailed);
        return;
    }

    vkCmdCopyBuffer(cmd, staging->Destination.Buffer, bufferHandle, static_cast<uint32_t>(bufferCopies.size()), bufferCopies.data());

    // no barrier after the copy: other queues see the write through the transfer timeline, which consumers wait on
    dataVector.clear();
    endTransfer(message.Future, std::move(*staging), message.Ticket, message.Waits);
}

void ResourceTransferSystem::processMessage(TransferSystemSetImageDataMessage&& message)
{
    const ImageCreateInfo& imageInfo = message.ImageInfo.CreateInfo;
    const VkImage imageHandle = message.ImageInfo.ImageHandle;
    const VkImageAspectFlags barrierAspect = AspectMaskForImage(imageInfo);
    const VkImageAspectFlags copyAspect = BufferCopyAspect(barrierAspect);
    const VkImageLayout finalLayout = message.ImageInfo.AfterLayout;

    InternalResourceDataContainer::ImageDataVector& imageDataVector = std::get<InternalResourceDataContainer::ImageDataVector>(message.Data.DataVector);

    // only the subresources that the pieces write change layout. the others can be in use, in any layout
    foundation::SmallVector<VkImageSubresourceRange, 16> touchedRanges(imageDataVector.size());
    for (auto&& [index, piece] : std::views::enumerate(imageDataVector))
    {
        touchedRanges[static_cast<size_t>(index)] = TouchedRangeOf(barrierAspect, piece.MipLevel, piece.ArrayLayer, piece.NumLayers);
    }

    const int32_t touchedCount = MergeTouchedRanges(touchedRanges);
    const std::span<const VkImageSubresourceRange> touched(touchedRanges.data(), static_cast<size_t>(touchedCount));

    const uint64_t stagingAlignment = ImageStagingAlignment(imageInfo.Format);
    std::optional<StagingGrant> staging = acquireStaging(StagedImageSize(imageDataVector, stagingAlignment), stagingAlignment, message.Ticket);
    if (!staging.has_value())
    {
        failTransfer(message.Future, message.Ticket, rhi::ResourceError::StagingFailed);
        return;
    }

    const foundation::SmallVector<VkBufferImageCopy, 16> bufferImageCopies =
        WriteImageData(staging->Destination, imageDataVector, copyAspect, stagingAlignment);
    flushStaging(*staging);

    const VkCommandBuffer cmd = beginTransfer(message.Waits);
    if (cmd == VK_NULL_HANDLE)
    {
        failTransfer(message.Future, message.Ticket, rhi::ResourceError::RecordingFailed);
        return;
    }

    // required, unlike with buffers. forces a layout transition. stages are limited to the ones a transfer queue has
    RecordImageLayoutTransitions(cmd, imageHandle, touched,
        message.ImageInfo.BeforeLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    vkCmdCopyBufferToImage(
        cmd,
        staging->Destination.Buffer,
        imageHandle,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        static_cast<uint32_t>(bufferImageCopies.size()),
        bufferImageCopies.data());
    RecordImageLayoutTransitions(cmd, imageHandle, touched,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, finalLayout,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE);

    imageDataVector.clear();
    endTransfer(message.Future, std::move(*staging), message.Ticket, message.Waits);
}

void ResourceTransferSystem::processMessage(TransferSystemFillBufferMessage&& message)
{
    const VkCommandBuffer cmd = beginTransfer(message.Waits);
    if (cmd == VK_NULL_HANDLE)
    {
        failTransfer(message.Future, message.Ticket, rhi::ResourceError::RecordingFailed);
        return;
    }

    vkCmdFillBuffer(cmd, message.BufferInfo.BufferHandle, message.Offset, message.Size, message.Value);
    endTransfer(message.Future, StagingGrant{}, message.Ticket, message.Waits);
}

void ResourceTransferSystem::processMessage(TransferSystemCopyBufferToBufferMessage&& message)
{
    // the context worker checked every region
    const std::span<const BufferCopyRegion> regions = message.Regions.View();
    std::array<VkBufferCopy, k_MaxTransferRegions> copies{};
    for (auto&& [index, region] : std::views::enumerate(regions))
    {
        copies[static_cast<size_t>(index)] = VkBufferCopy{ region.SourceOffset, region.DestinationOffset, region.Size };
    }

    const VkCommandBuffer cmd = beginTransfer(message.Waits);
    if (cmd == VK_NULL_HANDLE)
    {
        failTransfer(message.Future, message.Ticket, rhi::ResourceError::RecordingFailed);
        return;
    }

    vkCmdCopyBuffer(cmd, message.SrcBufferInfo.BufferHandle, message.DstBufferInfo.BufferHandle, static_cast<uint32_t>(regions.size()), copies.data());
    endTransfer(message.Future, StagingGrant{}, message.Ticket, message.Waits);
}

void ResourceTransferSystem::processMessage(TransferSystemCopyImageToImageMessage&& message)
{
    // the context worker checked every region
    const ImageCreateInfo& srcInfo = message.SrcImageInfo.CreateInfo;
    const ImageCreateInfo& dstInfo = message.DstImageInfo.CreateInfo;
    const VkImage srcHandle = message.SrcImageInfo.ImageHandle;
    const VkImage dstHandle = message.DstImageInfo.ImageHandle;
    const VkImageAspectFlags srcAspect = AspectMaskForImage(srcInfo);
    const VkImageAspectFlags dstAspect = AspectMaskForImage(dstInfo);
    const std::span<const ImageCopyRegion> regions = message.Regions.View();
    const size_t regionCount = regions.size();

    // the first half of the ranges is the source side, and the second half is the destination side
    std::array<VkImageCopy, static_cast<size_t>(k_MaxTransferRegions)> copies{};
    std::array<VkImageSubresourceRange, 2u * static_cast<size_t>(k_MaxTransferRegions)> ranges{};
    for (auto&& [index, region] : std::views::enumerate(regions))
    {
        const size_t regionIndex = static_cast<size_t>(index);
        copies[regionIndex] = ToVkImageCopy(region, srcAspect, dstAspect);
        ranges[regionIndex] = TouchedRangeOf(srcAspect, region.Source.MipLevel, region.Source.BaseArrayLayer, region.Source.LayerCount);
        ranges[regionCount + regionIndex] = TouchedRangeOf(dstAspect, region.Destination.MipLevel, region.Destination.BaseArrayLayer, region.Source.LayerCount);
    }

    const VkImageLayout srcFinal = message.SrcImageInfo.AfterLayout;
    const VkImageLayout dstFinal = message.DstImageInfo.AfterLayout;

    if (srcHandle == dstHandle)
    {
        // one image has one layout during the copy, and the worker made the two usages equal
        const int32_t touchedCount = MergeTouchedRanges(std::span<VkImageSubresourceRange>(ranges.data(), 2u * regionCount));
        const std::span<const VkImageSubresourceRange> touched(ranges.data(), static_cast<size_t>(touchedCount));

        const VkCommandBuffer cmd = beginTransfer(message.Waits);
        if (cmd == VK_NULL_HANDLE)
        {
            failTransfer(message.Future, message.Ticket, rhi::ResourceError::RecordingFailed);
            return;
        }

        RecordImageLayoutTransitions(cmd, srcHandle, touched,
            message.SrcImageInfo.BeforeLayout, VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT);
        vkCmdCopyImage(
            cmd,
            srcHandle, VK_IMAGE_LAYOUT_GENERAL,
            dstHandle, VK_IMAGE_LAYOUT_GENERAL,
            static_cast<uint32_t>(regionCount), copies.data());
        RecordImageLayoutTransitions(cmd, srcHandle, touched,
            VK_IMAGE_LAYOUT_GENERAL, srcFinal,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE);

        endTransfer(message.Future, StagingGrant{}, message.Ticket, message.Waits);
        return;
    }

    const int32_t srcTouchedCount = MergeTouchedRanges(std::span<VkImageSubresourceRange>(ranges.data(), regionCount));
    const int32_t dstTouchedCount = MergeTouchedRanges(std::span<VkImageSubresourceRange>(ranges.data() + regionCount, regionCount));
    const std::span<const VkImageSubresourceRange> srcTouched(ranges.data(), static_cast<size_t>(srcTouchedCount));
    const std::span<const VkImageSubresourceRange> dstTouched(ranges.data() + regionCount, static_cast<size_t>(dstTouchedCount));

    const VkCommandBuffer cmd = beginTransfer(message.Waits);
    if (cmd == VK_NULL_HANDLE)
    {
        failTransfer(message.Future, message.Ticket, rhi::ResourceError::RecordingFailed);
        return;
    }

    RecordImageLayoutTransitions(cmd, srcHandle, srcTouched,
        message.SrcImageInfo.BeforeLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    RecordImageLayoutTransitions(cmd, dstHandle, dstTouched,
        message.DstImageInfo.BeforeLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    // fixed: the copy named the usage layouts (shader read, attachment) instead of the transfer layouts
    vkCmdCopyImage(
        cmd,
        srcHandle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        dstHandle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        static_cast<uint32_t>(regionCount), copies.data());
    RecordImageLayoutTransitions(cmd, srcHandle, srcTouched,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, srcFinal,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE);
    RecordImageLayoutTransitions(cmd, dstHandle, dstTouched,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, dstFinal,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE);

    endTransfer(message.Future, StagingGrant{}, message.Ticket, message.Waits);
}

void ResourceTransferSystem::processMessage(TransferSystemCopyImageToBufferMessage&& message)
{
    // the context worker checked every region
    const ImageCreateInfo& srcInfo = message.ImageInfo.CreateInfo;
    const VkImage srcHandle = message.ImageInfo.ImageHandle;
    const VkImageAspectFlags barrierAspect = AspectMaskForImage(srcInfo);
    const VkImageAspectFlags copyAspect = BufferCopyAspect(barrierAspect);
    const std::span<const BufferImageRegion> regions = message.Regions.View();

    std::array<VkBufferImageCopy, static_cast<size_t>(k_MaxTransferRegions)> copies{};
    std::array<VkImageSubresourceRange, static_cast<size_t>(k_MaxTransferRegions)> ranges{};
    for (auto&& [index, region] : std::views::enumerate(regions))
    {
        const size_t regionIndex = static_cast<size_t>(index);
        copies[regionIndex] = ToVkBufferImageCopy(region, copyAspect);
        ranges[regionIndex] = TouchedRangeOf(barrierAspect, region.Image.MipLevel, region.Image.BaseArrayLayer, region.Image.LayerCount);
    }

    const int32_t touchedCount = MergeTouchedRanges(std::span<VkImageSubresourceRange>(ranges.data(), regions.size()));
    const std::span<const VkImageSubresourceRange> touched(ranges.data(), static_cast<size_t>(touchedCount));

    const VkCommandBuffer cmd = beginTransfer(message.Waits);
    if (cmd == VK_NULL_HANDLE)
    {
        failTransfer(message.Future, message.Ticket, rhi::ResourceError::RecordingFailed);
        return;
    }

    RecordImageLayoutTransitions(cmd, srcHandle, touched,
        message.ImageInfo.BeforeLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    vkCmdCopyImageToBuffer(cmd, srcHandle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, message.BufferInfo.BufferHandle, static_cast<uint32_t>(regions.size()), copies.data());
    RecordImageLayoutTransitions(cmd, srcHandle, touched,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, message.ImageInfo.AfterLayout,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE);
    // no barrier for the buffer: the host sees the write once the timeline passes the ticket (GPUToCPU memory is
    // host-coherent or invalidated on map)

    endTransfer(message.Future, StagingGrant{}, message.Ticket, message.Waits);
}

void ResourceTransferSystem::processMessage(TransferSystemCopyBufferToImageMessage&& message)
{
    // the context worker checked every region
    const ImageCreateInfo& dstInfo = message.ImageInfo.CreateInfo;
    const VkImage dstHandle = message.ImageInfo.ImageHandle;
    const VkImageAspectFlags barrierAspect = AspectMaskForImage(dstInfo);
    const VkImageAspectFlags copyAspect = BufferCopyAspect(barrierAspect);
    const std::span<const BufferImageRegion> regions = message.Regions.View();

    std::array<VkBufferImageCopy, static_cast<size_t>(k_MaxTransferRegions)> copies{};
    std::array<VkImageSubresourceRange, static_cast<size_t>(k_MaxTransferRegions)> ranges{};
    for (auto&& [index, region] : std::views::enumerate(regions))
    {
        const size_t regionIndex = static_cast<size_t>(index);
        copies[regionIndex] = ToVkBufferImageCopy(region, copyAspect);
        ranges[regionIndex] = TouchedRangeOf(barrierAspect, region.Image.MipLevel, region.Image.BaseArrayLayer, region.Image.LayerCount);
    }

    const int32_t touchedCount = MergeTouchedRanges(std::span<VkImageSubresourceRange>(ranges.data(), regions.size()));
    const std::span<const VkImageSubresourceRange> touched(ranges.data(), static_cast<size_t>(touchedCount));

    const VkCommandBuffer cmd = beginTransfer(message.Waits);
    if (cmd == VK_NULL_HANDLE)
    {
        failTransfer(message.Future, message.Ticket, rhi::ResourceError::RecordingFailed);
        return;
    }

    RecordImageLayoutTransitions(cmd, dstHandle, touched,
        message.ImageInfo.BeforeLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    // fixed: the copy named the usage layout instead of TRANSFER_DST_OPTIMAL
    vkCmdCopyBufferToImage(cmd, message.BufferInfo.BufferHandle, dstHandle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(regions.size()), copies.data());
    RecordImageLayoutTransitions(cmd, dstHandle, touched,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, message.ImageInfo.AfterLayout,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE);

    endTransfer(message.Future, StagingGrant{}, message.Ticket, message.Waits);
}

void ResourceTransferSystem::processMessage(TransferSystemTransferSetMessage&& message)
{
    // the data of the set is staged once, and each upload op reads its part by offset. a set with no data stages nothing
    std::optional<StagingGrant> staging;
    if (message.Data.Size > 0u)
    {
        staging = acquireStaging(message.Data.Size, message.StagingAlignment, message.Ticket);
        if (!staging.has_value())
        {
            failTransfer(message.Future, message.Ticket, rhi::ResourceError::StagingFailed);
            return;
        }

        std::memcpy(staging->Destination.Mapped.data(), message.Data.Items.get(), static_cast<size_t>(message.Data.Size));
        flushStaging(*staging);
    }

    const VkCommandBuffer cmd = beginTransfer(message.Waits);
    if (cmd == VK_NULL_HANDLE)
    {
        failTransfer(message.Future, message.Ticket, rhi::ResourceError::RecordingFailed);
        return;
    }

    const StagingDestination destination = staging.has_value() ? staging->Destination : StagingDestination{};
    recordTransferSet(cmd, message, destination);
    transferSetsRecorded.fetch_add(1, std::memory_order_relaxed);

    // the batch counts the whole set as one transfer
    endTransfer(message.Future, staging.has_value() ? std::move(*staging) : StagingGrant{}, message.Ticket, message.Waits);
}

void ResourceTransferSystem::recordTransferSet(VkCommandBuffer cmd, const TransferSystemTransferSetMessage& message, const StagingDestination& staging)
{
    const std::span<const ResolvedTransferOp> ops(message.Ops.get(), static_cast<size_t>(message.OpCount));
    const std::span<const int32_t> rangeEnds = message.Set.RangeEnds();
    const int32_t rangeCount = static_cast<int32_t>(rangeEnds.size());
    int32_t rangeStart = 0;

    for (int32_t rangeIndex = 0; rangeIndex < rangeCount; ++rangeIndex)
    {
        const int32_t rangeEnd = rangeEnds[static_cast<size_t>(rangeIndex)];
        const std::span<const ResolvedTransferOp> rangeOps = ops.subspan(static_cast<size_t>(rangeStart), static_cast<size_t>(rangeEnd - rangeStart));
        recordTransferSetRange(cmd, rangeOps, staging, rangeIndex == rangeCount - 1);
        rangeStart = rangeEnd;
    }
}

void ResourceTransferSystem::recordTransferSetRange(VkCommandBuffer cmd, std::span<const ResolvedTransferOp> range_ops, const StagingDestination& staging, bool is_last_range)
{
    SetBarrierScratch scratch;

    // a range with no image to transition needs no barrier before it: the barrier after the range before it (or the
    // ordering barrier, for the first range) already made its writes visible
    const std::span<const VkImageMemoryBarrier2> beforeBarriers = BuildSetImageBarriers(range_ops, SetBarrierPhase::Before, scratch);
    if (!beforeBarriers.empty())
    {
        RecordSetDependency(cmd, false, beforeBarriers);
        barriersRecorded.fetch_add(1, std::memory_order_relaxed);
    }

    for (const ResolvedTransferOp& op : range_ops)
    {
        RecordTransferSetOp(cmd, op, staging);
    }

    // the next range reads what this range wrote, so the writes must be visible. the last range needs no memory
    // barrier: the ordering barrier of the next transfer covers it
    const bool includeMemoryBarrier = !is_last_range;
    const std::span<const VkImageMemoryBarrier2> afterBarriers = BuildSetImageBarriers(range_ops, SetBarrierPhase::After, scratch);
    if (!afterBarriers.empty() || includeMemoryBarrier)
    {
        RecordSetDependency(cmd, includeMemoryBarrier, afterBarriers);
        barriersRecorded.fetch_add(1, std::memory_order_relaxed);
    }
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

    void RecordImageLayoutTransitions(
        VkCommandBuffer cmd,
        VkImage image,
        std::span<const VkImageSubresourceRange> ranges,
        VkImageLayout old_layout,
        VkImageLayout new_layout,
        VkPipelineStageFlags2 src_stage,
        VkAccessFlags2 src_access,
        VkPipelineStageFlags2 dst_stage,
        VkAccessFlags2 dst_access) noexcept
    {
        std::array<VkImageMemoryBarrier2, static_cast<size_t>(k_MaxTransferRegions)> barriers{};

        for (size_t first = 0u; first < ranges.size(); first += barriers.size())
        {
            const size_t count = std::min(barriers.size(), ranges.size() - first);
            for (size_t index = 0u; index < count; ++index)
            {
                barriers[index] = VkImageMemoryBarrier2
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
                    ranges[first + index]
                };
            }

            const VkDependencyInfo dependency{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr, 0, 0, nullptr, 0, nullptr, static_cast<uint32_t>(count), barriers.data() };
            vkCmdPipelineBarrier2(cmd, &dependency);
        }
    }

    VkImageAspectFlags AspectMaskForImage(const ImageCreateInfo& create_info) noexcept
    {
        return ToVkImageAspect(AspectFromVkFormat(ToVkFormat(create_info.Format)));
    }

    VkImageAspectFlags BufferCopyAspect(VkImageAspectFlags image_aspect) noexcept
    {
        const bool hasDepthAndStencil = (image_aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0u && (image_aspect & VK_IMAGE_ASPECT_STENCIL_BIT) != 0u;
        return hasDepthAndStencil ? VK_IMAGE_ASPECT_DEPTH_BIT : image_aspect;
    }

    VkImageSubresourceRange TouchedRangeOf(VkImageAspectFlags aspect, int32_t mip_level, int32_t base_layer, int32_t layer_count) noexcept
    {
        return VkImageSubresourceRange{ aspect, static_cast<uint32_t>(mip_level), 1u, static_cast<uint32_t>(base_layer), static_cast<uint32_t>(layer_count) };
    }

    int32_t MergeTouchedRanges(std::span<VkImageSubresourceRange> ranges) noexcept
    {
        if (ranges.empty())
        {
            return 0;
        }

        const auto isBefore = [](const VkImageSubresourceRange& first, const VkImageSubresourceRange& second)
        {
            return first.baseMipLevel != second.baseMipLevel ? first.baseMipLevel < second.baseMipLevel : first.baseArrayLayer < second.baseArrayLayer;
        };
        std::ranges::sort(ranges, isBefore);

        size_t mergedCount = 0u;
        for (const VkImageSubresourceRange& range : ranges.subspan(1u))
        {
            VkImageSubresourceRange& open = ranges[mergedCount];
            const bool sharesMip = open.baseMipLevel == range.baseMipLevel;
            const bool touchesOpenRange = range.baseArrayLayer <= open.baseArrayLayer + open.layerCount;
            if (!sharesMip || !touchesOpenRange)
            {
                ++mergedCount;
                ranges[mergedCount] = range;
                continue;
            }

            const uint32_t openEnd = open.baseArrayLayer + open.layerCount;
            const uint32_t rangeEnd = range.baseArrayLayer + range.layerCount;
            open.layerCount = std::max(openEnd, rangeEnd) - open.baseArrayLayer;
        }

        return static_cast<int32_t>(mergedCount + 1u);
    }

    VkBufferImageCopy ToVkBufferImageCopy(const BufferImageRegion& region, VkImageAspectFlags aspect) noexcept
    {
        const ImageRegion& image = region.Image;
        return VkBufferImageCopy
        {
            region.BufferOffset,
            static_cast<uint32_t>(region.BufferRowTexels),
            static_cast<uint32_t>(region.BufferImageRows),
            VkImageSubresourceLayers{ aspect, static_cast<uint32_t>(image.MipLevel), static_cast<uint32_t>(image.BaseArrayLayer), static_cast<uint32_t>(image.LayerCount) },
            VkOffset3D{ image.X, image.Y, image.Z },
            VkExtent3D{ static_cast<uint32_t>(image.Width), static_cast<uint32_t>(image.Height), static_cast<uint32_t>(image.Depth) }
        };
    }

    VkImageCopy ToVkImageCopy(const ImageCopyRegion& region, VkImageAspectFlags src_aspect, VkImageAspectFlags dst_aspect) noexcept
    {
        const ImageRegion& source = region.Source;
        const ImageRegion& destination = region.Destination;
        const uint32_t layerCount = static_cast<uint32_t>(source.LayerCount);
        return VkImageCopy
        {
            VkImageSubresourceLayers{ src_aspect, static_cast<uint32_t>(source.MipLevel), static_cast<uint32_t>(source.BaseArrayLayer), layerCount },
            VkOffset3D{ source.X, source.Y, source.Z },
            VkImageSubresourceLayers{ dst_aspect, static_cast<uint32_t>(destination.MipLevel), static_cast<uint32_t>(destination.BaseArrayLayer), layerCount },
            VkOffset3D{ destination.X, destination.Y, destination.Z },
            VkExtent3D{ static_cast<uint32_t>(source.Width), static_cast<uint32_t>(source.Height), static_cast<uint32_t>(source.Depth) }
        };
    }

    uint32_t MergeWaits(
        const std::array<rhi::TransferWait, rhi::k_MaxTransferWaits>& waits,
        int32_t wait_count,
        std::array<VkSemaphoreSubmitInfo, rhi::k_MaxTransferWaits>& out) noexcept
    {
        uint32_t merged = 0u;
        for (int32_t i = 0; i < wait_count; ++i)
        {
            const rhi::TransferWait& wait = waits[static_cast<size_t>(i)];
            const VkSemaphore semaphore = reinterpret_cast<VkSemaphore>(wait.Semaphore.Get());

            bool found = false;
            for (uint32_t j = 0; j < merged; ++j)
            {
                if (out[j].semaphore == semaphore)
                {
                    out[j].value = std::max(out[j].value, wait.Value);
                    found = true;
                }
            }

            if (found)
            {
                continue;
            }

            out[merged] = VkSemaphoreSubmitInfo
            {
                VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                nullptr,
                semaphore,
                wait.Value,
                VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                0u
            };
            ++merged;
        }

        return merged;
    }

    SetTransition MakeSetTransition(VkImage image, const VkImageSubresourceRange& range, VkImageLayout caller_layout, bool is_read, SetBarrierPhase phase) noexcept
    {
        const VkImageLayout transferLayout = is_read ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        const bool isBefore = phase == SetBarrierPhase::Before;
        return SetTransition
        {
            reinterpret_cast<uint64_t>(image),
            range,
            isBefore ? caller_layout : transferLayout,
            isBefore ? transferLayout : caller_layout,
            is_read
        };
    }

    size_t CollectSetTransitions(std::span<const ResolvedTransferOp> range_ops, SetBarrierPhase phase, std::span<SetTransition> out) noexcept
    {
        const bool isBefore = phase == SetBarrierPhase::Before;
        size_t count = 0u;

        for (const ResolvedTransferOp& op : range_ops)
        {
            const bool writesImageFromBuffer = op.Kind == TransferOpKind::UploadImage || op.Kind == TransferOpKind::CopyBufferToImage;
            if (writesImageFromBuffer)
            {
                const ImageRegion& box = op.BufferImage.Image;
                const VkImageSubresourceRange range = TouchedRangeOf(op.DestinationAspect, box.MipLevel, box.BaseArrayLayer, box.LayerCount);
                const VkImageLayout layout = isBefore ? op.DestinationBeforeLayout : op.DestinationAfterLayout;
                out[count] = MakeSetTransition(op.DestinationImage, range, layout, false, phase);
                ++count;
            }

            if (op.Kind == TransferOpKind::CopyImageToBuffer)
            {
                const ImageRegion& box = op.BufferImage.Image;
                const VkImageSubresourceRange range = TouchedRangeOf(op.SourceAspect, box.MipLevel, box.BaseArrayLayer, box.LayerCount);
                const VkImageLayout layout = isBefore ? op.SourceBeforeLayout : op.SourceAfterLayout;
                out[count] = MakeSetTransition(op.SourceImage, range, layout, true, phase);
                ++count;
            }

            if (op.Kind == TransferOpKind::CopyImage)
            {
                const ImageRegion& source = op.Image.Source;
                const ImageRegion& destination = op.Image.Destination;
                const VkImageSubresourceRange sourceRange = TouchedRangeOf(op.SourceAspect, source.MipLevel, source.BaseArrayLayer, source.LayerCount);
                const VkImageSubresourceRange destinationRange = TouchedRangeOf(op.DestinationAspect, destination.MipLevel, destination.BaseArrayLayer, source.LayerCount);
                out[count] = MakeSetTransition(op.SourceImage, sourceRange, isBefore ? op.SourceBeforeLayout : op.SourceAfterLayout, true, phase);
                ++count;
                out[count] = MakeSetTransition(op.DestinationImage, destinationRange, isBefore ? op.DestinationBeforeLayout : op.DestinationAfterLayout, false, phase);
                ++count;
            }
        }

        return count;
    }

    bool SetTransitionIsBefore(const SetTransition& first, const SetTransition& second) noexcept
    {
        const auto firstKey = std::tie(first.Image, first.OldLayout, first.NewLayout, first.IsRead, first.Range.baseMipLevel, first.Range.baseArrayLayer);
        const auto secondKey = std::tie(second.Image, second.OldLayout, second.NewLayout, second.IsRead, second.Range.baseMipLevel, second.Range.baseArrayLayer);
        return firstKey < secondKey;
    }

    bool SetTransitionsShareBarrier(const SetTransition& first, const SetTransition& second) noexcept
    {
        return first.Image == second.Image && first.OldLayout == second.OldLayout && first.NewLayout == second.NewLayout && first.IsRead == second.IsRead;
    }

    VkImageMemoryBarrier2 MakeSetImageBarrier(const SetTransition& transition, const VkImageSubresourceRange& range, SetBarrierPhase phase) noexcept
    {
        // the stages and accesses are the ones of the single transfers. stages are limited to the ones a transfer queue has
        const bool isBefore = phase == SetBarrierPhase::Before;
        const VkAccessFlags2 beforeDestinationAccess = transition.IsRead ? VK_ACCESS_2_TRANSFER_READ_BIT : VK_ACCESS_2_TRANSFER_WRITE_BIT;
        const VkAccessFlags2 afterSourceAccess = transition.IsRead ? VK_ACCESS_2_NONE : VK_ACCESS_2_TRANSFER_WRITE_BIT;

        return VkImageMemoryBarrier2
        {
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            nullptr,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
            isBefore ? VK_ACCESS_2_TRANSFER_WRITE_BIT : afterSourceAccess,
            isBefore ? VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            isBefore ? beforeDestinationAccess : VK_ACCESS_2_NONE,
            transition.OldLayout,
            transition.NewLayout,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            reinterpret_cast<VkImage>(transition.Image),
            range
        };
    }

    std::span<const VkImageMemoryBarrier2> BuildSetImageBarriers(std::span<const ResolvedTransferOp> range_ops, SetBarrierPhase phase, SetBarrierScratch& scratch) noexcept
    {
        const size_t transitionCount = CollectSetTransitions(range_ops, phase, scratch.Transitions);
        const std::span<SetTransition> transitions(scratch.Transitions.data(), transitionCount);
        std::ranges::sort(transitions, SetTransitionIsBefore);

        for (size_t index = 0u; index < transitionCount; ++index)
        {
            scratch.Ranges[index] = transitions[index].Range;
        }

        size_t barrierCount = 0u;
        size_t groupStart = 0u;
        while (groupStart < transitionCount)
        {
            size_t groupEnd = groupStart + 1u;
            while (groupEnd < transitionCount && SetTransitionsShareBarrier(transitions[groupStart], transitions[groupEnd]))
            {
                ++groupEnd;
            }

            // ranges of one image that touch merge, so that no subresource is in two barriers of one call
            const std::span<VkImageSubresourceRange> groupRanges(scratch.Ranges.data() + groupStart, groupEnd - groupStart);
            const size_t mergedCount = static_cast<size_t>(MergeTouchedRanges(groupRanges));
            for (size_t mergedIndex = 0u; mergedIndex < mergedCount; ++mergedIndex)
            {
                scratch.Barriers[barrierCount] = MakeSetImageBarrier(transitions[groupStart], groupRanges[mergedIndex], phase);
                ++barrierCount;
            }

            groupStart = groupEnd;
        }

        return std::span<const VkImageMemoryBarrier2>(scratch.Barriers.data(), barrierCount);
    }

    void RecordSetDependency(VkCommandBuffer cmd, bool include_memory_barrier, std::span<const VkImageMemoryBarrier2> image_barriers) noexcept
    {
        const VkMemoryBarrier2 memoryBarrier
        {
            VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            nullptr,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT
        };
        const VkDependencyInfo dependency
        {
            VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            nullptr,
            0,
            include_memory_barrier ? 1u : 0u,
            include_memory_barrier ? &memoryBarrier : nullptr,
            0,
            nullptr,
            static_cast<uint32_t>(image_barriers.size()),
            image_barriers.data()
        };
        vkCmdPipelineBarrier2(cmd, &dependency);
    }

    void RecordTransferSetOp(VkCommandBuffer cmd, const ResolvedTransferOp& op, const StagingDestination& staging) noexcept
    {
        switch (op.Kind)
        {
        case TransferOpKind::UploadBuffer:
            RecordUploadBufferOp(cmd, op, staging);
            break;
        case TransferOpKind::UploadImage:
            RecordUploadImageOp(cmd, op, staging);
            break;
        case TransferOpKind::FillBuffer:
            RecordFillBufferOp(cmd, op);
            break;
        case TransferOpKind::CopyBuffer:
            RecordCopyBufferOp(cmd, op);
            break;
        case TransferOpKind::CopyBufferToImage:
            RecordCopyBufferToImageOp(cmd, op);
            break;
        case TransferOpKind::CopyImageToBuffer:
            RecordCopyImageToBufferOp(cmd, op);
            break;
        case TransferOpKind::CopyImage:
            RecordCopyImageOp(cmd, op);
            break;
        case TransferOpKind::Invalid:
            break;
        }
    }

    void RecordUploadBufferOp(VkCommandBuffer cmd, const ResolvedTransferOp& op, const StagingDestination& staging) noexcept
    {
        // the source offset of the op is an offset into the data, which starts at the start of the staging range
        const VkBufferCopy copy{ staging.Offset + op.Buffer.SourceOffset, op.Buffer.DestinationOffset, op.Buffer.Size };
        vkCmdCopyBuffer(cmd, staging.Buffer, op.DestinationBuffer, 1u, &copy);
    }

    void RecordUploadImageOp(VkCommandBuffer cmd, const ResolvedTransferOp& op, const StagingDestination& staging) noexcept
    {
        BufferImageRegion region = op.BufferImage;
        region.BufferOffset += staging.Offset;

        const VkBufferImageCopy copy = ToVkBufferImageCopy(region, BufferCopyAspect(op.DestinationAspect));
        vkCmdCopyBufferToImage(cmd, staging.Buffer, op.DestinationImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &copy);
    }

    void RecordFillBufferOp(VkCommandBuffer cmd, const ResolvedTransferOp& op) noexcept
    {
        vkCmdFillBuffer(cmd, op.DestinationBuffer, op.Buffer.DestinationOffset, op.Buffer.Size, op.FillValue);
    }

    void RecordCopyBufferOp(VkCommandBuffer cmd, const ResolvedTransferOp& op) noexcept
    {
        const VkBufferCopy copy{ op.Buffer.SourceOffset, op.Buffer.DestinationOffset, op.Buffer.Size };
        vkCmdCopyBuffer(cmd, op.SourceBuffer, op.DestinationBuffer, 1u, &copy);
    }

    void RecordCopyBufferToImageOp(VkCommandBuffer cmd, const ResolvedTransferOp& op) noexcept
    {
        const VkBufferImageCopy copy = ToVkBufferImageCopy(op.BufferImage, BufferCopyAspect(op.DestinationAspect));
        vkCmdCopyBufferToImage(cmd, op.SourceBuffer, op.DestinationImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &copy);
    }

    void RecordCopyImageToBufferOp(VkCommandBuffer cmd, const ResolvedTransferOp& op) noexcept
    {
        const VkBufferImageCopy copy = ToVkBufferImageCopy(op.BufferImage, BufferCopyAspect(op.SourceAspect));
        vkCmdCopyImageToBuffer(cmd, op.SourceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, op.DestinationBuffer, 1u, &copy);
    }

    void RecordCopyImageOp(VkCommandBuffer cmd, const ResolvedTransferOp& op) noexcept
    {
        const VkImageCopy copy = ToVkImageCopy(op.Image, op.SourceAspect, op.DestinationAspect);
        vkCmdCopyImage(cmd, op.SourceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, op.DestinationImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &copy);
    }
}
