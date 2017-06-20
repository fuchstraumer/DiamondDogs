#include "stdafx.h"
#include "GraphicsPipeline.h"
#include "engine/renderer/core/LogicalDevice.h"
#include "engine/renderer/render/Swapchain.h"
#include "engine/renderer/resource/ShaderModule.h"
#include "..\resource\PipelineCache.h"

namespace vulpes {
	GraphicsPipeline::GraphicsPipeline(const Device * _parent, const GraphicsPipelineInfo & _info) : parent(_parent), info(_info) {}

	GraphicsPipeline::GraphicsPipeline(const Device * _parent, const Swapchain* swapchain) : parent(_parent), createInfo(vk_graphics_pipeline_create_info_base) {}

	GraphicsPipeline::~GraphicsPipeline(){
		shaders.clear();
		vkDestroyPipeline(parent->vkHandle(), handle, allocators);
	}

	void GraphicsPipeline::AddShaderModule(const char * filename, const VkShaderStageFlagBits & stages, const char * shader_name){
		shaders.push_back(std::move(ShaderModule(parent, filename, stages, shader_name)));
		shaderPipelineInfos.push_back(shaders.back().PipelineInfo());
	}

	void GraphicsPipeline::Init(VkGraphicsPipelineCreateInfo & create_info, const VkPipelineCache& cache){
		createInfo = create_info;
		VkResult result = vkCreateGraphicsPipelines(parent->vkHandle(), cache, 1, &create_info, nullptr, &handle);
		VkAssert(result);
	}

	void GraphicsPipeline::Init() {
		assert(shaders.size() != 0);
		createInfo.stageCount = static_cast<uint32_t>(shaders.size());
		createInfo.pStages = shaderPipelineInfos.data();
		VkResult result = vkCreateGraphicsPipelines(parent->vkHandle(), VK_NULL_HANDLE, 1, &createInfo, nullptr, &handle);
		VkAssert(result);
	}

	void GraphicsPipeline::SetRenderpass(const VkRenderPass & renderpass){
		createInfo.renderPass = renderpass;
	}

	const VkPipeline & GraphicsPipeline::vkHandle() const noexcept {
		return handle;
	}

}
