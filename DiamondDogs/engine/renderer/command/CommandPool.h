#pragma once
#ifndef VULPES_VK_COMMAND_POOL_H
#define VULPES_VK_COMMAND_POOL_H
#include "stdafx.h"
#include "engine/renderer/ForwardDecl.h"
#include "engine/renderer/NonCopyable.h"

namespace vulpes {

	class CommandPool : public NonCopyable {
	public:

		CommandPool(const Device* parent, const VkCommandPoolCreateInfo& create_info, bool primary = true);

		CommandPool(const Device* parent, bool primary = true);

		void Create();
		void Reset();

		CommandPool(CommandPool&& other) noexcept;
		CommandPool& operator=(CommandPool&& other) noexcept;

		virtual ~CommandPool();

		void Destroy();

		void CreateCommandBuffers(const uint32_t& num_buffers, const VkCommandBufferAllocateInfo& alloc_info = vk_command_buffer_allocate_info_base);

		void FreeCommandBuffers();

		void ResetCommandBuffer(const size_t& idx);

		const VkCommandPool& vkHandle() const noexcept;
		const VkCommandBuffer& operator[](const size_t& idx) const;

		VkCommandBuffer& GetCmdBuffer(const size_t& idx);
		const std::vector<VkCommandBuffer>& GetCmdBuffers() const;

		VkCommandBuffer StartSingleCmdBuffer();
		void EndSingleCmdBuffer(VkCommandBuffer& cmd_buffer, const VkQueue & queue);
		const size_t size() const noexcept;

	protected:
		std::vector<VkCommandBuffer> cmdBuffers;
		VkCommandPool handle;
		VkCommandPoolCreateInfo createInfo;
		const Device* parent;
		const VkAllocationCallbacks* allocators = nullptr;
		bool primary;
	};


	
}
#endif // !VULPES_VK_COMMAND_POOL_H
