#include "TransferSystem.hpp"
#include "CommandPool.hpp"
#include "LogicalDevice.hpp"
#include "Fence.hpp"
#include "Semaphore.hpp"
#include "vkAssert.hpp"

VkCommandPoolCreateInfo getCreateInfo(const vpr::Device* device) {
    constexpr static VkCommandPoolCreateInfo pool_info{
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        nullptr,
        VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        0
    };
    VkCommandPoolCreateInfo result = pool_info;
    result.queueFamilyIndex = device->QueueFamilyIndices().Transfer;
    return result;
}

UploadBuffer* ResourceTransferSystem::CreateUploadBuffer(vpr::Allocator * alloc, size_t buffer_sz) {
    auto guard = AcquireSpinLock();
    uploadBuffers.emplace_back(std::make_unique<UploadBuffer>(device, alloc, buffer_sz));
    return uploadBuffers.back().get();
}

ResourceTransferSystem::ResourceTransferSystem() : transferPool(nullptr), device(nullptr), fence(nullptr) {}

ResourceTransferSystem::~ResourceTransferSystem() {}

void ResourceTransferSystem::Initialize(const vpr::Device * dvc) {

    if (initialized) {
        return;
    }

    device = dvc;
    transferPool = std::make_unique<vpr::CommandPool>(dvc->vkHandle(), getCreateInfo(dvc));
    transferPool->AllocateCmdBuffers(1, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    fence = std::make_unique<vpr::Fence>(dvc->vkHandle(), 0); 
    
    constexpr static VkCommandBufferBeginInfo begin_info{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        nullptr,
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
        nullptr
    };

    VkResult result = vkBeginCommandBuffer(transferPool->GetCmdBuffer(0), &begin_info);
    VkAssert(result);
    initialized = true;
}

ResourceTransferSystem & ResourceTransferSystem::GetTransferSystem() {
    static ResourceTransferSystem transfer_system;
    return transfer_system;
}

void ResourceTransferSystem::CompleteTransfers() {
    if (!initialized) {
        throw std::runtime_error("Transfer system was not properly initialized!");
    }

    if (!cmdBufferDirty) {
        return;
    }

    auto guard = AcquireSpinLock();

    auto& pool = *transferPool;
    if (vkEndCommandBuffer(pool[0]) != VK_SUCCESS) {
        throw std::runtime_error("Failed to end Transfer command buffer!");
    }

    VkSubmitInfo submission{
        VK_STRUCTURE_TYPE_SUBMIT_INFO,
        nullptr,
        0,
        nullptr,
        nullptr,
        1,
        &pool[0],
        0,
        nullptr
    };

    VkResult result = vkQueueSubmit(device->TransferQueue(), 1, &submission, fence->vkHandle());
    VkAssert(result);
    result = vkWaitForFences(device->vkHandle(), 1, &fence->vkHandle(), VK_TRUE, UINT64_MAX);
    VkAssert(result);
    result = vkResetFences(device->vkHandle(), 1, &fence->vkHandle());
    VkAssert(result);

    transferPool->ResetCmdBuffer(0);

    constexpr static VkCommandBufferBeginInfo begin_info{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        nullptr,
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
        nullptr
    };

    result = vkBeginCommandBuffer(pool[0], &begin_info);
    VkAssert(result);
    cmdBufferDirty = false;

}

ResourceTransferSystem::transferSpinLockGuard ResourceTransferSystem::AcquireSpinLock() {
    return ResourceTransferSystem::transferSpinLockGuard(copyQueueLock);
}

VkCommandBuffer ResourceTransferSystem::TransferCmdBuffer() {
    cmdBufferDirty = true;
    auto& pool = *transferPool;
    return pool[0];
}

void ResourceTransferSystem::transferSpinLock::lock() {
    while (!try_lock()) {

    }
}

bool ResourceTransferSystem::transferSpinLock::try_lock() {
    return !lockFlag.test_and_set(std::memory_order_acquire);
}

void ResourceTransferSystem::transferSpinLock::unlock() {
    lockFlag.clear(std::memory_order_release);
}

ResourceTransferSystem::transferSpinLockGuard::transferSpinLockGuard(transferSpinLock & _lock) : lck(_lock) {
    lck.lock();
}

ResourceTransferSystem::transferSpinLockGuard::~transferSpinLockGuard() {
    lck.unlock();
}
