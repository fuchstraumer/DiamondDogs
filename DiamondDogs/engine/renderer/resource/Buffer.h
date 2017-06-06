#pragma once
#ifndef VULPES_VK_BUFFER_H
#define VULPES_VK_BUFFER_H
#include "stdafx.h"
#include "engine/renderer/ForwardDecl.h"
#include "engine/renderer/NonCopyable.h"

namespace vulpes {

	class Buffer : public NonCopyable {
	public:

		Buffer(const Device* parent = nullptr);

		~Buffer();

		Buffer(Buffer&& other) noexcept;
		Buffer& operator=(Buffer&& other) noexcept;

		void CreateBuffer(const VkBufferUsageFlags& usage_flags, const VkMemoryPropertyFlags& memory_flags, const VkDeviceSize& size, const VkDeviceSize& offset = 0);

		void Destroy();

		void CopyTo(void* data, const VkDeviceSize& size = 0, const VkDeviceSize& offset = 0);
		void CopyTo(void * data, VkCommandBuffer & transfer_cmd, const VkDeviceSize& copy_size, const VkDeviceSize& copy_offset);
		void CopyTo(void* data, CommandPool* cmd_pool, const VkQueue & transfer_queue, const VkDeviceSize& size, const VkDeviceSize& offset = 0);

		void UpdateCmd(VkCommandBuffer& cmd, const VkDeviceSize& data_sz, const VkDeviceSize& offset, const void* data);
		void Map(const VkDeviceSize& size = 0, const VkDeviceSize& offset = 0);
		void Unmap();

		const VkBuffer& vkHandle() const noexcept;
		VkBuffer& vkHandle() noexcept;
		const VkDeviceMemory& DvcMemory() const noexcept;
		VkDeviceMemory& DvcMemory() noexcept;

		VkDescriptorBufferInfo GetDescriptor() const noexcept;

		VkDeviceSize AllocSize() const noexcept;
		VkDeviceSize DataSize() const noexcept;

		void* MappedMemory = nullptr;
		static void CreateStagingBuffer(const Device* dvc, const VkDeviceSize& size, VkBuffer& dest, VkDeviceMemory& dest_memory);
		static void DestroyStagingResources(const Device* device);

	protected:

		static std::vector<VkBuffer> stagingBuffers;
		static std::vector<VkDeviceMemory> stagingMemory;

		void createStagingBuffer(const VkDeviceSize& size, const VkDeviceSize& offset, VkBuffer& staging_buffer, VkDeviceMemory& staging_memory);

		const Device* parent;
		const VkAllocationCallbacks* allocators = nullptr;
		VkBuffer handle;
		VkBufferCreateInfo createInfo;
		VkBufferView view;
		VkDeviceMemory memory;
		VkDeviceSize allocSize;
		VkDeviceSize dataSize;

	};

	/*
		Represents a pooled buffer that uses one VkDeviceMemory object, 
		but has several buffer objects that are bound to different
		sub-offsets of the memory.
	*/
	class PooledBuffer : public Buffer {

		PooledBuffer(const Device* dvc);

		// Allocates total memory required, creates N buffers where N is size of offsets and binds memory to each buffer n as appropriate.
		void CreateBuffers(const VkBufferUsageFlags& usage, const VkMemoryPropertyFlags& memory_flags, const VkDeviceSize& total_size, const std::vector<VkDeviceSize>& offsets);



	protected:
		// Buffers are sorted/indexed by their offsets.
		std::unordered_map<VkDeviceSize, VkBuffer> buffers;
	};


}

#endif // !VULPES_VK_BUFFER_H
