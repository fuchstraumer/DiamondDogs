#include "stdafx.h"
#include "CommandPool.h"
#include "engine/renderer/core/LogicalDevice.h"

namespace vulpes {
	CommandPool CommandPool::CreateTransferPool(const Device * parent){
		VkCommandPoolCreateInfo create = vk_command_pool_info_base;
		create.queueFamilyIndex = parent->QueueFamilyIndices.Transfer;
		create.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
		CommandPool result(parent, create);
		result.CreateCommandBuffers(1);
		return std::move(result);
	}

	void CommandPool::SubmitTransferCommand(const VkCommandBuffer & buffer, const VkQueue & transfer_queue){
		assert(buffer != VK_NULL_HANDLE && transfer_queue != VK_NULL_HANDLE);

		VkResult result = vkEndCommandBuffer(buffer);
		VkAssert(result);

		VkSubmitInfo submit = vk_submit_info_base;
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &buffer;
		result = vkQueueSubmit(transfer_queue, 1, &submit, VK_NULL_HANDLE);
		VkAssert(result);

		vkQueueWaitIdle(transfer_queue);
		
	}

	CommandPool::CommandPool(const Device * _parent, const VkCommandPoolCreateInfo & create_info) : parent(_parent), createInfo(create_info) {
		VkResult result = vkCreateCommandPool(parent->vkHandle(), &createInfo, allocators, &handle);
		VkAssert(result);
	}

	void CommandPool::Create() {
		VkResult result = vkCreateCommandPool(parent->vkHandle(), &createInfo, allocators, &handle);
		VkAssert(result);
	}

	CommandPool::CommandPool(CommandPool && other) noexcept{
		handle = std::move(other.handle);
		cmdBuffers = std::move(other.cmdBuffers);
		parent = std::move(other.parent);
		allocators = std::move(other.allocators);
		other.handle = VK_NULL_HANDLE;
	}

	CommandPool & CommandPool::operator=(CommandPool && other) noexcept{
		handle = std::move(other.handle);
		cmdBuffers = std::move(other.cmdBuffers);
		parent = std::move(other.parent);
		allocators = std::move(other.allocators);
		other.handle = VK_NULL_HANDLE;
		return *this;
	}

	CommandPool::~CommandPool(){
		Destroy();
	}

	void CommandPool::Destroy(){
		if (!cmdBuffers.empty()) {
			vkFreeCommandBuffers(parent->vkHandle(), handle, static_cast<uint32_t>(cmdBuffers.size()), cmdBuffers.data());
		}
		if (handle != VK_NULL_HANDLE) {
			vkDestroyCommandPool(parent->vkHandle(), handle, allocators);
		}
	}

	void CommandPool::CreateCommandBuffers(const uint32_t & num_buffers){
		cmdBuffers.resize(num_buffers);
		VkCommandBufferAllocateInfo alloc_info = vk_command_buffer_allocate_info_base;
		alloc_info.commandPool = handle;
		alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		alloc_info.commandBufferCount = num_buffers;
		VkResult result = vkAllocateCommandBuffers(parent->vkHandle(), &alloc_info, cmdBuffers.data());
	}

	void CommandPool::FreeCommandBuffers(){
		vkFreeCommandBuffers(parent->vkHandle(), handle, static_cast<uint32_t>(cmdBuffers.size()), cmdBuffers.data());
	}
	
	const VkCommandPool & CommandPool::vkHandle() const noexcept{
		return handle;
	}

	const VkCommandBuffer & vulpes::CommandPool::operator[](const size_t & idx) const{
		return cmdBuffers[idx];
	}

	VkCommandBuffer & CommandPool::GetCmdBuffer(const size_t & idx) {
		return cmdBuffers[idx];
	}

	VkCommandBuffer CommandPool::StartSingleCmdBuffer(){
		VkCommandBufferAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandPool = handle;
		allocInfo.commandBufferCount = 1;

		VkCommandBuffer commandBuffer;
		vkAllocateCommandBuffers(parent->vkHandle(), &allocInfo, &commandBuffer);

		VkCommandBufferBeginInfo beginInfo = {};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		beginInfo.pInheritanceInfo = nullptr;

		vkBeginCommandBuffer(commandBuffer, &beginInfo);

		return commandBuffer;
	}

	void CommandPool::EndSingleCmdBuffer(VkCommandBuffer& cmd_buffer, const VkQueue & queue) {
		vkEndCommandBuffer(cmd_buffer);

		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &cmd_buffer;
		VkFence fence;
		VkFenceCreateInfo fence_info = vk_fence_create_info_base;
		vkCreateFence(parent->vkHandle(), &fence_info, allocators, &fence);

		vkQueueSubmit(queue, 1, &submitInfo, fence);
		
		vkWaitForFences(parent->vkHandle(), 1, &fence, VK_TRUE, vk_default_fence_timeout);

		vkDestroyFence(parent->vkHandle(), fence, allocators);
		vkFreeCommandBuffers(parent->vkHandle(), handle, 1, &cmd_buffer);
	}

	const size_t CommandPool::size() const noexcept{
		return cmdBuffers.size();
	}

}
