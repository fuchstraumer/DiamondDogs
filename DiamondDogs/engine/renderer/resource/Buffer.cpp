#include "stdafx.h"
#include "Buffer.h"
#include "engine/renderer\core\LogicalDevice.h"
#include "engine/renderer\command\CommandPool.h"
#include "engine/renderer/resource/Allocator.h"
namespace vulpes {

	std::vector<VkBuffer> Buffer::stagingBuffers = std::vector<VkBuffer>();
	std::vector<VkDeviceMemory> Buffer::stagingMemory = std::vector<VkDeviceMemory>();

	Buffer::Buffer(const Device * _parent) : parent(_parent), createInfo(vk_buffer_create_info_base), handle(VK_NULL_HANDLE), memoryRange(VkMappedMemoryRange{ VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, nullptr }) {}

	Buffer::~Buffer(){
		Destroy();
	}

	Buffer::Buffer(Buffer && other) noexcept : allocators(std::move(other.allocators)), allocSize(std::move(other.allocSize)), createInfo(std::move(other.createInfo)), dataSize(std::move(other.dataSize)), handle(std::move(other.handle)), memoryRange(std::move(other.memoryRange)), parent(std::move(other.parent)), MappedMemory(std::move(other.MappedMemory)), view(std::move(other.view)) {
		// Make sure to nullify so destructor checks safer/more likely to succeed.
		other.handle = VK_NULL_HANDLE;
		other.memoryRange.memory = VK_NULL_HANDLE;
		other.MappedMemory = nullptr;
	}

	Buffer & Buffer::operator=(Buffer && other) noexcept {
		allocators = std::move(other.allocators);
		allocSize = std::move(other.allocSize);
		createInfo = std::move(other.createInfo);
		dataSize = std::move(other.dataSize);
		handle = std::move(other.handle);
		memoryRange = std::move(other.memoryRange);
		parent = std::move(other.parent);
		MappedMemory = std::move(other.MappedMemory);
		view = std::move(other.view);
		other.handle = VK_NULL_HANDLE;
		other.memoryRange.memory = VK_NULL_HANDLE;
		other.MappedMemory = nullptr;
		return *this;
	}

	void Buffer::CreateBuffer(const VkBufferUsageFlags & usage_flags, const VkMemoryPropertyFlags & memory_flags, const VkDeviceSize & size, const VkDeviceSize & offset) {
		if (parent == nullptr) {
			throw(std::runtime_error("Set the parent of a buffer before trying to populate it, you dolt."));
		}
		dataSize = size;
		createInfo.usage = usage_flags;
		createInfo.size = size;
		createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		AllocationRequirements reqs;
		reqs.preferredFlags = 0;
		reqs.privateMemory = false;
		reqs.requiredFlags = createInfo.usage;
		VkResult result = parent->vkAllocator->CreateBuffer(&handle, &memoryRange, &createInfo, reqs);
		VkAssert(result);
	}

	void Buffer::Destroy(){
		parent->vkAllocator->DestroyBuffer(handle);
	}

	void Buffer::CopyTo(void * data, const VkDeviceSize & size, const VkDeviceSize& offset){
		if (MappedMemory == nullptr) {
			Map();
		}
		if (size == 0) {
			memcpy(MappedMemory, data, dataSize);
		}
		else {
			memcpy(MappedMemory, data, size);
		}
	}

	void Buffer::CopyTo(void* data, VkCommandBuffer& transfer_cmd, const VkDeviceSize& copy_size, const VkDeviceSize& copy_offset) {

		VkBuffer staging_buffer;
		VkDeviceMemory staging_memory;
		createStagingBuffer(copy_size, 0, staging_buffer, staging_memory);

		void* mapped;
		VkResult result = vkMapMemory(parent->vkHandle(), staging_memory, 0, copy_size, 0, &mapped);
		VkAssert(result);
		memcpy(mapped, data, dataSize);
		vkUnmapMemory(parent->vkHandle(), staging_memory);

		VkBufferCopy copy{};
		copy.size = copy_size;
		copy.dstOffset = copy_offset;

		vkCmdCopyBuffer(transfer_cmd, staging_buffer, handle, 1, &copy);

		stagingBuffers.push_back(std::move(staging_buffer));
		stagingMemory.push_back(std::move(staging_memory));
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

	void Buffer::UpdateCmd(VkCommandBuffer & cmd, const VkDeviceSize & data_sz, const VkDeviceSize & offset, const void * data) {
		assert(MappedMemory);
		vkCmdUpdateBuffer(cmd, handle, memoryRange.offset + offset, data_sz, data);
	}

	void Buffer::Map(const VkDeviceSize& size, const VkDeviceSize& offset){
		if (size == 0) {
			VkResult result = vkMapMemory(parent->vkHandle(), memoryRange.memory, memoryRange.offset + offset, allocSize, 0, &MappedMemory);
			VkAssert(result);
		}
		else {
			VkResult result = vkMapMemory(parent->vkHandle(), memoryRange.memory, memoryRange.offset, memoryRange.size, 0, &MappedMemory);
			VkAssert(result);
		}
	}

	void Buffer::Unmap(){
		vkUnmapMemory(parent->vkHandle(), memoryRange.memory);
	}

	void * Buffer::GetData() {
		Map(allocSize, 0);
		void* result;
		memcpy(result, MappedMemory, allocSize);
		Unmap();
		return result;
	}

	const VkBuffer & Buffer::vkHandle() const noexcept{
		return handle;
	}

	VkBuffer & Buffer::vkHandle() noexcept{
		return handle;
	}


	VkDescriptorBufferInfo Buffer::GetDescriptor() const noexcept{
		return VkDescriptorBufferInfo{ handle, 0, allocSize };
	}

	VkDeviceSize Buffer::AllocSize() const noexcept{
		return allocSize;
	}

	VkDeviceSize Buffer::DataSize() const noexcept{
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

	void Buffer::DestroyStagingResources(const Device* device){
		for (auto& buff : stagingBuffers) {
			device->vkAllocator->DestroyBuffer(buff);
		}
		stagingBuffers.clear(); 
		stagingBuffers.shrink_to_fit();
		stagingMemory.clear();
		stagingMemory.shrink_to_fit();
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
