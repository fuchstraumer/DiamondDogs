#pragma once
#ifndef VULPES_VK_COMMAND_POOL_H
#define VULPES_VK_COMMAND_POOL_H
#include "stdafx.h"
#include "engine/renderer/ForwardDecl.h"
#include "engine/renderer/NonCopyable.h"

namespace vulpes {

	class CommandPool : public NonCopyable {
	public:

		static CommandPool CreateTransferPool(const Device* parent);

		static void SubmitTransferCommand(const VkCommandBuffer& buffer, const VkQueue& transfer_queue);

		CommandPool() : handle(VK_NULL_HANDLE), parent(nullptr) {}

		CommandPool(const Device* parent, const VkCommandPoolCreateInfo& create_info);

		void Create();

		CommandPool(CommandPool&& other) noexcept;
		CommandPool& operator=(CommandPool&& other) noexcept;

		~CommandPool();

		void Destroy();

		void CreateCommandBuffers(const uint32_t& num_buffers);

		void FreeCommandBuffers();

		const VkCommandPool& vkHandle() const noexcept;
		const VkCommandBuffer& operator[](const size_t& idx) const;

		VkCommandBuffer& GetCmdBuffer(const size_t& idx);

		VkCommandBuffer StartSingleCmdBuffer();
		void EndSingleCmdBuffer(VkCommandBuffer& cmd_buffer, const VkQueue & queue);
		const size_t size() const noexcept;

	protected:
		std::vector<VkCommandBuffer> cmdBuffers;
		VkCommandPool handle;
		VkCommandPoolCreateInfo createInfo;
		const Device* parent;
		const VkAllocationCallbacks* allocators = nullptr;
	};


	class TransferPool {
	public:

		TransferPool(const Device* parent, const VkCommandPoolCreateInfo& create_info);

		~TransferPool();

		void SetSubmitQueue(VkQueue& queue);

		VkCommandBuffer& Start();

		void Submit();

		VkCommandBuffer& CmdBuffer() noexcept;

	private:
		VkFence submitFence;
		VkQueue submitQueue;
		VkCommandBuffer cmdBuffer;
		VkCommandPool handle;
		VkCommandPoolCreateInfo createInfo;
		const Device* parent;
		const VkAllocationCallbacks* allocators = nullptr;
	};
}
#endif // !VULPES_VK_COMMAND_POOL_H
