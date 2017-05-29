#pragma once
#ifndef VULPES_TERRAIN_SCENE_H
#define VULPES_TERRAIN_SCENE_H

#include "stdafx.h"
#include "BaseScene.h"
#include "engine\objects\Skybox.h"
#include "engine\terrain\TerrainQuadtree.h"
#include "engine\renderer\render\GraphicsPipeline.h"

namespace terrain_scene {

	using namespace vulpes;

	class TerrainScene : public BaseScene {
	public:

		TerrainScene() : BaseScene() {
			
			

			CreateCommandPools();
			SetupRenderpass();
			SetupDepthStencil();

			const std::type_info& id = typeid(TerrainScene);
			uint16_t hash = static_cast<uint16_t>(id.hash_code());

			pipelineCache = std::make_shared<PipelineCache>(device, hash);

			VkQueue transfer;
			transfer = device->GraphicsQueue(0);

			object = new terrain::TerrainQuadtree(device, 1.25f, 10, 1000.0, glm::vec3(0.0f));
			object->SetupNodePipeline(renderPass->vkHandle(), swapchain, pipelineCache, instance->GetProjectionMatrix());

			skybox = new Skybox(device);
			skybox->CreateData(transferPool, transfer, instance->GetProjectionMatrix());
			skybox->CreatePipeline(renderPass->vkHandle(), swapchain, pipelineCache);

			SetupFramebuffers();


		}

		~TerrainScene() {
			delete skybox;
			delete object;
			delete secondaryPool;
		}

		virtual void CreateCommandPools() override {
			VkCommandPoolCreateInfo pool_info = vk_command_pool_info_base;
			pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
			pool_info.queueFamilyIndex = device->QueueFamilyIndices.Graphics;
			graphicsPool = new CommandPool(device, pool_info);

			VkCommandBufferAllocateInfo alloc_info = vk_command_buffer_allocate_info_base;
			graphicsPool->CreateCommandBuffers(swapchain->ImageCount, alloc_info);

			
			pool_info.queueFamilyIndex = device->QueueFamilyIndices.Graphics;
			transferPool = new CommandPool(device, pool_info);
			transferPool->CreateCommandBuffers(1);

			pool_info.queueFamilyIndex = device->QueueFamilyIndices.Graphics;
			secondaryPool = new CommandPool(device, pool_info);
			alloc_info.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
			secondaryPool->CreateCommandBuffers(swapchain->ImageCount * 2, alloc_info);

		}

		virtual void WindowResized() override {

		}

		virtual void RecreateObjects() override {

		}

