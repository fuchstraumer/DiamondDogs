#pragma once
#ifndef VULPES_BILLBOARD_H
#define VULPES_BILLBOARD_H

#include "stdafx.h"

#include "resource/Buffer.hpp"
#include "resource/PipelineLayout.hpp"
#include "resource/ShaderModule.hpp"
#include "resource/PipelineCache.hpp"
#include "render/GraphicsPipeline.hpp"


constexpr static std::array<float, 12> quad_vertices = {
	-0.5f,-0.5f, 0.0f, 
	0.5f,-0.5f, 0.0f,
	-0.5f, 0.5f, 0.0f,
	0.5f, 0.5f, 0.0f,
};

class Billboard {

	struct corona_parameters {
		float temperature = 24000.0f;
		float frequency = 1.5f;
		float speed = 0.0001f;
		float alphaDiscardLevel = 0.04f;
	};

public:

	Billboard(const vulpes::Device* dvc, const float& radius, const glm::vec3& position = glm::vec3(0.0f));
	virtual ~Billboard() = default;

	void SetViewport(const VkViewport& viewport);
	void SetScissor(const VkRect2D& scissor);

	corona_parameters& CoronaParameters();

	void Init(const VkRenderPass& renderpass, const glm::mat4& projection);

	void RecordCommands(VkCommandBuffer& cmd, const VkCommandBufferBeginInfo& begin_info, const glm::mat4& view);

	struct billboard_ubo_data_fs {
		corona_parameters parameters;
		uint32_t frame = 0;
	} pushDataFS;

	struct billboard_ubo_data {
		glm::mat4 view;
		glm::mat4 projection;
		glm::vec4 center;
		glm::vec4 size;
		glm::vec4 cameraUp;
		glm::vec4 cameraRight;
	} pushDataVS;

protected:

	// Type of billboard decided by shaders.
	virtual void createShaders() = 0;

	void setupPipelineInfo();
	void createPipeline(const VkRenderPass & renderpass);

	const vulpes::Device* device;
	std::unique_ptr<vulpes::Buffer> vbo;
	std::unique_ptr<vulpes::PipelineLayout> pipelineLayout;
	std::unique_ptr<vulpes::ShaderModule> vert, frag;
	std::unique_ptr<vulpes::PipelineCache> pipelineCache;
	std::unique_ptr<vulpes::GraphicsPipeline> pipeline;
		
	VkViewport viewport;
	VkRect2D scissor;
	vulpes::GraphicsPipelineInfo pipelineInfo;
	VkGraphicsPipelineCreateInfo pipelineCreateInfo;
};



#endif // !VULPES_BILLBOARD_H
