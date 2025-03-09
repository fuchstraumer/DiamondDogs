#pragma once
#ifndef DIAMOND_DOGS_RESOURCE_TRANSFER_SYSTEM_HPP
#define DIAMOND_DOGS_RESOURCE_TRANSFER_SYSTEM_HPP
#include "ForwardDecl.hpp"
#include "containers/mwsrQueue.hpp"
#include "ResourceMessageTypesInternal.hpp"
#include <vulkan/vulkan.h>
#include <memory>
#include <vector>
#include <unordered_set>
#include <vk_mem_alloc.h>

struct VulkanResource;
struct UploadBuffer;
class TransferCommandPool;

class TransferCommand
{
public:
    TransferCommand(const vpr::Device* _device, InternalResourceDataContainer&& _data);
    ~TransferCommand();
    void Execute();
    bool IsComplete() const noexcept;
private:
    std::shared_ptr<ResourceTransferReply> reply;
    VkCommandBuffer commandBuffer;
    VkCommandPool commandPool;
    VmaPool uploadPool;
    std::unique_ptr<UploadBuffer> uploadBuffer;
};

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
    // use a circular buffer to always cap capacity, and this way we can check if if the pool is full - when 
    // it is, we do a blocking wait on the fence to ensure that all the commands have been submitted and completed
    // (then we can reset them and pop from the buffer)
    circular_buffer<TransferCommandPool, 64> commandBuffers;
    bool cmdBufferDirty = false;
    bool initialized = false;
    std::unique_ptr<vpr::CommandPool> transferCmdPool;
    std::vector<std::unique_ptr<UploadBuffer>> uploadBuffers;
    std::vector<VkSemaphore> transferSignalSemaphores;
    std::vector<uint64_t> transferSignalValues;
    std::unique_ptr<vpr::Fence> fence;
    const vpr::Device* device;
    VmaAllocator allocator;
    std::vector<VmaPool> uploadPools;
    VkDeviceSize lastPoolSize{ VkDeviceSize(128e6) };
};

#endif //!DIAMOND_DOGS_RESOURCE_TRANSFER_SYSTEM_HPP
