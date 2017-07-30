#pragma once
#ifndef VULPES_STAR_SCENE_H
#define VULPES_STAR_SCENE_H

#include "stdafx.h"
#include "BaseScene.h"
#include "resource/PipelineCache.h"
#include "engine/bodies/star/Star.h"
#include "engine/objects/Skybox.h"

namespace star_scene {
	
	using namespace vulpes;

	class StarScene : public BaseScene {
	public:

		StarScene() : BaseScene(3) {

			pipelineCache = std::make_shared<PipelineCache>(device.get(), static_cast<int16_t>(typeid(StarScene).hash_code()));

			star = std::make_unique<Star>(device.get(), 5, 3000.0f, 4000, instance->GetProjectionMatrix());
			skybox = std::make_unique<obj::Skybox>(device.get());
			auto gui_cache = std::make_shared<PipelineCache>(device.get(), static_cast<uint16_t>(typeid(imguiWrapper).hash_code()));
			gui = std::make_unique<imguiWrapper>();
			gui->Init(device.get(), gui_cache, renderPass->vkHandle());
			gui->UploadTextureData(transferPool.get());

			instance->SetCamPos(glm::vec3(3300.0f, 0.0f, 0.0f));

			VkQueue transfer = device->TransferQueue(0);

			star->BuildMesh(transferPool.get());
			star->BuildPipeline(renderPass->vkHandle(), pipelineCache);

			skybox->CreateData(transferPool.get(), transfer, instance->GetProjectionMatrix());
			skybox->CreatePipeline(renderPass->vkHandle(), pipelineCache);

			SetupFramebuffers();

		}

		virtual void RecreateObjects() override {
			star = std::make_unique<Star>(device.get(), 5, 3000.0f, 4000, instance->GetProjectionMatrix());
			skybox = std::make_unique<obj::Skybox>(device.get());
			VkQueue transfer = device->TransferQueue(0);
			star->BuildMesh(transferPool.get());
			star->BuildPipeline(renderPass->vkHandle(), pipelineCache);
			skybox->CreateData(transferPool.get(), transfer, instance->GetProjectionMatrix());
			skybox->CreatePipeline(renderPass->vkHandle(), pipelineCache);
		}

		virtual void WindowResized() override {
			skybox.reset();
			star.reset();
		}

		~StarScene() {
			gui.reset();
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
			renderpass_begin.renderPass = renderPass->vkHandle();

			static VkCommandBufferInheritanceInfo inherit_info = vk_command_buffer_inheritance_info_base;
			inherit_info.renderPass = renderPass->vkHandle();
			inherit_info.subpass = 0;

			for (uint32_t i = 0; i < graphicsPool->size(); ++i) {

				std::vector<VkCommandBuffer> secondary_buffers;

				gui->NewFrame(instance.get(), false);

				ImGui::Begin("Star Parameters");
				ImGui::DragFloat("Noise Frequency", &star->fsUboData.noiseParams.frequency, 0.001f, 0.001f, 2.0f);
				ImGui::DragInt("Noise Octaves", &noiseOctBuffer, 0.1f);
				star->fsUboData.noiseParams.octaves = static_cast<float>(noiseOctBuffer);
				ImGui::DragFloat("Noise Lacunarity", &star->fsUboData.noiseParams.lacunarity, 0.001f, 1.0f, 2.60f);
				ImGui::DragFloat("Noise Persistence", &star->fsUboData.noiseParams.persistence, 0.001f, 0.01f, 0.9f);
				ImGui::End();

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
				VkCommandBuffer& gui_buffer = secondaryPool->GetCmdBuffer(2 + (i * swapchain->ImageCount));

				renderGUI(gui_buffer, begin_info, i);
				secondary_buffers.push_back(gui_buffer);

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
					vkCmdSetViewport(star_buffer, 0, 1, &viewport);
					vkCmdSetScissor(star_buffer, 0, 1, &scissor);
					star->RecordCommands(star_buffer);
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
			std::chrono::system_clock::time_point a = std::chrono::system_clock::now();
			std::chrono::system_clock::time_point b = std::chrono::system_clock::now();
			static constexpr double frame_time_desired = 16.0; // frametime desired in ms, 120Hz
			
			while (!glfwWindowShouldClose(instance->Window)) {

				a = std::chrono::system_clock::now();
				std::chrono::duration<double, std::milli> work_time = a - b;

				if (work_time.count() < frame_time_desired) {
					std::chrono::duration<double, std::milli> delta_ms(frame_time_desired - work_time.count());
					auto delta_ms_dur = std::chrono::duration_cast<std::chrono::milliseconds>(delta_ms);
					std::this_thread::sleep_for(std::chrono::milliseconds(delta_ms_dur.count()));
				}

				b = std::chrono::system_clock::now();


				float CurrentFrame = static_cast<float>(glfwGetTime());
				DeltaTime = CurrentFrame - LastFrame;
				LastFrame = CurrentFrame;
				glfwPollEvents();

				instance->UpdateMovement(DeltaTime);
				star->UpdateUBOs(instance->GetViewMatrix(), instance->GetCamPos());
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

		void renderGUI(VkCommandBuffer& cmd, const VkCommandBufferBeginInfo& begin_info, const size_t& frame_idx) {
			ImGui::Render();
			if (device->MarkersEnabled) {
				device->vkCmdInsertDebugMarker(graphicsPool->GetCmdBuffer(frame_idx), "Update GUI", glm::vec4(0.6f, 0.6f, 0.0f, 1.0f));
			}
			gui->UpdateBuffers();
			vkBeginCommandBuffer(cmd, &begin_info);
			if (device->MarkersEnabled) {
				device->vkCmdBeginDebugMarkerRegion(cmd, "Draw GUI", glm::vec4(0.6f, 0.7f, 0.0f, 1.0f));
			}
			gui->DrawFrame(cmd);
			if (device->MarkersEnabled) {
				device->vkCmdEndDebugMarkerRegion(cmd);
			}
			vkEndCommandBuffer(cmd);
		}

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
		std::unique_ptr<Star> star;
		std::unique_ptr<obj::Skybox> skybox;
		int noiseOctBuffer = 2;

	};

}
#endif // !VULPES_STAR_H
