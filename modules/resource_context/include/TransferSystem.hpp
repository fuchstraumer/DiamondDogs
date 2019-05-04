#pragma once
#ifndef DIAMOND_DOGS_RESOURCE_TRANSFER_SYSTEM_HPP
#define DIAMOND_DOGS_RESOURCE_TRANSFER_SYSTEM_HPP
#include "ForwardDecl.hpp"
#include <vulkan/vulkan.h>
#include <memory>
#include <atomic>
#include <mutex>
#include <vector>
#include <vk_mem_alloc.h>

struct UploadBuffer;

class ResourceTransferSystem {

    ResourceTransferSystem(const ResourceTransferSystem&) = delete;
    ResourceTransferSystem& operator=(const ResourceTransferSystem&) = delete;

    struct transferSpinLock {
        std::recursive_mutex lockFlag{};
        void lock();
        bool try_lock();
        void unlock();
    } copyQueueLock;

    struct transferSpinLockGuard {
        transferSpinLock& lck;
        transferSpinLockGuard(transferSpinLock& _lock);
        ~transferSpinLockGuard();
    };

    ResourceTransferSystem();
    ~ResourceTransferSystem();

public:

    static ResourceTransferSystem& GetTransferSystem();

    void Initialize(const vpr::Device* device, VmaAllocator _allocator);
    UploadBuffer* CreateUploadBuffer(const size_t buffer_sz);
    void CompleteTransfers();
    transferSpinLockGuard AcquireSpinLock();
    VkCommandBuffer TransferCmdBuffer();

private:

    VmaPool createPool();
    std::unique_ptr<UploadBuffer> createUploadBufferImpl(const size_t buffer_sz);
    void flushTransfersIfNeeded();

    std::atomic<bool> cmdBufferDirty = false;
    bool initialized = false;
    std::unique_ptr<vpr::CommandPool> transferCmdPool;
    std::vector<std::unique_ptr<UploadBuffer>> uploadBuffers;
    std::unique_ptr<vpr::Fence> fence;
    const vpr::Device* device;
	VmaAllocator allocator;
    std::vector<VmaPool> uploadPools;
    VkDeviceSize lastPoolSize{ VkDeviceSize(128e6) };
};

#endif //!DIAMOND_DOGS_RESOURCE_TRANSFER_SYSTEM_HPP
