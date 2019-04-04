#include "TransferSystem.hpp"
#include "CommandPool.hpp"
#include "LogicalDevice.hpp"
#include "Fence.hpp"
#include "Semaphore.hpp"
#include "vkAssert.hpp"
#include "Allocator.hpp"
#include "UploadBuffer.hpp"

constexpr static VkBufferCreateInfo staging_buffer_create_info{
	VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
	nullptr,
	0,
	4096u,
	VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
	VK_SHARING_MODE_EXCLUSIVE,
	0,
	nullptr
};

constexpr static VmaAllocationCreateInfo alloc_create_info {
	0,
	VMA_MEMORY_USAGE_CPU_ONLY,
	VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
	0u,
	UINT32_MAX,
	VK_NULL_HANDLE,
	nullptr
};

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

UploadBuffer* ResourceTransferSystem::CreateUploadBuffer(size_t buffer_sz) {
    uploadBuffers.emplace_back(std::make_unique<UploadBuffer>(device, allocator, uploadPool, buffer_sz));
    return uploadBuffers.back().get();
}

ResourceTransferSystem::ResourceTransferSystem() : transferCmdPool(nullptr), device(nullptr), fence(nullptr) {}

ResourceTransferSystem::~ResourceTransferSystem() {}

void ResourceTransferSystem::Initialize(const vpr::Device * dvc, VmaAllocator _allocator) {

    if (initialized) {
        return;
    }

    device = dvc;
    allocator = _allocator;

	uint32_t memory_type_index{ 0u };
	VkResult result = vmaFindMemoryTypeIndexForBufferInfo(allocator, &staging_buffer_create_info, &alloc_create_info, &memory_type_index);
	VkAssert(result);

	const VmaPoolCreateInfo pool_info {
		memory_type_index,
		VMA_POOL_CREATE_IGNORE_BUFFER_IMAGE_GRANULARITY_BIT | VMA_POOL_CREATE_LINEAR_ALGORITHM_BIT,
		(3276800u / 10u), // block size
		0u, // min block count
		0u, // max block count
		0u
	};

	result = vmaCreatePool(allocator, &pool_info, &uploadPool);
	VkAssert(result);
    
    transferCmdPool = std::make_unique<vpr::CommandPool>(dvc->vkHandle(), getCreateInfo(dvc));
	transferCmdPool->AllocateCmdBuffers(1, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    fence = std::make_unique<vpr::Fence>(dvc->vkHandle(), 0); 
    
    constexpr static VkCommandBufferBeginInfo begin_info{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        nullptr,
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
        nullptr
    };

    result = vkBeginCommandBuffer(transferCmdPool->GetCmdBuffer(0), &begin_info);
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

    auto& pool = *transferCmdPool;
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

	transferCmdPool->ResetCmdBuffer(0);

    constexpr static VkCommandBufferBeginInfo begin_info{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        nullptr,
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
        nullptr
    };

    result = vkBeginCommandBuffer(pool[0], &begin_info);
    VkAssert(result);
    cmdBufferDirty = false;

    if (uploadBuffers.empty()) {
        return;
    }

    for (auto& buff : uploadBuffers) {
		vmaDestroyBuffer(allocator, buff->Buffer, buff->Allocation);
        buff.reset();
    }

    uploadBuffers.clear(); 
    uploadBuffers.shrink_to_fit();

}

ResourceTransferSystem::transferSpinLockGuard ResourceTransferSystem::AcquireSpinLock() {
    return ResourceTransferSystem::transferSpinLockGuard(copyQueueLock);
}

VkCommandBuffer ResourceTransferSystem::TransferCmdBuffer() {
    cmdBufferDirty = true;
    auto& pool = *transferCmdPool;
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
