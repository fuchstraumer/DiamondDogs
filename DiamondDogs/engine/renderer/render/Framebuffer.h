#pragma once
#ifndef VULPES_VK_FRAMEBUFFER_H
#define VULPES_VK_FRAMEBUFFER_H

#include "stdafx.h"
#include "engine/renderer\ForwardDecl.h"
#include "engine/renderer\NonCopyable.h"

namespace vulpes {

	class Framebuffer : public NonCopyable {
	public:

		Framebuffer(const Device* parent, const VkFramebufferCreateInfo& create_info);
		Framebuffer& operator=(Framebuffer&& other) noexcept;
		Framebuffer(Framebuffer&& other) noexcept;
		const VkFramebuffer& vkHandle() const noexcept;
		operator VkFramebuffer() const noexcept;
		
		void Destroy();

	protected:

		const Device* parent;
		const VkAllocationCallbacks* allocators = nullptr;
		VkFramebuffer handle;
	};


	class OffscreenFramebuffer : public Framebuffer {
		struct Attachment {
			VkImage image = VK_NULL_HANDLE;
			VkDeviceMemory memory = VK_NULL_HANDLE;
			VkImageView view = VK_NULL_HANDLE;
			VkFormat format = VK_FORMAT_UNDEFINED;
			void destroy(const Device* dvc);
		};
	public:

		OffscreenFramebuffer(const Device* parent, const VkFramebufferCreateInfo& create_info);

		std::array<Attachment, 2> ColorAttachments;
		Attachment DepthAttachment;
	protected:

		VkSampler sampler;
	};
}
#endif // !VULPES_VK_FRAMEBUFFER_H
