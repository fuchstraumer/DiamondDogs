#include "stdafx.h"
#include "Buffer.h"
#include "engine/renderer\core\LogicalDevice.h"
#include "engine/renderer\command\CommandPool.h"
#include "engine/renderer/resource/Allocator.h"
namespace vulpes {

	std::vector<VkBuffer> Buffer::stagingBuffers = std::vector<VkBuffer>();

	Buffer::Buffer(const Device * _parent) : parent(_parent), createInfo(vk_buffer_create_info_base), handle(VK_NULL_HANDLE), memoryRange(VkMappedMemoryRange{ VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, nullptr }) {}

	Buffer::~Buffer(){
		Destroy();
	}

	Buffer::Buffer(Buffer && other) noexcept : allocators(std::move(other.allocators)), size(std::move(other.size)), createInfo(std::move(other.createInfo)), handle(std::move(other.handle)), memoryRange(std::move(other.memoryRange)), parent(std::move(other.parent)), MappedMemory(std::move(other.MappedMemory)), view(std::move(other.view)) {
		// Make sure to nullify so destructor checks safer/more likely to succeed.
		other.handle = VK_NULL_HANDLE;
		other.memoryRange.memory = VK_NULL_HANDLE;
		other.MappedMemory = nullptr;
	}

	Buffer & Buffer::operator=(Buffer && other) noexcept {
		allocators = std::move(other.allocators);
		size = std::move(other.size);
		createInfo = std::move(other.createInfo);
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

	void Buffer::CreateBuffer(const VkBufferUsageFlags & usage_flags, const VkMemoryPropertyFlags & memory_flags, const VkDeviceSize & _size, const VkDeviceSize & offset) {
		
		if (parent == nullptr) {
			throw(std::runtime_error("Set the parent of a buffer before trying to populate it, you dolt."));
		}

		createInfo.usage = usage_flags;
		createInfo.size = _size;
		dataSize = _size;
		createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		AllocationRequirements reqs;
		reqs.preferredFlags = 0;
		reqs.privateMemory = false;
		reqs.requiredFlags = memory_flags;
		VkResult result = parent->vkAllocator->CreateBuffer(&handle, &memoryRange, &createInfo, reqs);
		VkAssert(result);
		size = memoryRange.size;

	}

	void Buffer::Destroy(){
		if (handle != VK_NULL_HANDLE) {
			parent->vkAllocator->DestroyBuffer(handle);
		}
	}

	void Buffer::CopyTo(void * data, const VkDeviceSize & copy_size, const VkDeviceSize& offset){
		if (MappedMemory == nullptr) {
			Map();
		}
		if (size == 0) {
			memcpy(MappedMemory, data, Size());
		}
		else {
			memcpy(MappedMemory, data, copy_size);
		}
	}

	void Buffer::CopyTo(void* data, VkCommandBuffer& transfer_cmd, const VkDeviceSize& copy_size, const VkDeviceSize& copy_offset) {

		VkBuffer staging_buffer;
		VkMappedMemoryRange staging_range = vk_mapped_memory_base;
		createStagingBuffer(copy_size, 0, staging_buffer, staging_range);

		void* mapped;
		VkResult result = vkMapMemory(parent->vkHandle(), staging_range.memory, staging_range.offset, copy_size, 0, &mapped);
		VkAssert(result);
		memcpy(mapped, data, copy_size);
		vkUnmapMemory(parent->vkHandle(), staging_range.memory);

		VkBufferCopy copy{};
		copy.size = copy_size;
		copy.dstOffset = copy_offset;
		vkCmdCopyBuffer(transfer_cmd, staging_buffer, handle, 1, &copy);

		stagingBuffers.push_back(std::move(staging_buffer));
	}

	void Buffer::CopyTo(void * data, CommandPool* cmd_pool, const VkQueue & transfer_queue, const VkDeviceSize & size, const VkDeviceSize & offset){
		
		VkBuffer staging_buffer;
		VkMappedMemoryRange staging_range = vk_mapped_memory_base;
		createStagingBuffer(size, offset, staging_buffer, staging_range);

		void* mapped;
		VkResult result = vkMapMemory(parent->vkHandle(), staging_range.memory, staging_range.offset + offset, size, 0, &mapped);
		VkAssert(result);
			memcpy(mapped, data, size);
		vkUnmapMemory(parent->vkHandle(), staging_range.memory);

		VkCommandBuffer copy_cmd = cmd_pool->StartSingleCmdBuffer();
		VkBufferCopy copy_region{};
		copy_region.size = size;
		vkCmdCopyBuffer(copy_cmd, staging_buffer, handle, 1, &copy_region);
		cmd_pool->EndSingleCmdBuffer(copy_cmd, transfer_queue);

		parent->vkAllocator->DestroyBuffer(staging_buffer);
	}

	void Buffer::Update(VkCommandBuffer & cmd, const VkDeviceSize & data_sz, const VkDeviceSize & offset, const void * data) {
		assert(MappedMemory);
		vkCmdUpdateBuffer(cmd, handle, memoryRange.offset + offset, data_sz, data);
	}

	void Buffer::Map(){
		VkResult result = vkMapMemory(parent->vkHandle(), memoryRange.memory, memoryRange.offset, memoryRange.size, 0, &MappedMemory);
		VkAssert(result);
	}

	void Buffer::Unmap(){
		vkUnmapMemory(parent->vkHandle(), memoryRange.memory);
	}

	const VkBuffer & Buffer::vkHandle() const noexcept{
		return handle;
	}

	VkBuffer & Buffer::vkHandle() noexcept{
		return handle;
	}

	VkDescriptorBufferInfo Buffer::GetDescriptor() const noexcept{
		return VkDescriptorBufferInfo{ handle, 0, size };
	}

	VkDeviceSize Buffer::Size() const noexcept{
		return size;
	}

	VkDeviceSize Buffer::InitDataSize() const noexcept {
		return dataSize;
	}

	void Buffer::CreateStagingBuffer(const Device * dvc, const VkDeviceSize & size, VkBuffer & dest, VkMappedMemoryRange& dest_memory_range){
		
		VkBufferCreateInfo create_info = vk_buffer_create_info_base;
		create_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		create_info.size = size;

		AllocationRequirements alloc_reqs;
		alloc_reqs.privateMemory = false;
		alloc_reqs.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;

		VkResult result = dvc->vkAllocator->CreateBuffer(&dest, &dest_memory_range, &create_info, alloc_reqs);
		VkAssert(result);

		stagingBuffers.push_back(dest);
	}

	void Buffer::DestroyStagingResources(const Device* device){
		for (auto& buff : stagingBuffers) {
			device->vkAllocator->DestroyBuffer(buff);
		}
		stagingBuffers.clear(); 
		stagingBuffers.shrink_to_fit();
	}

	void Buffer::createStagingBuffer(const VkDeviceSize & size, const VkDeviceSize & offset, VkBuffer & staging_buffer, VkMappedMemoryRange& dest_memory_range){
		
		VkBufferCreateInfo create_info = vk_buffer_create_info_base;
		create_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		create_info.size = size;

		AllocationRequirements alloc_reqs;
		alloc_reqs.privateMemory = false;
		alloc_reqs.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;

		VkResult result = parent->vkAllocator->CreateBuffer(&staging_buffer, &dest_memory_range, &create_info, alloc_reqs);
		VkAssert(result);
	}

}
