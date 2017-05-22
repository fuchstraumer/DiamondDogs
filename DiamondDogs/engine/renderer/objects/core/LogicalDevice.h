#pragma once
#ifndef VULPES_VK_LOGICAL_DEVICE_H
#define VULPES_VK_LOGICAL_DEVICE_H

#include "stdafx.h"
#include "engine/renderer/objects\ForwardDecl.h"
#include "engine/renderer/objects\NonCopyable.h"

namespace vulpes {

	constexpr auto default_queues = VkQueueFlagBits::VK_QUEUE_COMPUTE_BIT | VkQueueFlagBits::VK_QUEUE_GRAPHICS_BIT | VkQueueFlagBits::VK_QUEUE_TRANSFER_BIT;

	struct vkQueueFamilyIndices {
		// indices into queue families.
		uint32_t Graphics = std::numeric_limits<uint32_t>::max(), 
			Compute = std::numeric_limits<uint32_t>::max(), 
			Transfer = std::numeric_limits<uint32_t>::max(), 
			SparseBinding = std::numeric_limits<uint32_t>::max(), 
			Present = std::numeric_limits<uint32_t>::max();
	};

	class Device : public NonMovable {
	public:
		Device(const Instance* instance, const PhysicalDevice* device, const VkDeviceCreateInfo& create_info);

		Device(const Instance* instance, const PhysicalDevice* device);

		~Device();

		vkQueueFamilyIndices QueueFamilyIndices;
		std::vector<const char*> Extensions;

		const VkDevice& vkHandle() const;

		VkQueue GraphicsQueue(const uint32_t & idx = 0) const;
		const void TransferQueue(const uint32_t & idx, VkQueue& queue) const;
		VkQueue PresentQueue(const uint32_t & idx = 0) const;
		VkQueue ComputeQueue(const uint32_t & idx = 0) const;

		operator VkDevice() const;

		VkFormat FindSupportedFormat(const std::vector<VkFormat>& options, const VkImageTiling& tiling, const VkFormatFeatureFlags& flags) const;
		VkFormat FindDepthFormat() const;

		uint32_t GetMemoryTypeIdx(uint32_t& type_bitfield, const VkMemoryPropertyFlags& property_flags, VkBool32* memory_type_found = nullptr) const;

		uint32_t GetPhysicalDeviceID() const noexcept;

		const PhysicalDevice& GetPhysicalDevice() const noexcept;

	private:
		VkQueue graphics = VK_NULL_HANDLE, compute = VK_NULL_HANDLE;
		VkQueue present = VK_NULL_HANDLE, binding = VK_NULL_HANDLE;
		const VkAllocationCallbacks* AllocCallbacks = nullptr;
		VkDevice handle;
		const PhysicalDevice* parent;
		uint32_t numGraphicsQueues, numComputeQueues, numTransferQueues;
	};

}

#endif // !VULPES_VK_LOGICAL_DEVICE_H
