#pragma once
#ifndef VULPES_VK_IMGUI_WRAPPER_H
#define VULPES_VK_IMGUI_WRAPPER_H
#include "stdafx.h"
#include <imgui\imgui.h>
#include "imguiShaders.h"
#include "engine\renderer\command\TransferPool.h"
namespace vulpes {

	struct imguiSettings {
		bool displayMeshes = true;
		bool displaySkybox = true;
		std::array<float, 200> frameTimes;
		float frameTimeMin = 9999.0f, frameTimeMax = 0.0f;
	};

	struct imguiWrapper : public NonCopyable {
		
		~imguiWrapper();

		void Init(const Device* dvc, std::shared_ptr<PipelineCache> _cache, const VkRenderPass& renderpass, const Swapchain* swapchain);

		void UploadTextureData(CommandPool* transfer_pool);

		void NewFrame(Instance* instance, bool update_framegraph = true);

		void UpdateBuffers();

		void DrawFrame(VkCommandBuffer& cmd);

		static float mouseWheel;
		std::array<bool, 3> mouseClick;
		size_t frameIdx;
		VkCommandBuffer graphicsCmd;
		const Device* device;
		std::shared_ptr<PipelineCache> cache;
		GraphicsPipeline* pipeline;
		Buffer *vbo, *ebo;
		Texture2D* texture;
		ShaderModule *vert, *frag;

		VkDescriptorPool descriptorPool;
		VkDescriptorSet descriptorSet;
		VkDescriptorSetLayout descriptorSetLayout;
		VkPipelineLayout pipelineLayout;

		VkImage fontImage;
		VkImageView fontView;
		VkSampler fontSampler;
		VkDeviceMemory fontMemory;
		VkDescriptorImageInfo fontInfo;
		VkWriteDescriptorSet fontWriteSet;

		VkBuffer transferBuffer;
		VkDeviceMemory transferMemory;

		imguiSettings settings;

		int imgWidth, imgHeight;

	};

}
#endif // !VULPES_VK_IMGUI_WRAPPER_H
