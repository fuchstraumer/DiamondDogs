#pragma once
#ifndef VULPES_TERRAIN_SCENE_H
#define VULPES_TERRAIN_SCENE_H

#include "stdafx.h"
#include "VulpesRender/include/BaseScene.h"
#include "engine\objects\Skybox.h"
#include "engine\terrain\TerrainQuadtree.h"
#include "VulpesRender/include/render\GraphicsPipeline.h"

namespace terrain_scene {

	using namespace vulpes;

	class TerrainScene : public BaseScene {
	public:

		TerrainScene() : BaseScene(3) {
			terrain::NodeRenderer::DrawAABBs = false;
			renderSkybox = true;
			const std::type_info& id = typeid(TerrainScene);
			uint16_t hash = static_cast<uint16_t>(id.hash_code());
			pipelineCache = std::make_shared<PipelineCache>(device.get(), hash);

			VkQueue transfer;
			transfer = device->GraphicsQueue(0);
			instance->SetCamPos(glm::vec3(0.0f, 400.0f, 0.0f));
			object = new terrain::TerrainQuadtree(device.get(), 1.30f, 1, 10000.0, glm::vec3(0.0f));
			object->SetupNodePipeline(renderPass->vkHandle(), swapchain.get(), pipelineCache, instance->GetProjectionMatrix());

			skybox = new obj::Skybox(device.get());
			skybox->CreateData(transferPool.get(), transfer, instance->GetProjectionMatrix());
			skybox->CreatePipeline(renderPass->vkHandle(), pipelineCache);

			SetupFramebuffers();

			auto gui_cache = std::make_shared<PipelineCache>(device.get(), static_cast<uint16_t>(typeid(imguiWrapper).hash_code()));
			gui = std::make_unique<imguiWrapper>();
			gui->Init(device.get(), gui_cache, renderPass->vkHandle());
			gui->UploadTextureData(graphicsPool.get());
		}

		~TerrainScene() {	
			delete skybox;
			delete object;
		}

		virtual void WindowResized() override {

		}

		virtual void RecreateObjects() override {

		}

