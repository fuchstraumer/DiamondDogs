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
			transfer = device->GraphicsQueue(4);

			instance->SetCamPos(glm::vec3(0.0f, 200.0f, 0.0f));


			skybox = new Skybox(device);
			skybox->CreateData(transferPool, transfer, instance->GetProjectionMatrix());
			skybox->CreatePipeline(renderPass->vkHandle(), swapchain, pipelineCache);

			SetupFramebuffers();

			RecordCommands();

		}

		~TerrainScene() {
			delete skybox;
			delete object;
		}

		virtual void CreateCommandPools() override {
			VkCommandPoolCreateInfo pool_info = vk_command_pool_info_base;
			pool_info.queueFamilyIndex = device->QueueFamilyIndices.Graphics;
			pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
			graphicsPool = new CommandPool(device, pool_info);
			graphicsPool->CreateCommandBuffers(swapchain->ImageCount);

			pool_info.queueFamilyIndex = device->QueueFamilyIndices.Graphics;
			transferPool = new CommandPool(device, pool_info);
			transferPool->CreateCommandBuffers(1);

			pool_info.queueFamilyIndex = device->QueueFamilyIndices.Transfer;
			terrainTransferPool = new TransferPool(device, pool_info);
			VkQueue transfer;
			device->TransferQueue(0, transfer);
			terrainTransferPool->SetSubmitQueue(transfer);
		}

		virtual void WindowResized() override {

		}

		virtual void RecreateObjects() override {

		}

		virtual void RecordCommands() override {
			for (uint32_t i = 0; i < graphicsPool->size(); ++i) {

				static VkCommandBufferBeginInfo begin = vk_command_buffer_begin_info_base;
				begin.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
				begin.pInheritanceInfo = nullptr;

				VkResult err = vkBeginCommandBuffer(graphicsPool->GetCmdBuffer(i), &begin);
				VkAssert(err);

				VkRenderPassBeginInfo rp_begin = vk_renderpass_begin_info_base;
				rp_begin.renderPass = *renderPass;
				rp_begin.framebuffer = framebuffers[i].vkHandle();
				rp_begin.renderArea.offset = { 0, 0 };
				rp_begin.renderArea.extent = swapchain->Extent;
				VkClearValue clear_color = { 0.025f, 0.025f, 0.085f, 1.0f };
				VkClearValue clear_depth = { 1.0f, 0 };
				std::array<VkClearValue, 2> clear_vals{ clear_color, clear_depth };
				rp_begin.clearValueCount = clear_vals.size();
				rp_begin.pClearValues = clear_vals.data();
				static VkDeviceSize offsets[1]{ 0 };

				vkCmdBeginRenderPass(graphicsPool->GetCmdBuffer(i), &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

				VkViewport viewport = vk_default_viewport;
				viewport.width = swapchain->Extent.width;
				viewport.height = swapchain->Extent.height;
				vkCmdSetViewport(graphicsPool->GetCmdBuffer(i), 0, 1, &viewport);

				VkRect2D scissor = vk_default_viewport_scissor;
				scissor.extent.width = swapchain->Extent.width;
				scissor.extent.height = swapchain->Extent.height;
				vkCmdSetScissor(graphicsPool->GetCmdBuffer(i), 0, 1, &scissor);

				skybox->RecordCommands(graphicsPool->GetCmdBuffer(i));
				object->BuildCommandBuffers(graphicsPool->GetCmdBuffer(i));

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

				draw_frame();

				for (size_t i = 0; i < swapchain->ImageCount; ++i) {
					vkResetCommandBuffer(graphicsPool->GetCmdBuffer(i), VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
				}
			}
			vkDeviceWaitIdle(device->vkHandle());
			glfwTerminate();
		}

	private:

		void draw_frame() {
			
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

		}

		std::shared_ptr<PipelineCache> pipelineCache;
		terrain::TerrainQuadtree* object;
		Skybox* skybox;
		TransferPool* terrainTransferPool;
	};

}


#endif // !VULPES_TERRAIN_SCENE_H
