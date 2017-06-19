#pragma once
#ifndef VULPES_VK_LOGICAL_DEVICE_H
#define VULPES_VK_LOGICAL_DEVICE_H

#include "stdafx.h"
#include "engine/renderer\ForwardDecl.h"
#include "engine/renderer\NonCopyable.h"

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

		void CheckSurfaceSupport(const VkSurfaceKHR& surf);

		VkQueue GraphicsQueue(const uint32_t & idx = 0) const;
		const void TransferQueue(const uint32_t & idx, VkQueue& queue) const;
		VkQueue PresentQueue(const uint32_t & idx = 0) const;
		VkQueue ComputeQueue(const uint32_t & idx = 0) const;

		operator VkDevice() const;

		VkFormat FindSupportedFormat(const std::vector<VkFormat>& options, const VkImageTiling& tiling, const VkFormatFeatureFlags& flags) const;
		VkFormat FindDepthFormat() const;

		uint32_t GetMemoryTypeIdx(const uint32_t& type_bitfield, const VkMemoryPropertyFlags& property_flags, VkBool32* memory_type_found = nullptr) const;

		uint32_t GetPhysicalDeviceID() const noexcept;

		const PhysicalDevice& GetPhysicalDevice() const noexcept;

		bool HasDedicatedComputeQueues() const;

		void vkSetObjectDebugMarkerName(const uint64_t& object_handle, const VkDebugReportObjectTypeEXT& object_type, const char* name) const;
		void vkSetObjectDebugMarkerTag(const uint64_t& object_handle, const VkDebugReportObjectTypeEXT& object_type, uint64_t name, size_t tagSize, const void* tag) const;
		void vkCmdBeginDebugMarkerRegion(VkCommandBuffer& cmd, const char* region_name, const glm::vec4& region_color) const;
		void vkCmdInsertDebugMarker(VkCommandBuffer& cmd, const char* marker_name, const glm::vec4& marker_color) const;
		void vkCmdEndDebugMarkerRegion(VkCommandBuffer& cmd) const;
		bool MarkersEnabled;

		uint32_t numGraphicsQueues, numComputeQueues, numTransferQueues;
	private:
		VkQueue graphics = VK_NULL_HANDLE, compute = VK_NULL_HANDLE;
		VkQueue present = VK_NULL_HANDLE, binding = VK_NULL_HANDLE;

		const VkAllocationCallbacks* AllocCallbacks = nullptr;

		VkDevice handle;

		const PhysicalDevice* parent;
		

		PFN_vkDebugMarkerSetObjectTagEXT pfnDebugMarkerSetObjectTag;
		PFN_vkDebugMarkerSetObjectNameEXT pfnDebugMarkerSetObjectName;
		PFN_vkCmdDebugMarkerBeginEXT pfnCmdDebugMarkerBegin;
		PFN_vkCmdDebugMarkerEndEXT pfnCmdDebugMarkerEnd;
		PFN_vkCmdDebugMarkerInsertEXT pfnCmdDebugMarkerInsert;
	};

}

#endif // !VULPES_VK_LOGICAL_DEVICE_H