		virtual void RecordCommands() override {
			
			// Clear color value, clear depth value
			static const std::array<VkClearValue, 3> clear_values{ VkClearValue{ 0.025f, 0.025f, 0.085f, 1.0f }, VkClearValue{ 0.025f, 0.025f, 0.085f, 1.0f }, VkClearValue{ 1.0f, 0 } };

			// Given at each frame in framebuffer to describe layout of framebuffer
			static VkRenderPassBeginInfo renderpass_begin{
				VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
				nullptr,
				renderPass->vkHandle(),
				VK_NULL_HANDLE, // update this every frame
				VkRect2D{ VkOffset2D{0, 0}, swapchain->Extent },
				static_cast<uint32_t>(clear_values.size()),
				clear_values.data(),
			};

			static VkCommandBufferInheritanceInfo inherit_info = vk_command_buffer_inheritance_info_base;
			inherit_info.renderPass = renderPass->vkHandle();
			inherit_info.subpass = 0;

			for (uint32_t i = 0; i < graphicsPool->size(); ++i) {

				// holds secondary buffers
				std::vector<VkCommandBuffer> buffers;

				gui->NewFrame(instance.get(), true);

				VkCommandBufferBeginInfo begin_info = vk_command_buffer_begin_info_base;
				begin_info.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
				begin_info.pInheritanceInfo = nullptr;

				VkResult err = vkBeginCommandBuffer(graphicsPool->GetCmdBuffer(i), &begin_info);
				VkAssert(err);

				if (device->MarkersEnabled) {
					std::string region_name = std::string("Main render loop, Swapchain image ") + std::to_string(i);
					device->vkCmdBeginDebugMarkerRegion(graphicsPool->GetCmdBuffer(i), region_name.c_str(), glm::vec4(0.0f, 0.2f, 0.8f, 1.0f));
				}

				renderpass_begin.framebuffer = framebuffers[i];
				renderpass_begin.renderArea.extent = swapchain->Extent;
				inherit_info.framebuffer = framebuffers[i];

				static VkDeviceSize offsets[1]{ 0 };

				VkViewport viewport = vk_default_viewport;
				viewport.width = static_cast<float>(swapchain->Extent.width);
				viewport.height = static_cast<float>(swapchain->Extent.height);

				VkRect2D scissor = vk_default_viewport_scissor;
				scissor.extent.width = swapchain->Extent.width;
				scissor.extent.height = swapchain->Extent.height;

				vkCmdBeginRenderPass(graphicsPool->GetCmdBuffer(i), &renderpass_begin, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);

				begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT | VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
				inherit_info.framebuffer = framebuffers[i];
				begin_info.pInheritanceInfo = &inherit_info;

				VkCommandBuffer& terrain_buffer = secondaryPool->GetCmdBuffer(i * swapchain->ImageCount);
				VkCommandBuffer& skybox_buffer = secondaryPool->GetCmdBuffer(1 + (i * swapchain->ImageCount));
				VkCommandBuffer& gui_buffer = secondaryPool->GetCmdBuffer(2 + (i * swapchain->ImageCount));

				ImGui::Begin("Debug");
				ImGui::Checkbox("Render Skybox", &renderSkybox);
				ImGui::End();

				if (renderSkybox) {
					vkBeginCommandBuffer(skybox_buffer, &begin_info);
					if (device->MarkersEnabled) {
						device->vkCmdBeginDebugMarkerRegion(skybox_buffer, "Draw skybox", glm::vec4(0.8f, 0.0f, 0.9f, 1.0f));
					}
					vkCmdSetViewport(skybox_buffer, 0, 1, &viewport);
					vkCmdSetScissor(skybox_buffer, 0, 1, &scissor);
					skybox->RecordCommands(skybox_buffer);
					if (device->MarkersEnabled) {
						device->vkCmdEndDebugMarkerRegion(skybox_buffer);
					}
					vkEndCommandBuffer(skybox_buffer);
					buffers.push_back(skybox_buffer);
				}
				else {
					vkBeginCommandBuffer(skybox_buffer, &begin_info);
					vkEndCommandBuffer(skybox_buffer);
				}

				object->RenderNodes(terrain_buffer, begin_info, instance->GetViewMatrix(), instance->GetCamPos(), viewport, scissor);
				buffers.push_back(terrain_buffer);

				ImGui::Render();
				if (device->MarkersEnabled) {
					device->vkCmdInsertDebugMarker(graphicsPool->GetCmdBuffer(i), "Update GUI", glm::vec4(0.6f, 0.6f, 0.0f, 1.0f));
				}
				gui->UpdateBuffers();
				vkBeginCommandBuffer(gui_buffer, &begin_info);
				if (device->MarkersEnabled) {
					device->vkCmdBeginDebugMarkerRegion(gui_buffer, "Draw GUI", glm::vec4(0.6f, 0.7f, 0.0f, 1.0f));
				}
				gui->DrawFrame(gui_buffer);
				if (device->MarkersEnabled) {
					device->vkCmdEndDebugMarkerRegion(gui_buffer);
				}
				vkEndCommandBuffer(gui_buffer);
				buffers.push_back(gui_buffer);

				vkCmdExecuteCommands(graphicsPool->GetCmdBuffer(i), static_cast<uint32_t>(buffers.size()), buffers.data());

				vkCmdEndRenderPass(graphicsPool->GetCmdBuffer(i));

				if (device->MarkersEnabled) {
					device->vkCmdEndDebugMarkerRegion(graphicsPool->GetCmdBuffer(i));
				}

				err = vkEndCommandBuffer(graphicsPool->GetCmdBuffer(i));
				VkAssert(err);
				buffers.clear();
				buffers.shrink_to_fit();

			}

		}

		void RenderLoop() {

			float DeltaTime, LastFrame = 0.0f;

			while (!glfwWindowShouldClose(instance->Window)) {
				// Update frame time values
				float CurrentFrame = static_cast<float>(glfwGetTime());
				DeltaTime = CurrentFrame - LastFrame;
				instance->frameTime = DeltaTime;
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
		obj::Skybox* skybox;
		bool renderSkybox;
	};

}


#endif // !VULPES_TERRAIN_SCENE_H
