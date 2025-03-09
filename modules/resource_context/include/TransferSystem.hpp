#pragma once
#ifndef DIAMOND_DOGS_RESOURCE_TRANSFER_SYSTEM_HPP
#define DIAMOND_DOGS_RESOURCE_TRANSFER_SYSTEM_HPP
#include "ForwardDecl.hpp"
#include "ResourceMessageTypesInternal.hpp"
#include "containers/mwsrQueue.hpp"
#include <vulkan/vulkan_core.h>
#include <memory>
#include <vector>
#include <vk_mem_alloc.h>
#include <thread>

struct VulkanResource;
struct UploadBuffer;
class TransferCommandPool;

class ResourceTransferSystem
{
public:

    // Specialized classes for managing transfer command buffers and pools because I use RAII
    // to manage lifetimes and submission of the commands
    class CommandBuffer;
    class CommandPool;

    ResourceTransferSystem(const ResourceTransferSystem&) = delete;
    ResourceTransferSystem& operator=(const ResourceTransferSystem&) = delete;

    ResourceTransferSystem();
    ~ResourceTransferSystem();

    void Initialize(const vpr::Device* device);
    // bad, but there are some places we may need to do this - like swapchain resize or device loss events
    void ForceCompleteTransfers();

    void EnqueueTransfer(TransferPayloadType&& payload);


private:
    
    class TransferCommand
    {
    public:
        TransferCommand(
            const vpr::Device* _device,
            InternalResourceDataContainer&& _data,
            std::shared_ptr<ResourceTransferReply>&& _reply);
        
        ~TransferCommand();

        TransferCommand(const TransferCommand&) = delete;
        TransferCommand& operator=(const TransferCommand&) = delete;

        TransferCommand(TransferCommand&& other) noexcept;
        TransferCommand& operator=(TransferCommand&& other) noexcept;

        const vpr::Device* device;
        std::shared_ptr<ResourceTransferReply> reply;
        VkCommandBuffer commandBuffer;
        VkCommandPool commandPool;
        VmaPool uploadPool;
        std::unique_ptr<UploadBuffer> uploadBuffer;
    };

    // worker thread job, pops messages from the queue and processes them
    void processMessages();

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
    void setBufferDataHostOnly(TransferSystemSetBufferDataMessage&& message);
    void setBufferDataUploadBuffer(TransferSystemSetBufferDataMessage&& message);
    void processSetImageDataMessage(TransferSystemSetImageDataMessage&& message);
    void processFillBufferMessage(TransferSystemFillBufferMessage&& message);
    void processCopyBufferToBufferMessage(TransferSystemCopyBufferToBufferMessage&& message);
    void processCopyImageToImageMessage(TransferSystemCopyImageToImageMessage&& message);
    void processCopyImageToBufferMessage(TransferSystemCopyImageToBufferMessage&& message);
    void processCopyBufferToImageMessage(TransferSystemCopyBufferToImageMessage&& message);

    mwsrQueue<TransferPayloadType> transferQueue;

    class CommandPool
    {
    public:
        CommandPool();
        ~CommandPool();
        std::vector<CommandBuffer> commandBuffers;
        std::unique_ptr<vpr::CommandPool> transferCmdPool;
        VmaPool uploadPool;
    };

    void destroy();

    VmaPool createPool();
    std::unique_ptr<UploadBuffer> createUploadBufferImpl(const size_t buffer_sz);
    void flushTransfersIfNeeded();
    
    bool cmdBufferDirty = false;
    bool initialized = false;

    std::vector<TransferCommand> commands;
    std::unique_ptr<vpr::CommandPool> transferCmdPool;
    std::vector<std::unique_ptr<UploadBuffer>> uploadBuffers;
    std::unique_ptr<vpr::Fence> fence;
    const vpr::Device* device;
    VmaAllocator allocatorHandle;
    std::vector<VmaPool> uploadPools;
    VkDeviceSize lastPoolSize{ VkDeviceSize(128e6) };
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
