#pragma once
#ifndef VULPES_STAR_SCENE_H
#define VULPES_STAR_SCENE_H

#include "stdafx.h"
#include "BaseScene.h"
#include "bodies\star\Star.h"
#include "engine\objects\Skybox.h"
#include "engine\renderer\objects\render\GraphicsPipeline.h"
namespace star_scene {
	
	using namespace vulpes;

	class StarScene : public BaseScene {
	public:

		StarScene() : BaseScene() {
			CreateCommandPools();
			SetupRenderpass();
			SetupDepthStencil();
			pipelineCache = std::make_shared<PipelineCache>(device);
			object = new Star(device, 5, 300.0f, 4000, instance->GetProjectionMatrix());
			skybox = new Skybox(device);
			instance->SetCamPos(glm::vec3(450.0f, 0.0f, 0.0f));
			VkQueue transfer;
			device->TransferQueue(0, transfer);
			object->BuildMesh(transferPool, transfer);
			object->BuildPipeline(renderPass->vkHandle(), swapchain, pipelineCache);
			skybox->CreateData(transferPool, transfer, instance->GetProjectionMatrix());
			skybox->CreatePipeline(renderPass->vkHandle(), swapchain, pipelineCache);
			SetupFramebuffers();
			
			RecordCommands();
		}

		virtual void RecreateObjects() override {
			object = new Star(device, 5, 300.0f, 4000, instance->GetProjectionMatrix());
			skybox = new Skybox(device);
			VkQueue transfer;
			device->TransferQueue(0, transfer);
			object->BuildMesh(transferPool, transfer);
			object->BuildPipeline(renderPass->vkHandle(), swapchain, pipelineCache);
			skybox->CreateData(transferPool, transfer, instance->GetProjectionMatrix());
			skybox->CreatePipeline(renderPass->vkHandle(), swapchain, pipelineCache);
		}

		virtual void WindowResized() override {
			delete object;
			delete skybox;
		}

		~StarScene() {
			delete skybox;
			delete object;
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
					object->RecordCommands(graphicsPool->GetCmdBuffer(i));

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
				instance->UpdateMovement(DeltaTime);

				draw_frame();
				object->UpdateUBOs(instance->GetViewMatrix(), instance->GetCamPos());
				skybox->UpdateUBO(instance->GetViewMatrix());
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
		Star* object;
		Skybox* skybox;
	};

}
#endif // !VULPES_STAR_H
