#pragma once
#ifndef VULPES_VK_FRAMEBUFFER_H
#define VULPES_VK_FRAMEBUFFER_H

#include "stdafx.h"
#include "engine/renderer\ForwardDecl.h"
#include "engine/renderer\NonCopyable.h"
#include "engine/renderer/resource/Image.h"
namespace vulpes {

	class Framebuffer : public NonCopyable {
	public:

		virtual ~Framebuffer();

		Framebuffer(const Device* parent, const VkFramebufferCreateInfo& create_info);
		Framebuffer& operator=(Framebuffer&& other) noexcept;
		Framebuffer(Framebuffer&& other) noexcept;
		const VkFramebuffer& vkHandle() const noexcept;
		
		void Destroy();

	protected:

		const Device* parent;
		const VkAllocationCallbacks* allocators = nullptr;
		VkFramebuffer handle;
	};

	using hdr_framebuffer_t = std::integral_constant<int, 0>;
	using bloom_framebuffer_t = std::integral_constant<int, 1>;

	template<typename offscreen_framebuffer_type>
	class OffscreenFramebuffer : public Framebuffer {
	public:

		OffscreenFramebuffer(const Device* parent, const VkFramebufferCreateInfo& create_info);

		void RecordRenderCmds(VkCommandBuffer& cmd_buffer) const;

	protected:
		
		void createAttachments();
		void createAttachmentDescriptions();
		void createAttachmentReferences();
		void setupSubpassDependencies();

		void createRenderpass();
		void createViews();
		void createFramebuffer();
		void createSampler();

		std::vector<Image> attachments;
		std::vector<VkAttachmentDescription> attachmentDescriptions;
		std::vector<VkAttachmentReference> attachmentReferences;

		VkRenderPass renderpass;
		VkSemaphore semaphore = VK_NULL_HANDLE;
		VkSampler sampler;
	};

	template<>
	inline void OffscreenFramebuffer<hdr_framebuffer_t>::createAttachments() {

	}
}
#endif // !VULPES_VK_FRAMEBUFFER_H
