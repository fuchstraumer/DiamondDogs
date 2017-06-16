#include "stdafx.h"
#include "Image.h"
#include "engine/renderer\core\LogicalDevice.h"
#include "engine/renderer\command\CommandPool.h"

namespace vulpes {

	Image::Image(const Device * _parent) : parent(_parent) {}

	Image::~Image() {
		Destroy();
	}

	void Image::Destroy(){
		if (view != VK_NULL_HANDLE) {
			vkDestroyImageView(parent->vkHandle(), view, allocators);
		}
		if (memory != VK_NULL_HANDLE) {
			vkFreeMemory(parent->vkHandle(), memory, allocators);
		}
		if (image != VK_NULL_HANDLE) {
			vkDestroyImage(parent->vkHandle(), image, allocators);
		}
	}

	void Image::Create(const VkImageCreateInfo & create_info, const VkMemoryPropertyFlagBits& memory_flags) {
		this->createInfo = create_info;
		CreateImage(image, memory, parent, createInfo, memory_flags);
	}

	void Image::Create(const VkExtent3D& _extents, const VkFormat& _format, const VkImageUsageFlags& usage_flags, const VkImageLayout& init_layout) {
		extents = _extents;
		format = _format;
		usageFlags = usage_flags;
		CreateImage(image, memory, parent, extents, format, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, usageFlags, VK_IMAGE_TILING_OPTIMAL, init_layout);
	}

	void Image::CreateView(const VkImageViewCreateInfo & info){
		VkResult result = vkCreateImageView(parent->vkHandle(), &info, allocators, &view);
		VkAssert(result);
	}

	void Image::CreateView(const VkImageAspectFlags & aspect_flags){
		VkImageViewCreateInfo view_info = vk_image_view_create_info_base;
		view_info.subresourceRange.aspectMask = aspect_flags;
		view_info.image = image;
		view_info.format = format;
		VkResult result = vkCreateImageView(parent->vkHandle(), &view_info, allocators, &view);
		VkAssert(result);
	}

	void Image::TransitionLayout(const VkImageLayout & initial, const VkImageLayout & final, CommandPool* pool, VkQueue & queue) {
		VkImageMemoryBarrier barrier = vk_image_memory_barrier_base;
		barrier = GetMemoryBarrier(image, format, initial, final);
		VkCommandBuffer cmd = pool->StartSingleCmdBuffer();
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);	
		pool->EndSingleCmdBuffer(cmd, queue);
	}

	VkImageMemoryBarrier Image::GetMemoryBarrier(const VkImage& image, const VkFormat& img_format, const VkImageLayout & prev, const VkImageLayout & next){

		auto has_stencil = [&]()->bool {
			return img_format == VK_FORMAT_D32_SFLOAT_S8_UINT || img_format == VK_FORMAT_D24_UNORM_S8_UINT;
		};

		VkImageMemoryBarrier barrier = vk_image_memory_barrier_base;
		barrier.oldLayout = prev;
		barrier.newLayout = next;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;
		barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

		// Set right aspect mask.
		if (next == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			if (has_stencil()) {
				barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
			}
		}
		else {
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		}

		if (prev == VK_IMAGE_LAYOUT_PREINITIALIZED && next == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
			barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		}
		else if (prev == VK_IMAGE_LAYOUT_PREINITIALIZED && next == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
			barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		}
		else if (prev == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && next == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		}
		else if (prev == VK_IMAGE_LAYOUT_UNDEFINED && next == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		}
		else if (prev == VK_IMAGE_LAYOUT_UNDEFINED && next == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		}
		else {
			throw(std::runtime_error("Invalid image transition specified"));
		}
		return barrier;
	}

	void Image::CreateImage(VkImage & dest_image, VkDeviceMemory & dest_memory, const Device* parent, const VkExtent3D & extents, const VkFormat & image_format, const VkMemoryPropertyFlags & memory_flags, const VkImageUsageFlags & usage_flags, const VkImageTiling& tiling, const VkImageLayout& init_layout) {

		VkImageCreateInfo create_info = vk_image_create_info_base;
		create_info.imageType = VK_IMAGE_TYPE_2D;
		create_info.extent = extents;
		create_info.mipLevels = 1;
		create_info.arrayLayers = 1;
		create_info.format = image_format;
		create_info.tiling = tiling;
		create_info.initialLayout = init_layout;
		create_info.samples = VK_SAMPLE_COUNT_1_BIT;
		create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		create_info.usage = usage_flags;

		VkResult result = vkCreateImage(parent->vkHandle(), &create_info, nullptr, &dest_image);
		VkAssert(result);

		VkMemoryRequirements mem_reqs;
		vkGetImageMemoryRequirements(parent->vkHandle(), dest_image, &mem_reqs);

		VkMemoryAllocateInfo alloc_info = vk_allocation_info_base;
		alloc_info.allocationSize = mem_reqs.size;
		alloc_info.memoryTypeIndex = parent->GetMemoryTypeIdx(mem_reqs.memoryTypeBits, memory_flags);

		result = vkAllocateMemory(parent->vkHandle(), &alloc_info, nullptr, &dest_memory);
		VkAssert(result);

		result = vkBindImageMemory(parent->vkHandle(), dest_image, dest_memory, 0);
		VkAssert(result);
	}

	void Image::CreateImage(VkImage & dest_image, VkDeviceMemory & dest_memory, const Device * parent, const VkImageCreateInfo & create_info, const VkMemoryPropertyFlags & memory_flags) {
		VkResult result = vkCreateImage(parent->vkHandle(), &create_info, nullptr, &dest_image);
		VkAssert(result);

		VkMemoryRequirements mem_reqs;
		vkGetImageMemoryRequirements(parent->vkHandle(), dest_image, &mem_reqs);

		VkMemoryAllocateInfo alloc_info = vk_allocation_info_base;
		alloc_info.allocationSize = mem_reqs.size;
		alloc_info.memoryTypeIndex = parent->GetMemoryTypeIdx(mem_reqs.memoryTypeBits, memory_flags);

		result = vkAllocateMemory(parent->vkHandle(), &alloc_info, nullptr, &dest_memory);
		VkAssert(result);

		result = vkBindImageMemory(parent->vkHandle(), dest_image, dest_memory, 0);
		VkAssert(result);
	}

	const VkImage & Image::vkHandle() const noexcept{
		return image;
	}

	const VkImageView & Image::View() const noexcept{	
		return view;
	}

	const VkDeviceMemory & Image::Memory() const noexcept{
		return memory;
	}

	VkExtent3D Image::GetExtents() const noexcept{
		return extents;
	}

	VkFormat Image::Format() const noexcept{
		return format;
	}

	void Image::SetFormat(const VkFormat & _format) noexcept {
		format = _format;
	}


}
