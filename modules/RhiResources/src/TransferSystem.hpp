#pragma once
#ifndef DIAMOND_DOGS_RESOURCE_TRANSFER_SYSTEM_HPP
#define DIAMOND_DOGS_RESOURCE_TRANSFER_SYSTEM_HPP
#include "ResourceMessageTypesInternal.hpp"
#include "ResourceFuture.hpp"
#include "UploadBuffer.hpp"
#include "containers/RingAllocator.hpp"
#include "containers/mwsrQueue.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <vector>
#include <thread>

#include <vulkan/vulkan_core.h>
#include <vk_mem_alloc.h>

namespace rhi
{
    class CommandPool;
    class Device;
    class FutureTable;
}

/** @brief Moved from include/ to src/: it includes the internal message types and VMA, and only ResourceContextImpl uses it. */
enum class ResourceContextError : uint8_t;

/** @brief The transfer system's counts, for tests and profiling. Each is written by one thread with relaxed stores. */
struct TransferSystemCounters
{
    int64_t TransfersRecorded{ 0 };
    int64_t TransfersFailed{ 0 };
    int64_t BatchesSubmitted{ 0 };
    int64_t CompletionReads{ 0 };
    int64_t BytesStaged{ 0 };
    int64_t BlockedPushes{ 0 };
    int64_t StagingRingStalls{ 0 };
    int64_t StagingDedicatedUploads{ 0 };
    int64_t StagingRingBytesInUse{ 0 };
};

class ResourceTransferSystem
{
public:

    ResourceTransferSystem(const ResourceTransferSystem&) = delete;
    ResourceTransferSystem& operator=(const ResourceTransferSystem&) = delete;
    ResourceTransferSystem& operator=(ResourceTransferSystem&& other) noexcept = delete;
    ResourceTransferSystem(ResourceTransferSystem&& other) noexcept = delete;
    ResourceTransferSystem();
    ~ResourceTransferSystem();

    /** @brief `context_queue` receives a TransferProgressMessage each time the completed ticket moves. The transfer
     *  thread writes the final state of each transfer's future into `future_table`. */
    [[nodiscard]] ResourceContextError Initialize(
        const rhi::Device* _device,
        VkInstance instance,
        uint32_t vulkan_api_version,
        mwsrQueue<ResourceMessagePayloadType>* context_queue,
        rhi::FutureTable* future_table,
        uint64_t staging_ring_size);

    /** @brief Only the context worker calls this, so the tickets in the queue rise in queue order. */
    void EnqueueTransfer(TransferPayloadType&& payload);

    /** @brief The timeline semaphore that every batch signals, as a VkSemaphore cast to uint64_t. */
    [[nodiscard]] uint64_t TimelineHandle() const noexcept;
    /** @brief The highest ticket that the GPU finished. Every ticket at or below it is complete or failed. */
    [[nodiscard]] uint64_t CompletedTicket() const noexcept;
    [[nodiscard]] TransferSystemCounters Counters() const noexcept;

private:

    friend class ResourceContextImpl;

    // one command buffer per batch, reused. a batch returns to the free list when the timeline passes its value
    struct TransferBatch
    {
        std::unique_ptr<rhi::CommandPool> Pool;
        uint64_t SignalValue{ 0u };
        int32_t TransferCount{ 0 };
        // the future slot of each transfer in the batch
        std::vector<int32_t> Futures;
        std::vector<std::unique_ptr<UploadBuffer>> Staging;
        // the waits of the one transfer that this batch holds. WaitCount is 0 for a batch with no wait
        std::array<rhi::TransferWait, rhi::k_MaxTransferWaits> Waits{};
        int32_t WaitCount{ 0 };
    };

    /** @brief The staging of one transfer: a destination in the ring, or in a dedicated buffer when the ring is too small
     *  for it. A default-constructed grant means that the transfer stages nothing. */
    struct StagingGrant
    {
        StagingDestination Destination;
        uint64_t Size{ 0u };
        std::unique_ptr<UploadBuffer> Dedicated;
    };

    [[nodiscard]] ResourceContextError createStagingRing(uint64_t size);
    void startWorker();
    void stopWorker();
    void workerThreadJob();
    void dispatchMessage(TransferPayloadType&& message);
    void drainMessages();

