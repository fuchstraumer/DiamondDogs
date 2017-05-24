#include "stdafx.h"
#include "LogicalDevice.h"
#include "PhysicalDevice.h"
#include "Instance.h"
namespace vulpes {

	Device::Device(const Instance* instance, const PhysicalDevice * device) : parent(device) {

		std::vector<VkDeviceQueueCreateInfo> queue_infos;

		constexpr float queue_priority = 1.0f;

		VkDeviceQueueCreateInfo graphics_info = vk_device_queue_create_info_base;
		QueueFamilyIndices.Graphics = parent->GetQueueFamilyIndex(VK_QUEUE_GRAPHICS_BIT);
		auto graphics_properties = parent->GetQueueFamilyProperties(VK_QUEUE_GRAPHICS_BIT);
		graphics_info.queueCount = graphics_properties.queueCount;
		numGraphicsQueues = graphics_properties.queueCount;
		graphics_info.queueFamilyIndex = QueueFamilyIndices.Graphics;
		std::vector<float> gqueue_priorities(numGraphicsQueues);
		std::fill(gqueue_priorities.begin(), gqueue_priorities.end(), 1.0f);
		graphics_info.pQueuePriorities = gqueue_priorities.data();
		queue_infos.push_back(graphics_info);
		
		QueueFamilyIndices.Compute = parent->GetQueueFamilyIndex(VK_QUEUE_COMPUTE_BIT);
		if (QueueFamilyIndices.Compute != QueueFamilyIndices.Graphics) {
			VkDeviceQueueCreateInfo compute_info = vk_device_queue_create_info_base;
			VkQueueFamilyProperties compute_properties = parent->GetQueueFamilyProperties(VK_QUEUE_COMPUTE_BIT);
			compute_info.queueCount = compute_properties.queueCount;
			numComputeQueues = compute_properties.queueCount;
			std::vector<float> queue_priorities(numComputeQueues);
			std::fill(queue_priorities.begin(), queue_priorities.end(), 1.0f);
			compute_info.pQueuePriorities = queue_priorities.data();
			compute_info.queueFamilyIndex = QueueFamilyIndices.Compute;
			queue_infos.push_back(compute_info);
		}
		else {
			numComputeQueues = numGraphicsQueues;
		}
		
		QueueFamilyIndices.Transfer = parent->GetQueueFamilyIndex(VK_QUEUE_TRANSFER_BIT);
		if (QueueFamilyIndices.Transfer != QueueFamilyIndices.Graphics) {
			VkDeviceQueueCreateInfo transfer_info = vk_device_queue_create_info_base;
			VkQueueFamilyProperties transfer_properties = parent->GetQueueFamilyProperties(VK_QUEUE_TRANSFER_BIT);
			transfer_info.queueCount = transfer_properties.queueCount;
			numTransferQueues = transfer_properties.queueCount;
			std::vector<float> queue_priorities(numTransferQueues);
			std::fill(queue_priorities.begin(), queue_priorities.end(), 1.0f);
			transfer_info.pQueuePriorities = queue_priorities.data();
			transfer_info.queueFamilyIndex = QueueFamilyIndices.Transfer;
			queue_infos.push_back(transfer_info);
		}
		else {
			numTransferQueues = numGraphicsQueues;
		}
	
		QueueFamilyIndices.SparseBinding = parent->GetQueueFamilyIndex(VK_QUEUE_SPARSE_BINDING_BIT);
		if (QueueFamilyIndices.SparseBinding != QueueFamilyIndices.Graphics) {
			VkDeviceQueueCreateInfo binding_info = vk_device_queue_create_info_base;
			binding_info.queueFamilyIndex = QueueFamilyIndices.SparseBinding;
			queue_infos.push_back(binding_info);
		}

		{
			// Get presentation support
			VkBool32 present_support = false;
			for (uint32_t i = 0; i < 3; ++i) {
				vkGetPhysicalDeviceSurfaceSupportKHR(parent->vkHandle(), i, instance->GetSurface(), &present_support);
				if (present_support) {
					QueueFamilyIndices.Present = i;
					break;
				}
			}
		}

		if (QueueFamilyIndices.Present != QueueFamilyIndices.Graphics) {
			VkDeviceQueueCreateInfo present_info = vk_device_queue_create_info_base;
			present_info.queueFamilyIndex = QueueFamilyIndices.Present;
			present_info.pQueuePriorities = &queue_priority;
			queue_infos.push_back(std::move(present_info));
		}
		
		VkDeviceCreateInfo create_info = vk_device_create_info_base;
		create_info.queueCreateInfoCount = static_cast<uint32_t>(queue_infos.size());
		create_info.pQueueCreateInfos = queue_infos.data();
		
		create_info.pEnabledFeatures = &device->Features;
		create_info.enabledExtensionCount = 1;
		create_info.ppEnabledExtensionNames = device_extensions.data();

		if (instance->validationEnabled) {
			create_info.enabledLayerCount = 1;
			create_info.ppEnabledLayerNames = &standard_validation_layer;
		}
		else {
			create_info.enabledLayerCount = 0;
			create_info.ppEnabledLayerNames = nullptr;
		}

		VkResult result = vkCreateDevice(parent->vkHandle(), &create_info, AllocCallbacks, &handle);
		VkAssert(result);
		
	}

