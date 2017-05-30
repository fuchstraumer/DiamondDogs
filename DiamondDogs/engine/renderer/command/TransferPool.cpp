#include "stdafx.h"
#include "TransferPool.h"
#include "../core/LogicalDevice.h"

namespace vulpes {

    TransferPool::TransferPool(const Device * _parent) : CommandPool(_parent) {
		
		createInfo = transfer_pool_info;
		createInfo.queueFamilyIndex = parent->QueueFamilyIndices.Transfer;
		VkResult result = vkCreateCommandPool(parent->vkHandle(), &createInfo, allocators, &handle);
		VkAssert(result);

		VkCommandBufferAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandPool = handle;
		allocInfo.commandBufferCount = 1;

		CreateCommandBuffers(1);

		result = vkCreateFence(parent->vkHandle(), &vk_fence_create_info_base, allocators, &fence);
		VkAssert(result);

		parent->TransferQueue(0, queue);

	}

	VkCommandBuffer& TransferPool::Begin() {

		VkCommandBufferBeginInfo beginInfo = {};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		beginInfo.pInheritanceInfo = nullptr;

		vkBeginCommandBuffer(cmdBuffers.front(), &beginInfo);

		return cmdBuffers.front();
	}

	void TransferPool::End() {
		VkResult result = vkEndCommandBuffer(cmdBuffers.front());
		VkAssert(result);
	}

	void TransferPool::Submit() {
		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = cmdBuffers.data();

		VkResult result = vkQueueSubmit(queue, 1, &submitInfo, fence);
		VkAssert(result);

		result = vkWaitForFences(parent->vkHandle(), 1, &fence, VK_TRUE, vk_default_fence_timeout);
		VkAssert(result);
		vkResetFences(parent->vkHandle(), 1, &fence);

		// Reset command buffer so we can re-record shortly.
		ResetCommandBuffer(0);
	}

	VkCommandBuffer& TransferPool::CmdBuffer() noexcept {
		return cmdBuffers.front();
	}

}