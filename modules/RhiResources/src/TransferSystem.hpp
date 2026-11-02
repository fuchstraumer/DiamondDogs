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
enum class ResourceContextError : uint8_t;

class ResourceTransferSystem
{
public:

    ResourceTransferSystem(const ResourceTransferSystem&) = delete;
    ResourceTransferSystem& operator=(const ResourceTransferSystem&) = delete;
    ResourceTransferSystem& operator=(ResourceTransferSystem&& other) noexcept = delete;
    ResourceTransferSystem(ResourceTransferSystem&& other) noexcept = delete;
    ResourceTransferSystem();
    ~ResourceTransferSystem();

    [[nodiscard]] ResourceContextError Initialize(const rhi::Device* _device, VkInstance instance, uint32_t vulkan_api_version);

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

        /** @brief Creates the command pool and command buffer, and begins recording. Returns false, and fails the reply,
         *  when Vulkan cannot create them. */
        [[nodiscard]] bool BeginRecording();
        [[nodiscard]] VkCommandBuffer CmdBuffer() const noexcept;
        /** @brief Ends recording. Returns false, and fails the reply, when Vulkan reports an error. */
        [[nodiscard]] bool EndRecording();
        /** @brief Sets the reply to Failed and releases it. Used when the transfer cannot be recorded or submitted. */
        void Fail() noexcept;
        [[nodiscard]] MessageReply::Status WaitForCompletion(uint64_t timeout_ns);
        [[nodiscard]] VkSemaphore Semaphore() const;
        [[nodiscard]] UploadBuffer* GetUploadBuffer() noexcept;

    private:
        const rhi::Device* device;
        uint32_t queueFamilyIndex;
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

    // one overload per alternative of TransferPayloadType, so that a message type without a handler fails to compile
    void processMessage(TransferSystemSetBufferDataMessage&& message);
    void processMessage(TransferSystemSetImageDataMessage&& message);
    void processMessage(TransferSystemFillBufferMessage&& message);
    void processMessage(TransferSystemCopyBufferToBufferMessage&& message);
    void processMessage(TransferSystemCopyImageToImageMessage&& message);
    void processMessage(TransferSystemCopyImageToBufferMessage&& message);
    void processMessage(TransferSystemCopyBufferToImageMessage&& message);

    mwsrQueue<TransferPayloadType> messageQueue;

    void destroy();

    bool initialized = false;

    struct InflightCommandBatch
    {
        // need to move commands that have been submitted out of the class vector, so that we don't
        // add to them during the next batch and accidentally dual-submit commands!
        std::vector<TransferCommand> Commands;
        // optional and rarely used, but useful if we have to force wait for some reason or another
        // (potentially swapchain resize or device loss? need to assess more)
        std::unique_ptr<rhi::Fence> Fence;
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