	Device::~Device(){
		vkDestroyDevice(handle, AllocCallbacks);
	}

	const VkDevice & Device::vkHandle() const{
		return handle;
	}

	void Device::CheckSurfaceSupport(const VkSurfaceKHR& surf) {
		VkBool32 supported;
		vkGetPhysicalDeviceSurfaceSupportKHR(parent->vkHandle(), QueueFamilyIndices.Present, surf, &supported);
		assert(supported);
	}

	VkQueue Device::GraphicsQueue(const uint32_t & idx) const{
		assert(idx < numGraphicsQueues);
		VkQueue result;
		vkGetDeviceQueue(vkHandle(), QueueFamilyIndices.Graphics, idx, &result);
		return result;
	}

	const void Device::TransferQueue(const uint32_t & idx, VkQueue& queue) const{
		vkGetDeviceQueue(vkHandle(), QueueFamilyIndices.Transfer, idx, &queue);
		assert(queue != VK_NULL_HANDLE);
		return;
	}

	VkQueue Device::PresentQueue(const uint32_t & idx) const{
		return present;
	}

	VkQueue Device::ComputeQueue(const uint32_t & idx) const{
		assert(idx < numComputeQueues);
		VkQueue result;
		vkGetDeviceQueue(vkHandle(), QueueFamilyIndices.Compute, idx, &result);
		return result;
	}

	Device::operator VkDevice() const{
		return handle;
	}

	VkFormat Device::FindSupportedFormat(const std::vector<VkFormat>& options, const VkImageTiling & tiling, const VkFormatFeatureFlags & flags) const {
		for (const auto& fmt : options) {
			VkFormatProperties properties;
			vkGetPhysicalDeviceFormatProperties(parent->vkHandle(), fmt, &properties);
			if (tiling == VK_IMAGE_TILING_LINEAR && (properties.linearTilingFeatures & flags) == flags) {
				return fmt;
			}
			else if (tiling == VK_IMAGE_TILING_OPTIMAL && (properties.optimalTilingFeatures & flags) == flags) {
				return fmt;
			}
		}
		throw(std::runtime_error("Failed to find a supported format!"));
	}

	VkFormat Device::FindDepthFormat() const{
		return FindSupportedFormat({ VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT }, VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
	}

	uint32_t Device::GetMemoryTypeIdx(uint32_t & type_bitfield, const VkMemoryPropertyFlags & property_flags, VkBool32 * memory_type_found) const{
		return parent->GetMemoryTypeIdx(type_bitfield, property_flags, memory_type_found);
	}

	uint32_t Device::GetPhysicalDeviceID() const noexcept{
		return parent->Properties.deviceID;
	}

	const PhysicalDevice & Device::GetPhysicalDevice() const noexcept{
		return *parent;
	}

}
