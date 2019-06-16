#include "TransferSystem.hpp"
#include "CommandPool.hpp"
#include "LogicalDevice.hpp"
#include "Fence.hpp"
#include "Semaphore.hpp"
#include "vkAssert.hpp"
#include "Allocator.hpp"
#include "UploadBuffer.hpp"
#include "../../rendering_context/include/RenderingContext.hpp"
#include "VkDebugUtils.hpp"
#include <array>
#include "easylogging++.h"

constexpr static size_t MAX_QUEUED_UPLOAD_BUFFERS = 128u;

constexpr static std::array<VkDeviceSize, 4u> stagingPoolSizeRanges{
    (VkDeviceSize)128e6,
    (VkDeviceSize)256e6,
    (VkDeviceSize)512e6,
    (VkDeviceSize)1024e6 // This should not be in anything but builds I use for fun. Could end in disaster, heh
};

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

constexpr static VkDebugUtilsLabelEXT queue_debug_label{
    VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
    nullptr,
    "TransferQueue",
    { 244.0f / 255.0f, 158.0f / 255.0f, 66.0f / 255.0f, 1.0f }
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

void ResourceTransferSystem::flushTransfersIfNeeded()
{
    /*
        A bit of reasoning: 
        - upload pools greater than one means we created a pool specifically to hold one massive allocation. once we
          reach this point, it should be safe to free that as it was created to return an upload buffer, we set it's data, we're done
        - uploadBuffers being greater than 50 just means we have a ton of pending transfers, and we're going to be accumulating a ton
          of RAM usage. flush the pending transfers to free up what memory we can
    */
    if (uploadPools.size() > 2u || uploadBuffers.size() > MAX_QUEUED_UPLOAD_BUFFERS)
    {
        CompleteTransfers();
        LOG(WARNING) << "Had to flush transfers, incurs high cost and indicates overload of transfer system";
    }
}

UploadBuffer* ResourceTransferSystem::CreateUploadBuffer(const size_t buffer_sz, VulkanResource* rsrc) {
    flushTransfersIfNeeded();
    uploadBuffers.emplace_back(createUploadBufferImpl(buffer_sz));
    auto iter = pendingResources.emplace(rsrc);
	if constexpr (VTF_VALIDATION_ENABLED && VTF_USE_DEBUG_INFO)
	{
		const std::string upload_buffer_name = std::string("UploadBuffer") + std::to_string(uploadBuffers.size());
		RenderingContext::SetObjectName(VK_OBJECT_TYPE_BUFFER, (uint64_t)uploadBuffers.back()->Buffer, upload_buffer_name.c_str());
        // if we're in validation mode, might as well check "iter" too. Won't crash, but will log an error
        if (!iter.second)
        {
            LOG(ERROR) << "Resource upload buffer is being created for is already in internal containers! This implies duplication of memory.";
        }
	}
    return uploadBuffers.back().get();
}

std::unique_ptr<UploadBuffer> ResourceTransferSystem::createUploadBufferImpl(const size_t buffer_sz)
{

    std::unique_ptr<UploadBuffer> created_buffer = std::make_unique<UploadBuffer>(device, allocator);

    VmaAllocationCreateInfo alloc_create_info
    {
        0,
        VMA_MEMORY_USAGE_CPU_ONLY,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        0u,
        UINT32_MAX,
        VK_NULL_HANDLE,
        nullptr
    };

    VkResult created_alloc = VK_SUCCESS;

    do {

        VkBufferCreateInfo create_info = staging_buffer_create_info;
        create_info.size = buffer_sz;
        alloc_create_info.pool = uploadPools.back();

        created_alloc = vmaCreateBuffer(allocator, &create_info, &alloc_create_info, &created_buffer->Buffer, &created_buffer->Allocation, nullptr);
        
        if (created_alloc == VK_ERROR_OUT_OF_DEVICE_MEMORY)
        {
            // create a new pool
            if (buffer_sz > lastPoolSize)
            {
                auto next_sz_iter = std::lower_bound(std::begin(stagingPoolSizeRanges), std::end(stagingPoolSizeRanges), buffer_sz);
                if (next_sz_iter == std::end(stagingPoolSizeRanges))
                {
                    throw std::out_of_range("Requested staging buffer size was bigger than maximum staging allocation size!");
                }
                lastPoolSize = *next_sz_iter;
                uploadPools.emplace_back(createPool());
            }
        }

    } while (created_alloc != VK_SUCCESS);

    return std::move(created_buffer);
}

ResourceTransferSystem::ResourceTransferSystem() : transferCmdPool(nullptr), device(nullptr), fence(nullptr) {}

ResourceTransferSystem::~ResourceTransferSystem() {
    CompleteTransfers();
    for (auto pool : uploadPools)
    {
        vmaDestroyPool(allocator, pool);
    }
}

void ResourceTransferSystem::Initialize(const vpr::Device * dvc, VmaAllocator _allocator) {

    if (initialized) {
        return;
    }

    device = dvc;
    allocator = _allocator;

    uploadPools.emplace_back(createPool());
    
    transferCmdPool = std::make_unique<vpr::CommandPool>(dvc->vkHandle(), getCreateInfo(dvc));
	transferCmdPool->AllocateCmdBuffers(1, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    fence = std::make_unique<vpr::Fence>(dvc->vkHandle(), 0); 
    
    constexpr static VkCommandBufferBeginInfo begin_info{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        nullptr,
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
        nullptr
    };

    VkResult result = vkBeginCommandBuffer(transferCmdPool->GetCmdBuffer(0), &begin_info);
    VkAssert(result);
    initialized = true;

    if constexpr (VTF_VALIDATION_ENABLED && VTF_USE_DEBUG_INFO)
    {
        device->DebugUtilsHandler().vkCmdBeginDebugUtilsLabel(transferCmdPool->GetCmdBuffer(0), &queue_debug_label);
    }

	if constexpr (VTF_VALIDATION_ENABLED && VTF_USE_DEBUG_INFO)
	{
		static const std::string base_name{ "ResourceCtxt_TransferSystem_" };
		static const std::string pool_name = base_name + std::string("_CommandPool");
		static const std::string fence_name = base_name + std::string("_Fence");
		static const std::string cmd_buffer_name = base_name + std::string("_CmdBuffer");
		RenderingContext::SetObjectName(VK_OBJECT_TYPE_COMMAND_POOL, (uint64_t)transferCmdPool->vkHandle(), pool_name.c_str());
		RenderingContext::SetObjectName(VK_OBJECT_TYPE_FENCE, (uint64_t)fence->vkHandle(), fence_name.c_str());
		RenderingContext::SetObjectName(VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t)transferCmdPool->GetCmdBuffer(0u), cmd_buffer_name.c_str());
	}

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

    if constexpr (VTF_VALIDATION_ENABLED && VTF_USE_DEBUG_INFO)
    {
        device->DebugUtilsHandler().vkCmdEndDebugUtilsLabel(transferCmdPool->GetCmdBuffer(0));
    }

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

    if constexpr (VTF_VALIDATION_ENABLED && VTF_USE_DEBUG_INFO)
    {
        device->DebugUtilsHandler().vkCmdBeginDebugUtilsLabel(transferCmdPool->GetCmdBuffer(0), &queue_debug_label);
    }

    if (uploadBuffers.empty()) {
        return;
    }

    for (auto& buff : uploadBuffers) {
		vmaDestroyBuffer(allocator, buff->Buffer, buff->Allocation);
        buff.reset();
    }

    uploadBuffers.clear();
    uploadBuffers.reserve(MAX_QUEUED_UPLOAD_BUFFERS);
    pendingResources.clear();
    pendingResources.reserve(MAX_QUEUED_UPLOAD_BUFFERS);

    while (uploadPools.size() > 1u)
    {
        vmaDestroyPool(allocator, uploadPools.back());
        uploadPools.pop_back();

        // Shrink back down one size
        auto new_pool_size_iter = std::upper_bound(std::begin(stagingPoolSizeRanges), std::end(stagingPoolSizeRanges), lastPoolSize);
        if (new_pool_size_iter != std::end(stagingPoolSizeRanges))
        {
            lastPoolSize = *new_pool_size_iter;
        }
    }

}

ResourceTransferSystem::transferSpinLockGuard ResourceTransferSystem::AcquireSpinLock() {
    return ResourceTransferSystem::transferSpinLockGuard(copyQueueLock);
}

VkCommandBuffer ResourceTransferSystem::TransferCmdBuffer() {
    cmdBufferDirty = true;
    auto& pool = *transferCmdPool;
    return pool[0];
}

bool ResourceTransferSystem::ResourceQueuedForTransfer(VulkanResource* rsrc)
{
    return pendingResources.find(rsrc) != pendingResources.end();
}

VmaPool ResourceTransferSystem::createPool()
{

    uint32_t memory_type_index{ 0u };
    VkResult create_result = vmaFindMemoryTypeIndexForBufferInfo(allocator, &staging_buffer_create_info, &alloc_create_info, &memory_type_index);
    VkAssert(create_result);

    const VmaPoolCreateInfo pool_info{
        memory_type_index,
        VMA_POOL_CREATE_IGNORE_BUFFER_IMAGE_GRANULARITY_BIT | VMA_POOL_CREATE_LINEAR_ALGORITHM_BIT,
        lastPoolSize, // block size: 256mb of staging memory. if we use more than this, we have a problem
        0u, // min block count
        0u, // max block count
        0u
    };

    VmaPool result;
    create_result = vmaCreatePool(allocator, &pool_info, &result);
    VkAssert(create_result);

    return result;
}

void ResourceTransferSystem::transferSpinLock::lock() {
    while (!lockFlag.try_lock()) {

    }
}

void ResourceTransferSystem::transferSpinLock::unlock() {
    lockFlag.unlock();
}

ResourceTransferSystem::transferSpinLockGuard::transferSpinLockGuard(transferSpinLock & _lock) : lck(_lock) {
    lck.lock();
}

ResourceTransferSystem::transferSpinLockGuard::~transferSpinLockGuard() {
    lck.unlock();
}
