#pragma once
#ifndef VULPES_VK_IMAGE_H
#define VULPES_VK_IMAGE_H
#include "stdafx.h"
#include "engine/renderer/objects/ForwardDecl.h"
#include "engine/renderer/objects/NonCopyable.h"

/*

	Wraps the common image creation, transfer, and staging methods. 

	Texture derives from this, and so does DepthStencil.
	
*/

namespace vulpes {

	class Image : public NonCopyable {
	public:

		Image(const Device* parent);

		virtual ~Image();

		void Destroy();

		void Create(const VkExtent3D& extents, const VkFormat& format, const VkImageUsageFlags& usage_flags, const VkImageLayout& init_layout = VK_IMAGE_LAYOUT_PREINITIALIZED);
		
		void CreateView(const VkImageViewCreateInfo& info);
		
		void CreateView(const VkImageAspectFlags& aspect_flags);

		void TransitionLayout(const VkImageLayout& initial, const VkImageLayout& final, CommandPool* cmd, VkQueue& queue);

		static VkImageMemoryBarrier GetMemoryBarrier(const VkImage& image, const VkFormat& img_format, const VkImageLayout& prev, const VkImageLayout& next);

		static void CreateImage(VkImage& dest_image, VkDeviceMemory& dest_memory, VkDeviceSize& dest_image_size, const Device* parent, const VkExtent3D& extents, const VkFormat& image_format, const VkMemoryPropertyFlags& memory_flags, const VkImageUsageFlags& usage_flags, const VkImageTiling& tiling = VK_IMAGE_TILING_OPTIMAL, const VkImageLayout& init_layout = VK_IMAGE_LAYOUT_PREINITIALIZED);

		static void CreateImage(VkImage& dest_image, VkDeviceMemory& dest_memory, VkDeviceSize& dest_image_size, const Device* parent, const VkImageCreateInfo& create_info, const VkMemoryPropertyFlags & memory_flags);

		const VkImage& vkHandle() const noexcept;
		const VkImageView& View() const noexcept;
		const VkDeviceMemory& Memory() const noexcept;
		virtual VkExtent3D GetExtents() const noexcept;

		VkFormat Format() const noexcept;
		void SetFormat(const VkFormat& format) noexcept;

	protected:

		const Device* parent;
		const VkAllocationCallbacks* allocators = nullptr;

		VkImageCreateInfo createInfo;
		VkImageSubresource subresource;
		VkSubresourceLayout subresourceLayout;

		VkImage image;
		VkImageView view;
		VkDeviceMemory memory;

		VkImageLayout finalLayout;
		VkExtent3D extents;
		VkFormat format;
		VkImageUsageFlags usageFlags;
		VkDeviceSize imageDataSize;
	};

}

#endif // !VULPES_VK_IMAGE_H
