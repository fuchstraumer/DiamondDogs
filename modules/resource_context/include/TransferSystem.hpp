#pragma once
#ifndef DIAMOND_DOGS_RESOURCE_TRANSFER_SYSTEM_HPP
#define DIAMOND_DOGS_RESOURCE_TRANSFER_SYSTEM_HPP
#include "ForwardDecl.hpp"
#include "ResourceMessageTypesInternal.hpp"
#include "ResourceMessageReply.hpp"
#include "containers/mwsrQueue.hpp"

#include <chrono>
#include <memory>
#include <vector>
#include <thread>

#include <vulkan/vulkan_core.h>
#include <vk_mem_alloc.h>

struct VulkanResource;
struct UploadBuffer;
class TransferCommandPool;

class ResourceTransferSystem
{
public:

    ResourceTransferSystem(const ResourceTransferSystem&) = delete;
    ResourceTransferSystem& operator=(const ResourceTransferSystem&) = delete;

    ResourceTransferSystem();
    ~ResourceTransferSystem();

    void Initialize(const vpr::Device* device);
    // bad, but there are some places we may need to do this - like swapchain resize or device loss events
    void ForceCompleteTransfers();

    void EnqueueTransfer(TransferPayloadType&& payload);

    void SetExitWorker(bool value);
    void StartWorker();
    void StopWorker();

private:
    
    class TransferCommand
    {
    public:
        TransferCommand(
            const vpr::Device* _device,
            VmaAllocator _allocator,
            std::shared_ptr<ResourceTransferReply>&& _reply);

        // not using an upload buffer, is for resources that are already allocated and just need
        // data written to them (so still needs device to create command pool)
        TransferCommand(
            const vpr::Device* _device,
            std::shared_ptr<ResourceTransferReply>&& _reply);
        
        ~TransferCommand();

        TransferCommand(const TransferCommand&) = delete;
        TransferCommand& operator=(const TransferCommand&) = delete;

        TransferCommand(TransferCommand&& other) noexcept;
        TransferCommand& operator=(TransferCommand&& other) noexcept;

        VkCommandBuffer CmdBuffer() const;
        void EndRecording();
        MessageReply::Status WaitForCompletion(uint64_t timeoutNs);
        VkSemaphore Semaphore() const;
        UploadBuffer* GetUploadBuffer() noexcept;

    private:
        void createCommandPool();
        const vpr::Device* device;
        VmaAllocator allocatorHandle;
        std::shared_ptr<ResourceTransferReply> reply;
        std::unique_ptr<vpr::CommandPool> commandPool;
        std::unique_ptr<UploadBuffer> uploadBuffer;
    };

    // worker thread job, pops messages from the queue and processes them
    void workerThreadJob();
    void processMessages(std::chrono::milliseconds timeout);
    void submitTransferCommands();
    void waitForCommandsToComplete(std::chrono::milliseconds timeout);

    template<typename T>
    void processMessage(T&& message);
    template<>
    void processMessage<TransferSystemSetBufferDataMessage>(TransferSystemSetBufferDataMessage&& message);
    template<>
    void processMessage<TransferSystemSetImageDataMessage>(TransferSystemSetImageDataMessage&& message);
    template<>
    void processMessage<TransferSystemFillBufferMessage>(TransferSystemFillBufferMessage&& message);
    template<>
    void processMessage<TransferSystemCopyBufferToBufferMessage>(TransferSystemCopyBufferToBufferMessage&& message);
    template<>
    void processMessage<TransferSystemCopyImageToImageMessage>(TransferSystemCopyImageToImageMessage&& message);
    template<>
    void processMessage<TransferSystemCopyImageToBufferMessage>(TransferSystemCopyImageToBufferMessage&& message);
    template<>
    void processMessage<TransferSystemCopyBufferToImageMessage>(TransferSystemCopyBufferToImageMessage&& message);

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
        std::unique_ptr<vpr::Fence> fence;
    };
    std::vector<InflightCommandBatch> inflightCommandBatches;

    std::vector<TransferCommand> commands;
    const vpr::Device* device;
    VmaAllocator allocatorHandle;
    std::thread workerThread;
    std::atomic<bool> shouldExitWorker;
    // since we may spawn multiple instances of this system, we need to know which queue to submit
    // since splitting up work across queues is part of the benefit of multiple instances! :)
    uint32_t transferQueueIndex;
};

template<>
inline void ResourceTransferSystem::processMessage<TransferSystemSetBufferDataMessage>(TransferSystemSetBufferDataMessage&& message)
{
    processSetBufferDataMessage(std::move(message));
}

template<>
inline void ResourceTransferSystem::processMessage<TransferSystemSetImageDataMessage>(TransferSystemSetImageDataMessage&& message)
{
    processSetImageDataMessage(std::move(message));
}

template<>
inline void ResourceTransferSystem::processMessage<TransferSystemFillBufferMessage>(TransferSystemFillBufferMessage&& message)
{
    processFillBufferMessage(std::move(message));
}

template<>
inline void ResourceTransferSystem::processMessage<TransferSystemCopyBufferToBufferMessage>(TransferSystemCopyBufferToBufferMessage&& message)
{
    processCopyBufferToBufferMessage(std::move(message));
}

template<>
inline void ResourceTransferSystem::processMessage<TransferSystemCopyImageToImageMessage>(TransferSystemCopyImageToImageMessage&& message)
{
    processCopyImageToImageMessage(std::move(message));
}

template<>
inline void ResourceTransferSystem::processMessage<TransferSystemCopyImageToBufferMessage>(TransferSystemCopyImageToBufferMessage&& message)
{
    processCopyImageToBufferMessage(std::move(message));
}

template<>
inline void ResourceTransferSystem::processMessage<TransferSystemCopyBufferToImageMessage>(TransferSystemCopyBufferToImageMessage&& message)
{
    processCopyBufferToImageMessage(std::move(message));
}

#endif //!DIAMOND_DOGS_RESOURCE_TRANSFER_SYSTEM_HPP
