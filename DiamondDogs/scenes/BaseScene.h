#pragma once
#ifndef VULPES_VK_BASE_SCENE_H
#define VULPES_VK_BASE_SCENE_H

#include "stdafx.h"

#include "engine\renderer\objects\core\Instance.h"
#include "engine\renderer\objects\core\LogicalDevice.h"
#include "engine\renderer\objects\core\PhysicalDevice.h"
#include "engine\renderer\objects\render\Swapchain.h"
#include "engine\renderer\objects\render\Renderpass.h"
#include "engine\renderer\objects\render\Framebuffer.h"
#include "engine\renderer\objects\command\CommandPool.h"
#include "engine\renderer\objects\render\DepthStencil.h"

namespace vulpes {

	class BaseScene {
	public:

		BaseScene();

		~BaseScene();

		virtual void CreateCommandPools();

		virtual void SetupRenderpass();

		virtual void SetupDepthStencil();

		virtual void SetupFramebuffers();

	protected:

		VkSemaphore semaphores[2];
		InstanceGLFW* instance;
		Device* device;
		Swapchain* swapchain;
		std::vector<Framebuffer> framebuffers;
		DepthStencil* depthStencil;
		CommandPool *transferPool, *graphicsPool;
		Renderpass* renderPass;
	};

}

#endif // !VULPES_VK_BASE_SCENE_H
