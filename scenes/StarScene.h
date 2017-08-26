#pragma once
#ifndef VULPES_STAR_SCENE_H
#define VULPES_STAR_SCENE_H

#include "stdafx.h"
#include "BaseScene.h"
#include "resource/PipelineCache.h"
#include "engine/bodies/star/Star.h"
#include "engine/objects/Skybox.h"
#include "engine/bodies/star/Corona.h"
#include "util/imguiTabs.h"

namespace star_scene {
	
	using namespace vulpes;

	static const char* tab_names[] = { "Star settings", "Corona settings", "Toggle settings" };
	static int tab_order[] = { 0, 1, 2 };
	static int tab_active = 0;
	static int noise_octave_buffer = 2;

	static void drawStarSettings(std::unique_ptr<Star>& star) {
		
	}

	static float size_buffer = 8500.0f;

	static void drawCoronaSettings(std::unique_ptr<Corona>& corona) {
		
	}

	enum class possibleTabs : int {
		STAR_SETTINGS,
		CORONA_SETTINGS,
		TOGGLE_SETTINGS,
	};

	class StarScene : public BaseScene {
	public:

		StarScene() : BaseScene(4) {

			instance->SetCamPos(glm::vec3(3300.0f, 0.0f, 0.0f));

			star = std::make_unique<Star>(device.get(), 5, 3000.0f, 4000, instance->GetProjectionMatrix());
			corona = std::make_unique<Corona>(device.get(), 8500.0f);
			skybox = std::make_unique<Skybox>(device.get());

			gui = std::make_unique<imguiWrapper>();
			gui->Init(device.get(), renderPass->vkHandle());
			gui->UploadTextureData(transferPool.get());

			VkQueue transfer = device->TransferQueue(0);

			star->BuildMesh(transferPool.get());
			star->BuildPipeline(renderPass->vkHandle());

			skybox->CreateData(transferPool.get(), transfer, instance->GetProjectionMatrix());
			skybox->CreatePipeline(renderPass->vkHandle());

			corona->Init(renderPass->vkHandle(), instance->GetProjectionMatrix());

			SetupFramebuffers();
			secondaryBuffers.resize(swapchain->ImageCount);

		}

		virtual void RecreateObjects() override {
			star = std::make_unique<Star>(device.get(), 5, 3000.0f, 4000, instance->GetProjectionMatrix());
			skybox = std::make_unique<Skybox>(device.get());
			corona = std::make_unique<Corona>(device.get(), 8500.0f);
			gui = std::make_unique<imguiWrapper>();
			VkQueue transfer = device->TransferQueue(0);
			star->BuildMesh(transferPool.get());
			star->BuildPipeline(renderPass->vkHandle());
			skybox->CreateData(transferPool.get(), transfer, instance->GetProjectionMatrix());
			skybox->CreatePipeline(renderPass->vkHandle());
			corona->Init(renderPass->vkHandle(), instance->GetProjectionMatrix());
			gui->Init(device.get(), renderPass->vkHandle());
			gui->UploadTextureData(transferPool.get());
		}

		virtual void WindowResized() override {
			gui.reset();
			skybox.reset();
			star.reset();
			corona.reset();
		}

		~StarScene() {
			skybox.reset();
			star.reset();
			corona.reset();
			gui.reset();
		}

