#pragma once
#ifndef VULPES_VK_BASE_SCENE_H
#define VULPES_VK_BASE_SCENE_H

#include "stdafx.h"

#include "engine\renderer\core\Instance.h"
#include "engine\renderer\core\LogicalDevice.h"
#include "engine\renderer\core\PhysicalDevice.h"
#include "engine\renderer\render\Swapchain.h"
#include "engine\renderer\render\Renderpass.h"
#include "engine\renderer\render\Framebuffer.h"
#include "engine\renderer\command\CommandPool.h"
#include "engine\renderer\render\DepthStencil.h"

namespace vulpes {

	class BaseScene {
	public:

		BaseScene(const size_t& num_secondary_buffers = 1, const uint32_t& width = DEFAULT_WIDTH, const uint32_t& height = DEFAULT_HEIGHT);

		~BaseScene();

		virtual void CreateCommandPools(const size_t& num_secondary_buffers);

		virtual void SetupRenderpass();

		virtual void SetupDepthStencil();

		virtual void SetupFramebuffers();

		virtual void RecreateSwapchain(const bool& windowed_fullscreen = false);

		virtual void WindowResized() = 0;

		virtual void RecreateObjects() = 0;

		virtual void RecordCommands() = 0;
	protected:

		uint32_t width, height;
		VkSemaphore semaphores[2];
		InstanceGLFW* instance;
		Device* device;
		Swapchain* swapchain;
		std::vector<Framebuffer> framebuffers;
		DepthStencil* depthStencil;
		CommandPool *transferPool, *graphicsPool, *secondaryPool;
		Renderpass* renderPass;
	};

}

#endif // !VULPES_VK_BASE_SCENE_H
