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
		void Map();
		void Unmap();

		void* GetData();

		const VkBuffer& vkHandle() const noexcept;
		VkBuffer& vkHandle() noexcept;

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
		VkMappedMemoryRange memoryRange;
		VkDeviceSize allocSize;
		VkDeviceSize dataSize;

	};
	
}

#endif // !VULPES_VK_BUFFER_H
