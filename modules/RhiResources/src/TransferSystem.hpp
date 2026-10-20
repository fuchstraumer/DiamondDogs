#pragma once
#ifndef DIAMOND_DOGS_RESOURCE_TRANSFER_SYSTEM_HPP
#define DIAMOND_DOGS_RESOURCE_TRANSFER_SYSTEM_HPP
#include "ResourceMessageTypesInternal.hpp"
#include "ResourceMessageReply.hpp"
#include "containers/mwsrQueue.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>
#include <thread>

#include <vulkan/vulkan_core.h>
#include <vk_mem_alloc.h>

namespace rhi
{
    class CommandPool;
    class Device;
    class Fence;
}

struct UploadBuffer;

/** @brief Moved from include/ to src/: it includes the internal message types and VMA, and only ResourceContextImpl uses it. */
class ResourceTransferSystem
{
public:

    ResourceTransferSystem(const ResourceTransferSystem&) = delete;
    ResourceTransferSystem& operator=(const ResourceTransferSystem&) = delete;

    ResourceTransferSystem();
    ~ResourceTransferSystem();

    void Initialize(const rhi::Device* _device, VkInstance instance, uint32_t vulkan_api_version);
    // bad, but there are some places we may need to do this - like swapchain resize or device loss events
    void ForceCompleteTransfers();

    void EnqueueTransfer(TransferPayloadType&& payload);

    void StartWorker();
    void StopWorker();

private:

    friend class ResourceContextImpl;

    class TransferCommand
    {
    public:
        TransferCommand(
            const rhi::Device* _device,
            uint32_t queue_family_index,
            VmaAllocator _allocator,
            std::shared_ptr<ResourceTransferReply>&& _reply);

        // not using an upload buffer, is for resources that are already allocated and just need
        // data written to them (so still needs device to create command pool)
        TransferCommand(
            const rhi::Device* _device,
            uint32_t queue_family_index,
            std::shared_ptr<ResourceTransferReply>&& _reply);

        // fixed: this used to throw after a one second wait. a submitted command pool cannot be freed before the GPU
        // finishes with it, so this waits as long as it takes
        ~TransferCommand();

        TransferCommand(const TransferCommand&) = delete;
        TransferCommand& operator=(const TransferCommand&) = delete;

        TransferCommand(TransferCommand&& other) noexcept;
        TransferCommand& operator=(TransferCommand&& other) noexcept;

        [[nodiscard]] VkCommandBuffer CmdBuffer() const noexcept;
        void EndRecording();
        [[nodiscard]] MessageReply::Status WaitForCompletion(uint64_t timeoutNs);
        [[nodiscard]] VkSemaphore Semaphore() const;
        [[nodiscard]] UploadBuffer* GetUploadBuffer() noexcept;

    private:
        void createCommandPool(uint32_t queue_family_index);
        const rhi::Device* device;
        VmaAllocator allocatorHandle;
        std::shared_ptr<ResourceTransferReply> reply;
        std::unique_ptr<rhi::CommandPool> commandPool;
        std::unique_ptr<UploadBuffer> uploadBuffer;
    };

    // worker thread job, pops messages from the queue and processes them
    void workerThreadJob();
    bool processMessages(std::chrono::milliseconds timeout);
    void submitTransferCommands();
    void waitForCommandsToComplete(std::chrono::milliseconds timeout);
    void processRemainingAndWait();

    void processSetBufferDataMessage(TransferSystemSetBufferDataMessage&& message);
    void processSetImageDataMessage(TransferSystemSetImageDataMessage&& message);
    void processFillBufferMessage(TransferSystemFillBufferMessage&& message);
    void processCopyBufferToBufferMessage(TransferSystemCopyBufferToBufferMessage&& message);
    void processCopyImageToImageMessage(TransferSystemCopyImageToImageMessage&& message);
    void processCopyImageToBufferMessage(TransferSystemCopyImageToBufferMessage&& message);
    void processCopyBufferToImageMessage(TransferSystemCopyBufferToImageMessage&& message);

    mwsrQueue<TransferPayloadType> messageQueue;

    void destroy();

    bool initialized = false;

    struct InflightCommandBatch
    {
        // need to move commands that have been submitted out of the class vector, so that we don't
        // add to them during the next batch and accidentally dual-submit commands!
        std::vector<TransferCommand> commands;
        // optional and rarely used, but useful if we have to force wait for some reason or another
        // (potentially swapchain resize or device loss? need to assess more)
        std::unique_ptr<rhi::Fence> fence;
    };
    std::vector<InflightCommandBatch> inflightCommandBatches;

    std::vector<TransferCommand> commands;
    const rhi::Device* device{ nullptr };
    VmaAllocator allocatorHandle{ VK_NULL_HANDLE };
    std::thread workerThread;
    std::atomic<bool> shouldExitWorker{ false };
    // since we may spawn multiple instances of this system, we need to know which queue to submit
    // since splitting up work across queues is part of the benefit of multiple instances! :)
    VkQueue transferQueue{ VK_NULL_HANDLE };
    uint32_t transferQueueFamily{ 0u };
};

#endif //!DIAMOND_DOGS_RESOURCE_TRANSFER_SYSTEM_HPP