		virtual void RecordCommands() override {

			static const std::array<VkClearValue, 3> clear_values{ VkClearValue{ 0.025f, 0.025f, 0.085f, 1.0f }, VkClearValue{ 0.025f, 0.025f, 0.085f, 1.0f }, VkClearValue{ 1.0f, 0 } };

			// Given at each frame in framebuffer to describe layout of framebuffer
			static VkRenderPassBeginInfo renderpass_begin{
				VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
				nullptr,
				VK_NULL_HANDLE,
				VK_NULL_HANDLE, // update this every frame
				VkRect2D{ VkOffset2D{ 0, 0 }, swapchain->Extent },
				static_cast<uint32_t>(clear_values.size()),
				clear_values.data(),
			};

			gui->NewFrame(instance.get(), true);
			imguiDrawcalls();

			// Update this too, as swapchain recreation can void it
			renderpass_begin.renderPass = renderPass->vkHandle();

			static VkCommandBufferInheritanceInfo inherit_info = vk_command_buffer_inheritance_info_base;
			inherit_info.renderPass = renderPass->vkHandle();
			inherit_info.subpass = 0;

			for (uint32_t i = 0; i < graphicsPool->size(); ++i) {

				if (!secondaryBuffers[i].empty()) {
					secondaryBuffers[i].clear();
				}

				

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

				VkCommandBuffer& skybox_buffer = secondaryPool->GetCmdBuffer(i * (swapchain->ImageCount + 1));
				// 0 1 2 3 || 4 5 6 7 || 8 9 10 11
				// 
				VkCommandBuffer& star_buffer = secondaryPool->GetCmdBuffer(1 + (i * (swapchain->ImageCount + 1)));
				VkCommandBuffer& gui_buffer = secondaryPool->GetCmdBuffer(2 + (i * (swapchain->ImageCount + 1)));
				VkCommandBuffer& corona_buffer = secondaryPool->GetCmdBuffer(3 + (i * (swapchain->ImageCount + 1)));

				renderGUI(gui_buffer, begin_info, i);
				secondaryBuffers[i].push_back(gui_buffer);

				if (renderSkybox) {
					vkBeginCommandBuffer(skybox_buffer, &begin_info);
					vkCmdSetViewport(skybox_buffer, 0, 1, &viewport);
					vkCmdSetScissor(skybox_buffer, 0, 1, &scissor);
					skybox->UpdateUBO(instance->GetViewMatrix());
					skybox->RecordCommands(skybox_buffer);
					vkEndCommandBuffer(skybox_buffer);
					secondaryBuffers[i].push_back(skybox_buffer);
				}

				if (renderStar) {
					star->SetViewport(viewport);
					star->SetScissor(scissor);
					star->UpdateUBOs(instance->GetViewMatrix(), instance->GetCamPos());
					star->RecordCommands(star_buffer, begin_info);
					secondaryBuffers[i].push_back(star_buffer);
				}

				if (renderCorona) {
					corona->SetViewport(viewport);
					corona->SetScissor(scissor);
					corona->RecordCommands(corona_buffer, begin_info, instance->GetViewMatrix());
					secondaryBuffers[i].push_back(corona_buffer);
					auto param = corona->CoronaParameters();
				}

				vkCmdExecuteCommands(graphicsPool->GetCmdBuffer(i), static_cast<uint32_t>(secondaryBuffers[i].size()), secondaryBuffers[i].data());

				vkCmdEndRenderPass(graphicsPool->GetCmdBuffer(i));

				err = vkEndCommandBuffer(graphicsPool->GetCmdBuffer(i));
				VkAssert(err);
				assert(secondaryBuffers[i].size() <= 4);
			}
		}

	

	private:

		virtual void endFrame(const size_t& idx) override {
			VkResult result = vkWaitForFences(device->vkHandle(), 1, &presentFences[idx], VK_TRUE, vk_default_fence_timeout);
			VkAssert(result);
			secondaryBuffers[idx].clear();
			secondaryBuffers[idx].shrink_to_fit();
			result = vkResetFences(device->vkHandle(), 1, &presentFences[idx]);
			VkAssert(result);
		}
			
		virtual void imguiDrawcalls() override {

			ImGui::Begin("Parameters");
			const bool tab_change = ImGui::TabLabels(tab_names, sizeof(tab_names) / sizeof(tab_names[0]), tab_active, tab_order);

			switch (static_cast<possibleTabs>(tab_active)) {
			case possibleTabs::STAR_SETTINGS:
				ImGui::PushID("Star Parameters");
				ImGui::DragFloat("Noise Frequency", &star->fsUboData.noiseParams.frequency, 0.001f, 0.001f, 2.0f);
				ImGui::DragInt("Noise Octaves", &noise_octave_buffer, 0.1f);
				star->fsUboData.noiseParams.octaves = static_cast<float>(noise_octave_buffer);
				ImGui::DragFloat("Noise Lacunarity", &star->fsUboData.noiseParams.lacunarity, 0.001f, 1.0f, 2.60f);
				ImGui::DragFloat("Noise Persistence", &star->fsUboData.noiseParams.persistence, 0.001f, 0.01f, 0.9f);
				ImGui::PopID();
				break;
			case possibleTabs::CORONA_SETTINGS:
				ImGui::PushID("Corona parameters");
				ImGui::DragFloat("Temperature", &corona->pushDataFS.parameters.temperature, 100.0f, 5000.0f, 29000.0f);
				ImGui::DragFloat("Frequency", &corona->pushDataFS.parameters.frequency, 0.01f, 0.01f, 30.0f);
				ImGui::DragFloat("Permutation speed", &corona->pushDataFS.parameters.speed, 0.0001f, 0.00001f, 1.0f);
				ImGui::DragFloat("Alpha discard level", &corona->pushDataFS.parameters.alphaDiscardLevel, 0.001f, 0.001f, 0.95f);
				ImGui::DragFloat("Size", &size_buffer, 100.0f, 1000.0f);
				corona->pushDataVS.size = glm::vec4(size_buffer);
				ImGui::PopID();
				break;
			case possibleTabs::TOGGLE_SETTINGS:
				ImGui::PushID("Toggle settings");
				ImGui::Checkbox("Render Skybox", &renderSkybox);
				ImGui::Checkbox("Render Star", &renderStar);
				ImGui::Checkbox("Render Corona", &renderCorona);
				ImGui::PopID();
			}

			ImGui::End();

		}

		bool renderSkybox, renderStar, renderCorona;
		std::shared_ptr<PipelineCache> pipelineCache;
		std::unique_ptr<Star> star;
		std::unique_ptr<Skybox> skybox;
		std::unique_ptr<Corona> corona;
		int noiseOctBuffer = 2;
		std::vector<std::vector<VkCommandBuffer>> secondaryBuffers;

	};

}
#endif // !VULPES_STAR_H
