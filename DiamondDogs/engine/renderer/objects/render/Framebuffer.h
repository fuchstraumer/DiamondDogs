#pragma once
#ifndef VULPES_VK_FRAMEBUFFER_H
#define VULPES_VK_FRAMEBUFFER_H

#include "stdafx.h"
#include "engine/renderer/objects\ForwardDecl.h"
#include "engine/renderer/objects\NonCopyable.h"

namespace vulpes {

	class Framebuffer : public NonCopyable {
	public:

		Framebuffer(const Device* parent, const VkFramebufferCreateInfo& create_info);
		Framebuffer& operator=(Framebuffer&& other) noexcept;
		Framebuffer(Framebuffer&& other) noexcept;
		const VkFramebuffer& vkHandle() const noexcept;
		operator VkFramebuffer() const noexcept;
		
		void Destroy();

	private:

		const Device* parent;
		const VkAllocationCallbacks* allocators = nullptr;
		VkFramebuffer handle;
	};
}
#endif // !VULPES_VK_FRAMEBUFFER_H