		virtual void RecordCommands() override {
			
			// Clear color value, clear depth value
			static const std::array<VkClearValue, 2> clear_values{ VkClearValue{ 0.025f, 0.025f, 0.085f, 1.0f }, VkClearValue{ 1.0f, 0 } };

			// Given at each frame in framebuffer to describe layout of framebuffer
			static VkRenderPassBeginInfo renderpass_begin{
				VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
				nullptr,
				renderPass->vkHandle(),
				VK_NULL_HANDLE, // update this every frame
				VkRect2D{ VkOffset2D{0, 0}, swapchain->Extent },
				2,
				clear_values.data(),
			};

			static VkCommandBufferInheritanceInfo inherit_info = vk_command_buffer_inheritance_info_base;
			inherit_info.renderPass = renderPass->vkHandle();
			inherit_info.subpass = 0;

			for (uint32_t i = 0; i < graphicsPool->size(); ++i) {

				VkCommandBufferBeginInfo begin_info = vk_command_buffer_begin_info_base;
				begin_info.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
				begin_info.pInheritanceInfo = nullptr;

				VkResult err = vkBeginCommandBuffer(graphicsPool->GetCmdBuffer(i), &begin_info);
				VkAssert(err);
				
				renderpass_begin.framebuffer = framebuffers[i].vkHandle();
				renderpass_begin.renderArea.extent = swapchain->Extent;
				inherit_info.framebuffer = framebuffers[i].vkHandle();

				static VkDeviceSize offsets[1]{ 0 };

				VkViewport viewport = vk_default_viewport;
				viewport.width = swapchain->Extent.width;
				viewport.height = swapchain->Extent.height;

				VkRect2D scissor = vk_default_viewport_scissor;
				scissor.extent.width = swapchain->Extent.width;
				scissor.extent.height = swapchain->Extent.height;

				vkCmdBeginRenderPass(graphicsPool->GetCmdBuffer(i), &renderpass_begin, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
				
				begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
				inherit_info.framebuffer = framebuffers[i].vkHandle();
				begin_info.pInheritanceInfo = &inherit_info;

				VkCommandBuffer& skybox_buffer = secondaryPool->GetCmdBuffer(i + swapchain->ImageCount);
				VkCommandBuffer& terrain_buffer = secondaryPool->GetCmdBuffer(i);

				vkBeginCommandBuffer(skybox_buffer, &begin_info);
				vkCmdSetViewport(skybox_buffer, 0, 1, &viewport);
				vkCmdSetScissor(skybox_buffer, 0, 1, &scissor);

				skybox->RecordCommands(skybox_buffer);

				vkEndCommandBuffer(skybox_buffer);;

				object->UpdateNodes(terrain_buffer, begin_info, instance->GetViewMatrix(), viewport, scissor);

				// Execute commands in secondary command buffer
				std::vector<VkCommandBuffer> buffers{ skybox_buffer, terrain_buffer };
				vkCmdExecuteCommands(graphicsPool->GetCmdBuffer(i), 2, buffers.data());

				vkCmdEndRenderPass(graphicsPool->GetCmdBuffer(i));
				err = vkEndCommandBuffer(graphicsPool->GetCmdBuffer(i));
				VkAssert(err);
			}
		}

		void RenderLoop() {

			float DeltaTime, LastFrame = 0.0f;

			while (!glfwWindowShouldClose(instance->Window)) {
				// Update frame time values
				float CurrentFrame = static_cast<float>(glfwGetTime());
				DeltaTime = CurrentFrame - LastFrame;
				LastFrame = CurrentFrame;
				glfwPollEvents();

				// Update things, THEN call draw_frame().
				instance->UpdateMovement(DeltaTime);
				auto pos = instance->GetCamPos();
				object->UpdateQuadtree(glm::dvec3(pos.x, pos.y, pos.z), instance->GetViewMatrix());
				skybox->UpdateUBO(instance->GetViewMatrix());

				RecordCommands();

				submit_frame();

				vkResetCommandPool(device->vkHandle(), secondaryPool->vkHandle(), VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);
				vkResetCommandPool(device->vkHandle(), graphicsPool->vkHandle(), VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);
			}
			vkDeviceWaitIdle(device->vkHandle());
			glfwTerminate();
		}

	private:

		void submit_frame() {
			
			uint32_t image_idx;
			vkAcquireNextImageKHR(device->vkHandle(), swapchain->vkHandle(), std::numeric_limits<uint64_t>::max(), semaphores[0], VK_NULL_HANDLE, &image_idx);
			VkSubmitInfo submit_info = vk_submit_info_base;
			VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
			submit_info.waitSemaphoreCount = 1;
			submit_info.pWaitSemaphores = &semaphores[0];
			submit_info.pWaitDstStageMask = wait_stages;
			submit_info.commandBufferCount = 1;
			submit_info.pCommandBuffers = &graphicsPool->GetCmdBuffer(image_idx);
			submit_info.signalSemaphoreCount = 1;
			submit_info.pSignalSemaphores = &semaphores[1];
			VkResult result = vkQueueSubmit(device->GraphicsQueue(), 1, &submit_info, VK_NULL_HANDLE);
			VkAssert(result);

			VkPresentInfoKHR present_info{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
			present_info.waitSemaphoreCount = 1;
			present_info.pWaitSemaphores = &semaphores[1];
			present_info.swapchainCount = 1;
			present_info.pSwapchains = &swapchain->vkHandle();
			present_info.pImageIndices = &image_idx;
			present_info.pResults = nullptr;

			vkQueuePresentKHR(device->GraphicsQueue(), &present_info);
			vkQueueWaitIdle(device->GraphicsQueue());
		}

		std::shared_ptr<PipelineCache> pipelineCache;
		terrain::TerrainQuadtree* object;
		Skybox* skybox;
		CommandPool* secondaryPool;
	};

}


#endif // !VULPES_TERRAIN_SCENE_H
