#pragma once
#ifndef DIAMOND_DOGS_RESOURCE_TRANSFER_SYSTEM_HPP
#define DIAMOND_DOGS_RESOURCE_TRANSFER_SYSTEM_HPP
#include "ForwardDecl.hpp"
#include <vulkan/vulkan.h>
#include <memory>
#include <atomic>
#include <vector>

struct UploadBuffer;

class ResourceTransferSystem {

    ResourceTransferSystem(const ResourceTransferSystem&) = delete;
    ResourceTransferSystem& operator=(const ResourceTransferSystem&) = delete;

    struct transferSpinLock {
        std::atomic_flag lockFlag{ 0 };
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

    void Initialize(const vpr::Device* device);
    UploadBuffer* CreateUploadBuffer(vpr::Allocator* alloc, size_t buffer_sz);
    void CompleteTransfers();
    transferSpinLockGuard AcquireSpinLock();
    VkCommandBuffer TransferCmdBuffer();

private:

    std::atomic<bool> cmdBufferDirty = false;
    bool initialized = false;
    std::unique_ptr<vpr::CommandPool> transferPool;
    std::vector<std::unique_ptr<UploadBuffer>> uploadBuffers;
    std::unique_ptr<vpr::Fence> fence;
    const vpr::Device* device;

};

#endif //!DIAMOND_DOGS_RESOURCE_TRANSFER_SYSTEM_HPP