    /** @brief Returns the recording command buffer, after the barrier that orders this transfer after the ones before
     *  it. Starts a batch when none is recording, and submits a full one first. A transfer with waits goes alone in
     *  its batch: the batch that is open is submitted first, unless it holds no transfer. Returns null when Vulkan fails. */
    [[nodiscard]] VkCommandBuffer beginTransfer(const TransferWaitSet& waits);
    /** @brief Returns the staging for a transfer of `size` bytes, with its offset a multiple of `alignment`. The ring
     *  stages it when it can ever fit, and a dedicated buffer stages it otherwise. When the ring is full, this submits
     *  the recording batch and waits for the oldest batch in flight. Returns nullopt when the size is 0 or Vulkan fails. */
    [[nodiscard]] std::optional<StagingGrant> acquireStaging(uint64_t size, uint64_t alignment, uint64_t ticket);
    [[nodiscard]] std::optional<StagingGrant> createDedicatedStaging(uint64_t size);
    /** @brief Returns a range of the ring. A failed first try polls the timeline. If that does not help, it counts one stall. */
    [[nodiscard]] std::optional<RingRange> allocateFromRing(uint64_t size, uint64_t alignment, uint64_t ticket);
    /** @brief Submits the recording batch and waits until the completed ticket moves. Returns false when nothing is left
     *  to wait for, or when the wait failed. */
    [[nodiscard]] bool waitForRingSpace();
    /** @brief Waits until the timeline reaches the last signaled value. */
    void waitForSignaledTickets();
    void flushStaging(const StagingGrant& staging);
    /** @brief Adds a recorded transfer to the batch. A transfer with waits is submitted at once. */
    void endTransfer(int32_t future_slot, StagingGrant&& staging, uint64_t ticket, const TransferWaitSet& waits);
    /** @brief Fails the future. The ticket still counts as consumed, so the next submit signals it. */
    void failTransfer(int32_t future_slot, uint64_t ticket, rhi::ResourceError error);
    /** @brief Fails every transfer in the recording batch with `error`, and returns the batch to the free list. */
    void failRecordingBatch(uint64_t ticket, rhi::ResourceError error);
    [[nodiscard]] bool startBatch();
    /** @brief Submits the recording batch. With nothing recorded, still submits a signal when a failed transfer
     *  consumed a ticket that no submit covers, so that no wait on that ticket hangs. */
    void submitBatch();
    /** @brief One read of the timeline counter. Retires every batch at or below it, and publishes the value. */
    void pollCompletion();
    void waitForOldestBatch(uint64_t timeout_ns);
    void processRemainingAndWait();
    void destroy();

    // one overload per alternative of TransferPayloadType, so that a message type without a handler fails to compile
    void processMessage(TransferSystemSetBufferDataMessage&& message);
    void processMessage(TransferSystemSetImageDataMessage&& message);
    void processMessage(TransferSystemFillBufferMessage&& message);
    void processMessage(TransferSystemCopyBufferToBufferMessage&& message);
    void processMessage(TransferSystemCopyImageToImageMessage&& message);
    void processMessage(TransferSystemCopyImageToBufferMessage&& message);
    void processMessage(TransferSystemCopyBufferToImageMessage&& message);
    void processMessage(TransferSystemExitMessage&& message) noexcept;

    mwsrQueue<TransferPayloadType> messageQueue;
    mwsrQueue<ResourceMessagePayloadType>* contextQueue{ nullptr };
    rhi::FutureTable* futures{ nullptr };

    bool initialized = false;

    // all of these belong to the transfer thread, and to the destroying thread once it has joined
    std::optional<TransferBatch> recording;
    std::vector<TransferBatch> freeBatches;
    // in signal order, oldest first
    std::vector<TransferBatch> inflightBatches;
    // the staging ring: one persistently mapped buffer, and the allocator that hands out its ranges by ticket
    VkBuffer ringBuffer{ VK_NULL_HANDLE };
    VmaAllocation ringAllocation{ VK_NULL_HANDLE };
    std::span<std::byte> ringMapped;
    std::optional<RingAllocator<uint64_t>> ring;
    uint64_t highestConsumedTicket{ 0u };
    uint64_t lastSignaledValue{ 0u };
    bool exitRequested{ false };

    // the one value other threads read. written by the transfer thread with release
    std::atomic<uint64_t> completedTicket{ 0u };

    std::atomic<int64_t> transfersRecorded{ 0 };
    std::atomic<int64_t> transfersFailed{ 0 };
    std::atomic<int64_t> batchesSubmitted{ 0 };
    std::atomic<int64_t> completionReads{ 0 };
    std::atomic<int64_t> bytesStaged{ 0 };
    std::atomic<int64_t> ringStalls{ 0 };
    std::atomic<int64_t> dedicatedUploads{ 0 };
    std::atomic<int64_t> ringBytesInUse{ 0 };

    const rhi::Device* device{ nullptr };
    VmaAllocator allocatorHandle{ VK_NULL_HANDLE };
    VkSemaphore timeline{ VK_NULL_HANDLE };
    std::thread workerThread;
    // since we may spawn multiple instances of this system, we need to know which queue to submit
    // since splitting up work across queues is part of the benefit of multiple instances! :)
    VkQueue transferQueue{ VK_NULL_HANDLE };
    uint32_t transferQueueFamily{ 0u };
};

#endif //!DIAMOND_DOGS_RESOURCE_TRANSFER_SYSTEM_HPP
