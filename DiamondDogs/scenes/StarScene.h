#pragma once
#ifndef VULPES_STAR_SCENE_H
#define VULPES_STAR_SCENE_H

#include "stdafx.h"
#include "BaseScene.h"
#include "bodies\star\Star.h"
#include "engine\objects\Skybox.h"
#include "engine\renderer\render\GraphicsPipeline.h"
namespace star_scene {
	
	using namespace vulpes;

	class StarScene : public BaseScene {
	public:

		StarScene() : BaseScene(2) {

			pipelineCache = std::make_shared<PipelineCache>(device, static_cast<int16_t>(typeid(StarScene).hash_code()));

			object = new Star(device, 5, 300.0f, 4000, instance->GetProjectionMatrix());
			skybox = new obj::Skybox(device);

			instance->SetCamPos(glm::vec3(450.0f, 0.0f, 0.0f));

			VkQueue transfer = device->TransferQueue(0);

			object->BuildMesh();
			object->BuildPipeline(renderPass->vkHandle(), pipelineCache);

			skybox->CreateData(transferPool, transfer, instance->GetProjectionMatrix());
			skybox->CreatePipeline(renderPass->vkHandle(), pipelineCache);

			SetupFramebuffers();

		}

		virtual void RecreateObjects() override {
			object = new Star(device, 5, 300.0f, 4000, instance->GetProjectionMatrix());
			skybox = new obj::Skybox(device);
			VkQueue transfer = device->TransferQueue(0);
			object->BuildMesh();
			object->BuildPipeline(renderPass->vkHandle(), pipelineCache);
			skybox->CreateData(transferPool, transfer, instance->GetProjectionMatrix());
			skybox->CreatePipeline(renderPass->vkHandle(), pipelineCache);
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

			static const std::array<VkClearValue, 3> clear_values{ VkClearValue{ 0.025f, 0.025f, 0.085f, 1.0f }, VkClearValue{ 0.025f, 0.025f, 0.085f, 1.0f }, VkClearValue{ 1.0f, 0 } };

			// Given at each frame in framebuffer to describe layout of framebuffer
			static VkRenderPassBeginInfo renderpass_begin{
				VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
				nullptr,
				renderPass->vkHandle(),
				VK_NULL_HANDLE, // update this every frame
				VkRect2D{ VkOffset2D{ 0, 0 }, swapchain->Extent },
				static_cast<uint32_t>(clear_values.size()),
				clear_values.data(),
			};

			static VkCommandBufferInheritanceInfo inherit_info = vk_command_buffer_inheritance_info_base;
			inherit_info.renderPass = renderPass->vkHandle();
			inherit_info.subpass = 0;

			for (uint32_t i = 0; i < graphicsPool->size(); ++i) {

				std::vector<VkCommandBuffer> secondary_buffers;

				static VkCommandBufferBeginInfo begin_info = vk_command_buffer_begin_info_base;
				begin_info.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
				begin_info.pInheritanceInfo = nullptr;

				VkResult err = vkBeginCommandBuffer(graphicsPool->GetCmdBuffer(i), &begin_info);
				VkAssert(err);

				static VkDeviceSize offsets[1]{ 0 };
				renderpass_begin.framebuffer = framebuffers[i];
				renderpass_begin.renderArea.extent = swapchain->Extent;
				inherit_info.framebuffer = framebuffers[i];

				VkViewport viewport = vk_default_viewport;
				viewport.width = static_cast<float>(swapchain->Extent.width);
				viewport.height = static_cast<float>(swapchain->Extent.height);

				VkRect2D scissor = vk_default_viewport_scissor;
				scissor.extent.width = swapchain->Extent.width;
				scissor.extent.height = swapchain->Extent.height;
				
				vkCmdBeginRenderPass(graphicsPool->GetCmdBuffer(i), &renderpass_begin, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);

				begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT | VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
				begin_info.pInheritanceInfo = &inherit_info;

				VkCommandBuffer& skybox_buffer = secondaryPool->GetCmdBuffer(i * swapchain->ImageCount);
				VkCommandBuffer& star_buffer = secondaryPool->GetCmdBuffer(1 + (i * swapchain->ImageCount));

				{
					vkBeginCommandBuffer(skybox_buffer, &begin_info);
					vkCmdSetViewport(skybox_buffer, 0, 1, &viewport);
					vkCmdSetScissor(skybox_buffer, 0, 1, &scissor);
					skybox->RecordCommands(skybox_buffer);
					vkEndCommandBuffer(skybox_buffer);
					secondary_buffers.push_back(skybox_buffer);
				}

				{
					vkBeginCommandBuffer(star_buffer, &begin_info);
					vkCmdSetViewport(skybox_buffer, 0, 1, &viewport);
					vkCmdSetScissor(skybox_buffer, 0, 1, &scissor);
					object->RecordCommands(star_buffer);
					vkEndCommandBuffer(star_buffer);
					secondary_buffers.push_back(star_buffer);
				}

				vkCmdExecuteCommands(graphicsPool->GetCmdBuffer(i), static_cast<uint32_t>(secondary_buffers.size()), secondary_buffers.data());

				vkCmdEndRenderPass(graphicsPool->GetCmdBuffer(i));

				err = vkEndCommandBuffer(graphicsPool->GetCmdBuffer(i));
				VkAssert(err);

				secondary_buffers.clear();

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
				object->UpdateUBOs(instance->GetViewMatrix(), instance->GetCamPos());
				skybox->UpdateUBO(instance->GetViewMatrix());

				RecordCommands();
				draw_frame();

				vkResetCommandPool(device->vkHandle(), secondaryPool->vkHandle(), VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);
				vkResetCommandPool(device->vkHandle(), graphicsPool->vkHandle(), VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);
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
			vkQueueWaitIdle(device->GraphicsQueue());

		}

		std::shared_ptr<PipelineCache> pipelineCache;
		Star* object;
		obj::Skybox* skybox;
	};

}
#endif // !VULPES_STAR_H
