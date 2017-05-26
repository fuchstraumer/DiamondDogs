#include "stdafx.h"
#include "Swapchain.h"
#include "engine/renderer\core\Instance.h"
#include "engine/renderer\core\PhysicalDevice.h"
#include "engine/renderer\core\LogicalDevice.h"

namespace vulpes {

	SwapchainInfo::SwapchainInfo(const VkPhysicalDevice & dvc, const VkSurfaceKHR& sfc){
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dvc, sfc, &Capabilities);
		uint32_t fmt_cnt = 0;
		vkGetPhysicalDeviceSurfaceFormatsKHR(dvc, sfc, &fmt_cnt, nullptr);
		if (fmt_cnt != 0) {
			Formats.resize(fmt_cnt);
			vkGetPhysicalDeviceSurfaceFormatsKHR(dvc, sfc, &fmt_cnt, Formats.data());
		}
		uint32_t present_cnt = 0;
		vkGetPhysicalDeviceSurfacePresentModesKHR(dvc, sfc, &present_cnt, nullptr);
		if (present_cnt != 0) {
			PresentModes.resize(present_cnt);
			vkGetPhysicalDeviceSurfacePresentModesKHR(dvc, sfc, &present_cnt, PresentModes.data());
		}
	}

	VkSurfaceFormatKHR SwapchainInfo::GetBestFormat() const{
		if (Formats.size() == 1 && Formats.front().format == VK_FORMAT_UNDEFINED) {
			return { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
		}
		else {
			for (const auto& fmt : Formats) {
				if (fmt.format == VK_FORMAT_B8G8R8A8_UNORM && fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
					return { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
				}
			}
			return Formats.front();
		}
	}

	VkPresentModeKHR SwapchainInfo::GetBestPresentMode() const{
		VkPresentModeKHR result = VK_PRESENT_MODE_FIFO_KHR;
		for (const auto& mode : PresentModes) {
			// Best mix of all modes.
			if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
				return VK_PRESENT_MODE_MAILBOX_KHR;
			}
			else if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
				// FIFO not always supported by driver, in which case this is our best bet.
				result = VK_PRESENT_MODE_IMMEDIATE_KHR;
			}
		}
		return result;
	}

	VkExtent2D SwapchainInfo::ChooseSwapchainExtent(const Instance* instance) const{
		if (Capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
			return Capabilities.currentExtent;
		}
		else {
			int glfw_width, glfw_height;
			glfwGetWindowSize(reinterpret_cast<const InstanceGLFW*>(instance)->Window, &glfw_width, &glfw_height);
			VkExtent2D actual_extent = { static_cast<uint32_t>(glfw_width), static_cast<uint32_t>(glfw_height) };
			actual_extent.width = std::max(Capabilities.minImageExtent.width, std::min(Capabilities.maxImageExtent.width, actual_extent.width));
			actual_extent.height = std::max(Capabilities.minImageExtent.height, std::min(Capabilities.maxImageExtent.height, actual_extent.height));
			return actual_extent;
		}
	}

	Swapchain::~Swapchain(){
		Destroy();
	}

	void Swapchain::Init(const Instance * _instance, const PhysicalDevice * _phys_device, const Device * _device) {
		instance = _instance;
		device = _device;
		phys_device = _phys_device;
		Info = new SwapchainInfo(_phys_device->vkHandle(), _instance->GetSurface());
		VkSurfaceFormatKHR surf_fmt = Info->GetBestFormat();
		ColorFormat = surf_fmt.format;
		VkPresentModeKHR present_mode = Info->GetBestPresentMode();
		Extent = Info->ChooseSwapchainExtent(instance);

		// Create one more image than minspec to implement triple buffering (in hope we got mailbox present mode)
		ImageCount = Info->Capabilities.minImageCount + 1;
		if (Info->Capabilities.maxImageCount > 0 && ImageCount > Info->Capabilities.maxImageCount) {
			ImageCount = Info->Capabilities.maxImageCount;
		}

		VkSwapchainCreateInfoKHR create_info = vk_swapchain_create_info_base;
		create_info.surface = instance->GetSurface();
		create_info.imageFormat = surf_fmt.format;
		create_info.imageColorSpace = surf_fmt.colorSpace;
		create_info.imageExtent = Extent;
		create_info.imageArrayLayers = 1;
		create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

		uint32_t indices[2] = { static_cast<uint32_t>(device->QueueFamilyIndices.Present), static_cast<uint32_t>(device->QueueFamilyIndices.Graphics) };
		if (device->QueueFamilyIndices.Present != device->QueueFamilyIndices.Graphics) {
			create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			create_info.queueFamilyIndexCount = 2;
			create_info.pQueueFamilyIndices = indices;
		}
		else {
			create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
			create_info.queueFamilyIndexCount = 0;
			create_info.pQueueFamilyIndices = nullptr;
		}

		create_info.minImageCount = ImageCount;
		create_info.preTransform = Info->Capabilities.currentTransform;
		create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		create_info.presentMode = present_mode;
		create_info.clipped = VK_TRUE;
		create_info.oldSwapchain = VK_NULL_HANDLE;

		VkResult result = vkCreateSwapchainKHR(device->vkHandle(), &create_info, AllocCallbacks, &handle);
		VkAssert(result);

		// Setup swap images
		vkGetSwapchainImagesKHR(device->vkHandle(), handle, &ImageCount, nullptr);
		Images.resize(ImageCount);
		vkGetSwapchainImagesKHR(device->vkHandle(), handle, &ImageCount, Images.data());

		// Setup image views
		ImageViews.resize(ImageCount);
		for (uint32_t i = 0; i < ImageCount; ++i) {
			VkImageViewCreateInfo iv_info = vk_image_view_create_info_base;
			iv_info.image = Images[i];
			iv_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
			iv_info.format = ColorFormat;
			iv_info.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
			iv_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			iv_info.subresourceRange.baseMipLevel = 0;
			iv_info.subresourceRange.levelCount = 1;
			iv_info.subresourceRange.baseArrayLayer = 0;
			iv_info.subresourceRange.layerCount = 1;
			result = vkCreateImageView(device->vkHandle(), &iv_info, AllocCallbacks, &ImageViews[i]);
		}
	}

	void Swapchain::Recreate(){
		delete Info;
		Info = new SwapchainInfo(phys_device->vkHandle(), instance->GetSurface());
		VkSurfaceFormatKHR surf_fmt = Info->GetBestFormat();
		ColorFormat = surf_fmt.format;
		VkPresentModeKHR present_mode = Info->GetBestPresentMode();
		Extent = Info->ChooseSwapchainExtent(instance);

		// Create one more image than minspec to implement triple buffering (in hope we got mailbox present mode)
		ImageCount = Info->Capabilities.minImageCount + 1;
		if (Info->Capabilities.maxImageCount > 0 && ImageCount > Info->Capabilities.maxImageCount) {
			ImageCount = Info->Capabilities.maxImageCount;
		}

		VkSwapchainCreateInfoKHR create_info = vk_swapchain_create_info_base;
		create_info.surface = instance->GetSurface();
		create_info.imageFormat = surf_fmt.format;
		create_info.imageColorSpace = surf_fmt.colorSpace;
		create_info.imageExtent = Extent;
		create_info.imageArrayLayers = 1;
		create_info.minImageCount = ImageCount;
		create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

		uint32_t indices[2] = { static_cast<uint32_t>(device->QueueFamilyIndices.Present), static_cast<uint32_t>(device->QueueFamilyIndices.Graphics) };
		if (device->QueueFamilyIndices.Present != device->QueueFamilyIndices.Graphics) {
			create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
			create_info.queueFamilyIndexCount = 2;
			create_info.pQueueFamilyIndices = indices;
		}
		else {
			create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
			create_info.queueFamilyIndexCount = 0;
			create_info.pQueueFamilyIndices = nullptr;
		}

		create_info.preTransform = Info->Capabilities.currentTransform;
		create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		create_info.presentMode = present_mode;
		create_info.clipped = VK_TRUE;
		create_info.oldSwapchain = handle;

		VkResult result = vkCreateSwapchainKHR(device->vkHandle(), &create_info, AllocCallbacks, &handle);
		VkAssert(result);

		// Setup swap images
		vkGetSwapchainImagesKHR(device->vkHandle(), handle, &ImageCount, nullptr);
		Images.resize(ImageCount);
		vkGetSwapchainImagesKHR(device->vkHandle(), handle, &ImageCount, Images.data());

		// Setup image views
		ImageViews.resize(ImageCount);
		for (uint32_t i = 0; i < ImageCount; ++i) {
			VkImageViewCreateInfo iv_info = vk_image_view_create_info_base;
			iv_info.image = Images[i];
			iv_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
			iv_info.format = ColorFormat;
			iv_info.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
			iv_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			iv_info.subresourceRange.baseMipLevel = 0;
			iv_info.subresourceRange.levelCount = 1;
			iv_info.subresourceRange.baseArrayLayer = 0;
			iv_info.subresourceRange.layerCount = 1;
			result = vkCreateImageView(device->vkHandle(), &iv_info, AllocCallbacks, &ImageViews[i]);
		}
	}

	void Swapchain::Destroy(){
		for (const auto& view : ImageViews) {
			if (view != VK_NULL_HANDLE) {
				vkDestroyImageView(device->vkHandle(), view, AllocCallbacks);
			}
		}
		if (handle != VK_NULL_HANDLE) {
			vkDestroySwapchainKHR(device->vkHandle(), handle, AllocCallbacks);
		}
	}

	const VkSwapchainKHR& Swapchain::vkHandle() const{
		return handle;
	}

	Swapchain::operator VkSwapchainKHR() const{
		return handle;
	}

}
