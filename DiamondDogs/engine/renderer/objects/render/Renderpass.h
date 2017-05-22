#pragma once
#ifndef VULPES_VK_RENDER_PASS_H
#define VULPES_VK_RENDER_PASS_H

#include "stdafx.h"
#include "engine/renderer/objects\ForwardDecl.h"
#include "engine/renderer/objects\NonCopyable.h"

namespace vulpes {

	class Renderpass : public NonCopyable {
	public:

		Renderpass(const Device* dvc, const VkRenderPassCreateInfo& create_info);

		~Renderpass();

		const VkRenderPass& vkHandle() const noexcept;
		operator VkRenderPass() const noexcept;

		const VkRenderPassCreateInfo& CreateInfo() const noexcept;

	private:
		const Device* parent;
		VkRenderPass handle;
		VkRenderPassCreateInfo createInfo;
		const VkAllocationCallbacks* allocators = nullptr;
	};

}
#endif // !VULPES_VK_RENDER_PASS_H
