#pragma once
#ifndef VULPES_TERRAIN_SCENE_H
#define VULPES_TERRAIN_SCENE_H

#include "stdafx.h"
#include "BaseScene.hpp"
#include "engine/objects/Skybox.hpp"
#include "engine/subsystems/terrain/TerrainQuadtree.hpp"
#include "render/GraphicsPipeline.hpp"
#include "resource/DescriptorPool.hpp"

namespace terrain_scene {

	using namespace vulpes;

	class TerrainScene : public BaseScene {
	public:

		TerrainScene() : BaseScene(3) {

			terrain::NodeRenderer::DrawAABBs = false;
			renderSkybox = true;

			instance->SetCamPos(glm::vec3(0.0f, 400.0f, 0.0f));

			SetupRenderpass(vulpes::Instance::VulpesInstanceConfig.MSAA_SampleCount);

			createDescriptorPool();
			createTerrain();
			createSkybox();
			createGUI();

			SetupFramebuffers();
			secondaryBuffers.resize(3);
		}

		~TerrainScene() {	
			skybox.reset();
			terrain.reset();
			gui.reset();
			descriptorPool.reset();
		}

		virtual void WindowResized() override {
			skybox.reset();
			terrain.reset();
			gui.reset();
			descriptorPool.reset();
		}

		virtual void RecreateObjects() override {
			createDescriptorPool();
			createTerrain();
			createSkybox();
			createGUI();
		}

		virtual void RecordCommands() override {

			terrain->UpdateQuadtree(instance->GetCamPos(), instance->GetViewMatrix());
			skybox->UpdateUBO(instance->GetViewMatrix());
			// Clear color value, clear depth value
			static const std::array<VkClearValue, 3> clear_values{ VkClearValue{ 0.025f, 0.025f, 0.085f, 1.0f }, VkClearValue{ 0.025f, 0.025f, 0.085f, 1.0f }, VkClearValue{ 1.0f, 0 } };

			// Given at each frame in framebuffer to describe layout of framebuffer
			static VkRenderPassBeginInfo renderpass_begin{
				VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
				nullptr,
				VK_NULL_HANDLE, // Frequently updated as well, static doesn't re-init on swapchain recreate
				VK_NULL_HANDLE, // update this every frame
				VkRect2D{ VkOffset2D{0, 0}, swapchain->Extent },
				static_cast<uint32_t>(clear_values.size()),
				clear_values.data(),
			};

			renderpass_begin.renderPass = renderPass->vkHandle();
			ImGui::SetNextWindowSize(ImVec2(550, 680), ImGuiSetCond_FirstUseEver);
			ImGui::Begin("Test Window");
			ImGui::Checkbox("Render Skybox", &renderSkybox);
			ImGui::End();
			static VkCommandBufferInheritanceInfo inherit_info = vk_command_buffer_inheritance_info_base;
			inherit_info.renderPass = renderPass->vkHandle();
			inherit_info.subpass = 0;

			for (uint32_t i = 0; i < graphicsPool->size(); ++i) { 

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

				VkCommandBuffer& terrain_buffer = secondaryPool->GetCmdBuffer(i * 3);
				VkCommandBuffer& skybox_buffer = secondaryPool->GetCmdBuffer(1 + (i * 3));
				VkCommandBuffer& gui_buffer = secondaryPool->GetCmdBuffer(2 + (i * 3));

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
					secondaryBuffers[i].push_back(skybox_buffer);
				}
				else {
					vkBeginCommandBuffer(skybox_buffer, &begin_info);
					vkEndCommandBuffer(skybox_buffer);
				}

				terrain->RenderNodes(terrain_buffer, begin_info, instance->GetViewMatrix(), instance->GetCamPos(), viewport, scissor);
				secondaryBuffers[i].push_back(terrain_buffer);

				if (device->MarkersEnabled) {
					device->vkCmdInsertDebugMarker(graphicsPool->GetCmdBuffer(i), "Update GUI", glm::vec4(0.6f, 0.6f, 0.0f, 1.0f));
				}
				
				renderGUI(gui_buffer, begin_info, i);
				secondaryBuffers[i].push_back(gui_buffer);

				vkCmdExecuteCommands(graphicsPool->GetCmdBuffer(i), static_cast<uint32_t>(secondaryBuffers[i].size()), secondaryBuffers[i].data());

				vkCmdEndRenderPass(graphicsPool->GetCmdBuffer(i));

				if (device->MarkersEnabled) {
					device->vkCmdEndDebugMarkerRegion(graphicsPool->GetCmdBuffer(i));
				}

				err = vkEndCommandBuffer(graphicsPool->GetCmdBuffer(i));
				VkAssert(err);

			}

		}

	private:

		void createTerrain() {
			terrain = std::make_unique<terrain::TerrainQuadtree>(device.get(), transferPool.get(), 1.30f, 1, 10000.0, glm::vec3(0.0f));
			terrain->SetupNodePipeline(renderPass->vkHandle(), instance->GetProjectionMatrix());
		}

		void createSkybox() {
			skybox = std::make_unique<Skybox>(device.get());
			skybox->CreateData(transferPool.get(), descriptorPool.get(), instance->GetProjectionMatrix());
			skybox->CreatePipeline(renderPass->vkHandle());
		}

		void createGUI() {

			gui = std::make_unique<imguiWrapper>();
			gui->Init(device.get(), renderPass->vkHandle());
			gui->UploadTextureData(transferPool.get());

		}

		void createDescriptorPool() {

			descriptorPool = std::make_unique<DescriptorPool>(device.get(), 1);
			descriptorPool->AddResourceType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1);
			descriptorPool->Create();

		}

		virtual void imguiDrawcalls() override {

		

		}

		virtual void endFrame(const size_t& idx) override {
			VkResult result = vkWaitForFences(device->vkHandle(), 1, &presentFences[idx], VK_TRUE, vk_default_fence_timeout);
			VkAssert(result);
			secondaryBuffers[idx].clear();
			secondaryBuffers[idx].shrink_to_fit();
			result = vkResetFences(device->vkHandle(), 1, &presentFences[idx]);
			VkAssert(result);
		}

		std::shared_ptr<PipelineCache> pipelineCache;
		std::vector<std::vector<VkCommandBuffer>> secondaryBuffers;
		std::unique_ptr<terrain::TerrainQuadtree> terrain;
		std::unique_ptr<Skybox> skybox;
		std::unique_ptr<DescriptorPool> descriptorPool;
		bool renderSkybox;

	};

}


#endif // !VULPES_TERRAIN_SCENE_H
