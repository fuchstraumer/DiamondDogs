#include "stdafx.h"
#include "Buffer.h"
#include "engine/renderer/objects\core\LogicalDevice.h"
#include "engine/renderer/objects\command\CommandPool.h"
namespace vulpes {

	Buffer::Buffer(const Device * _parent) : parent(_parent), createInfo(vk_buffer_create_info_base) {}

	Buffer::~Buffer(){
		Destroy();
	}

	void Buffer::CreateBuffer(const VkBufferUsageFlags & usage_flags, const VkMemoryPropertyFlags & memory_flags, const VkDeviceSize & size, const VkDeviceSize & offset){
		if (parent == nullptr) {
			throw(std::runtime_error("Set the parent of a buffer before trying to populate it, you dolt."));
		}
		createInfo.usage = usage_flags;
		createInfo.size = size;
		createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		VkResult result = vkCreateBuffer(parent->vkHandle(), &createInfo, allocators, &handle);
		VkAssert(result);

		VkMemoryRequirements memory_reqs;
		VkMemoryAllocateInfo alloc_info = vk_allocation_info_base;
		vkGetBufferMemoryRequirements(parent->vkHandle(), handle, &memory_reqs);
		alloc_info.allocationSize = memory_reqs.size;
		allocSize = alloc_info.allocationSize;
		alloc_info.memoryTypeIndex = parent->GetMemoryTypeIdx(memory_reqs.memoryTypeBits, memory_flags);
		result = vkAllocateMemory(parent->vkHandle(), &alloc_info, allocators, &memory);
		VkAssert(result);
		result = vkBindBufferMemory(parent->vkHandle(), handle, memory, offset);
		VkAssert(result);
	}

	void Buffer::Destroy(){
		if (memory != VK_NULL_HANDLE) {
			vkFreeMemory(parent->vkHandle(), memory, allocators);
		}
		if (handle != VK_NULL_HANDLE) {
			vkDestroyBuffer(parent->vkHandle(), handle, allocators);
		}
	}

	void Buffer::CopyTo(void * data, const VkDeviceSize & size, const VkDeviceSize& offset){
		if (MappedMemory == nullptr) {
			Map();
		}
		if (size == 0) {
			memcpy(MappedMemory, data, createInfo.size);
		}
		else {
			memcpy(MappedMemory, data, size);
		}
	}

	void Buffer::CopyTo(void * data, CommandPool* cmd_pool, const VkQueue & transfer_queue, const VkDeviceSize & size, const VkDeviceSize & offset){
		VkBuffer staging_buffer;
		VkDeviceMemory staging_memory;
		createStagingBuffer(size, offset, staging_buffer, staging_memory);

		void* mapped;
		VkResult result = vkMapMemory(parent->vkHandle(), staging_memory, offset, size, 0, &mapped);
		VkAssert(result);
			memcpy(mapped, data, size);
		vkUnmapMemory(parent->vkHandle(), staging_memory);

		VkCommandBuffer copy_cmd = cmd_pool->StartSingleCmdBuffer();

		VkBufferCopy copy_region{};
		copy_region.size = size;
		vkCmdCopyBuffer(copy_cmd, staging_buffer, handle, 1, &copy_region);
		
		cmd_pool->EndSingleCmdBuffer(copy_cmd, transfer_queue);

		vkFreeMemory(parent->vkHandle(), staging_memory, allocators);
		vkDestroyBuffer(parent->vkHandle(), staging_buffer, allocators);
	}

	void Buffer::Map(const VkDeviceSize& size, const VkDeviceSize& offset){
		if (size == 0) {
			VkResult result = vkMapMemory(parent->vkHandle(), memory, offset, allocSize, 0, &MappedMemory);
			VkAssert(result);
		}
		else {
			VkResult result = vkMapMemory(parent->vkHandle(), memory, offset, size, 0, &MappedMemory);
			VkAssert(result);
		}
	}

	void Buffer::Unmap(){
		vkUnmapMemory(parent->vkHandle(), memory);
	}

	const VkBuffer & Buffer::vkHandle() const noexcept{
		return handle;
	}

	VkBuffer & Buffer::vkHandle() noexcept{
		return handle;
	}

	const VkDeviceMemory & Buffer::DvcMemory() const noexcept{
		return memory;
	}

	VkDeviceMemory & Buffer::DvcMemory() noexcept{
		return memory;
	}

	VkDescriptorBufferInfo Buffer::GetDescriptor() const noexcept{
		return VkDescriptorBufferInfo{ handle, 0, VK_WHOLE_SIZE };
	}

	VkDeviceSize Buffer::AllocSize() const noexcept{
		return allocSize;
	}

	VkDeviceSize Buffer::DataSize() const noexcept
	{
		return createInfo.size;
	}

	void Buffer::CreateStagingBuffer(const Device * dvc, const VkDeviceSize & size, VkBuffer & dest, VkDeviceMemory & dest_memory){
		VkBufferCreateInfo create_info = vk_buffer_create_info_base;
		create_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		create_info.size = size;
		VkResult result = vkCreateBuffer(dvc->vkHandle(), &create_info, nullptr, &dest);
		VkAssert(result);

		VkMemoryRequirements memory_reqs;
		VkMemoryAllocateInfo alloc_info = vk_allocation_info_base;
		vkGetBufferMemoryRequirements(dvc->vkHandle(), dest, &memory_reqs);
		alloc_info.allocationSize = memory_reqs.size;
		alloc_info.memoryTypeIndex = dvc->GetMemoryTypeIdx(memory_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		result = vkAllocateMemory(dvc->vkHandle(), &alloc_info, nullptr, &dest_memory);
		VkAssert(result);
		result = vkBindBufferMemory(dvc->vkHandle(), dest, dest_memory, 0);
		VkAssert(result);
	}

	void Buffer::createStagingBuffer(const VkDeviceSize & size, const VkDeviceSize & offset, VkBuffer & staging_buffer, VkDeviceMemory & staging_memory){
		VkBufferCreateInfo create_info = vk_buffer_create_info_base;
		create_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		create_info.size = size;
		VkResult result = vkCreateBuffer(parent->vkHandle(), &create_info, allocators, &staging_buffer);
		VkAssert(result);

		VkMemoryRequirements memory_reqs;
		VkMemoryAllocateInfo alloc_info = vk_allocation_info_base;
		vkGetBufferMemoryRequirements(parent->vkHandle(), staging_buffer, &memory_reqs);
		alloc_info.allocationSize = memory_reqs.size;
		alloc_info.memoryTypeIndex = parent->GetMemoryTypeIdx(memory_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		result = vkAllocateMemory(parent->vkHandle(), &alloc_info, allocators, &staging_memory);
		VkAssert(result);
		result = vkBindBufferMemory(parent->vkHandle(), staging_buffer, staging_memory, offset);
		VkAssert(result);
	}

}
