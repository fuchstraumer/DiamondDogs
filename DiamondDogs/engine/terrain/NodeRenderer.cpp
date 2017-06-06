#include "stdafx.h"
#include "NodeRenderer.h"
#include "TerrainNode.h"
#include <imgui\imgui.h>
#include "common\CommonDef.h"
#include "engine\renderer\core\LogicalDevice.h"
#include "engine\renderer\resource\ShaderModule.h"
#include "engine\renderer\render\GraphicsPipeline.h"
#include "engine\renderer\resource\Buffer.h"
#include "engine\renderer\command\CommandPool.h"
#include "engine\renderer\command\TransferPool.h"
#include "engine\renderer\resource\Texture.h"

static const std::array<glm::vec4, 20> LOD_COLOR_ARRAY = {
	glm::vec4(1.0f, 0.0f, 0.0f, 1.0f),
	glm::vec4(0.9f, 0.1f, 0.0f, 1.0f),
	glm::vec4(0.8f, 0.2f, 0.0f, 1.0f),
	glm::vec4(0.7f, 0.3f, 0.0f, 1.0f),
	glm::vec4(0.6f, 0.4f, 0.0f, 1.0f),
	glm::vec4(0.5f, 0.5f, 0.0f, 1.0f),
	glm::vec4(0.4f, 0.6f, 0.0f, 1.0f),
	glm::vec4(0.3f, 0.7f, 0.0f, 1.0f),
	glm::vec4(0.2f, 0.8f, 0.1f, 1.0f),
	glm::vec4(0.1f, 0.9f, 0.2f, 1.0f),
	glm::vec4(0.0f, 0.8f, 0.2f, 1.0f),
	glm::vec4(0.0f, 0.7f, 0.3f, 1.0f),
	glm::vec4(0.0f, 0.6f, 0.4f, 1.0f),
	glm::vec4(0.0f, 0.5f, 0.5f, 1.0f),
	glm::vec4(0.0f, 0.4f, 0.6f, 1.0f),
	glm::vec4(0.0f, 0.3f, 0.7f, 1.0f),
	glm::vec4(0.0f, 0.2f, 0.8f, 1.0f),
	glm::vec4(0.0f, 0.1f, 0.9f, 1.0f),
	glm::vec4(0.0f, 0.0f, 1.0f, 1.0f),
};

bool vulpes::terrain::NodeRenderer::DrawAABBs = false;
float vulpes::terrain::NodeRenderer::MaxRenderDistance = 100000.0f;

vulpes::terrain::NodeRenderer::NodeRenderer(const Device * parent_dvc) : device(parent_dvc) {

	static const std::array<VkDescriptorPoolSize, 1> pools{
		VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
	};

	VkDescriptorPoolCreateInfo pool_info = vk_descriptor_pool_create_info_base;
	pool_info.maxSets = 1;
	pool_info.poolSizeCount = pools.size();
	pool_info.pPoolSizes = pools.data();

	VkResult result = vkCreateDescriptorPool(device->vkHandle(), &pool_info, nullptr, &descriptorPool);
	VkAssert(result);

	static std::array<VkDescriptorSetLayoutBinding, 2> bindings{
		VkDescriptorSetLayoutBinding{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr }
	};

	VkDescriptorSetLayoutCreateInfo layout_info = vk_descriptor_set_layout_create_info_base;
	layout_info.bindingCount = 1;
	layout_info.pBindings = bindings.data();

	result = vkCreateDescriptorSetLayout(device->vkHandle(), &layout_info, nullptr, &descriptorSetLayout);
	VkAssert(result);

	VkPipelineLayoutCreateInfo pipeline_info = vk_pipeline_layout_create_info_base;
	pipeline_info.setLayoutCount = 1;
	pipeline_info.pSetLayouts = &descriptorSetLayout;
	VkPushConstantRange ranges[2]{ VkPushConstantRange{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(vsUBO)}, VkPushConstantRange{VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(vsUBO), sizeof(glm::vec4)} };
	pipeline_info.pushConstantRangeCount = 2;
	pipeline_info.pPushConstantRanges = ranges;

	result = vkCreatePipelineLayout(device->vkHandle(), &pipeline_info, nullptr, &pipelineLayout);
	VkAssert(result);

	VkDescriptorSetAllocateInfo alloc_info = vk_descriptor_set_alloc_info_base;
	alloc_info.descriptorPool = descriptorPool;
	alloc_info.descriptorSetCount = 1;
	alloc_info.pSetLayouts = &descriptorSetLayout;

	result = vkAllocateDescriptorSets(device->vkHandle(), &alloc_info, &descriptorSet);
	VkAssert(result);

	vert = new ShaderModule(device, "shaders/terrain/terrain.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
	frag = new ShaderModule(device, "shaders/terrain/terrain.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
}

vulpes::terrain::NodeRenderer::~NodeRenderer() {
	vkDestroyDescriptorSetLayout(device->vkHandle(), descriptorSetLayout, nullptr);
	vkDestroyDescriptorPool(device->vkHandle(), descriptorPool, nullptr);
	vkDestroyPipelineLayout(device->vkHandle(), pipelineLayout, nullptr);
	delete pipeline;
	delete frag;
	delete vert;
}

void vulpes::terrain::NodeRenderer::CreatePipeline(const VkRenderPass & renderpass, const Swapchain * swapchain, std::shared_ptr<PipelineCache>& cache, const glm::mat4& projection) {

	CreateUBO(projection);

	const std::array<VkPipelineShaderStageCreateInfo, 2> shader_infos{ vert->PipelineInfo(), frag->PipelineInfo() };
	VkPipelineVertexInputStateCreateInfo vert_info = Vertices::PipelineInfo();

	GraphicsPipelineInfo pipeline_info;

	pipeline_info.RasterizationInfo.cullMode = VK_CULL_MODE_NONE;
	pipeline_info.RasterizationInfo.polygonMode = VK_POLYGON_MODE_LINE;

	// Set this through dynamic state so we can do it when rendering.
	pipeline_info.DynamicStateInfo.dynamicStateCount = 2;
	static const VkDynamicState states[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	pipeline_info.DynamicStateInfo.pDynamicStates = states;

	VkGraphicsPipelineCreateInfo pipeline_create_info = vk_graphics_pipeline_create_info_base;

	pipeline_create_info.flags = 0;

	pipeline_create_info.stageCount = 2;
	pipeline_create_info.pStages = shader_infos.data();
	pipeline_create_info.pVertexInputState = &vert_info;

	auto descr = Vertices::BindDescr();
	auto attr = Vertices::AttrDescr();

	vert_info.pVertexBindingDescriptions = descr.data();
	vert_info.pVertexAttributeDescriptions = attr.data();

	pipeline_create_info.pInputAssemblyState = &pipeline_info.AssemblyInfo;
	pipeline_create_info.pTessellationState = nullptr;
	pipeline_create_info.pViewportState = &pipeline_info.ViewportInfo;
	pipeline_create_info.pRasterizationState = &pipeline_info.RasterizationInfo;
	pipeline_create_info.pMultisampleState = &pipeline_info.MultisampleInfo;
	pipeline_create_info.pDepthStencilState = &pipeline_info.DepthStencilInfo;
	pipeline_create_info.pColorBlendState = &pipeline_info.ColorBlendInfo;
	pipeline_create_info.pDynamicState = &pipeline_info.DynamicStateInfo;
	pipeline_create_info.layout = pipelineLayout;
	pipeline_create_info.renderPass = renderpass;
	pipeline_create_info.subpass = 0;
	pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;
	pipeline_create_info.basePipelineIndex = -1;

	pipeline = new GraphicsPipeline(device, swapchain);
	pipeline->Init(pipeline_create_info, cache->vkHandle());
}

void vulpes::terrain::NodeRenderer::CreateUBO(const glm::mat4 & projection) {
	uboData.projection = projection;
	//VkSamplerCreateInfo sampler_info = vk_sampler_create_info_base;
	//heightmap->CreateSampler(sampler_info);
	//auto descr = heightmap->GetDescriptor();
	//const std::array<VkWriteDescriptorSet, 1> writes{
	//	VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &descr, nullptr, nullptr },
	//};

	// vkUpdateDescriptorSets(device->vkHandle(), 1, writes.data(), 0, nullptr);

}

void vulpes::terrain::NodeRenderer::AddNode(TerrainNode * node, bool ready){
	/*if (ready) {
		auto inserted = readyNodes.insert(node);
	}
	else {
		transferNodes.push_front(node);
	}*/
}



void vulpes::terrain::NodeRenderer::Render(VkCommandBuffer& graphics_cmd, VkCommandBufferBeginInfo& begin_info, const glm::mat4 & view, const VkViewport& viewport, const VkRect2D& scissor) {
	uboData.view = view;
	VkResult result = vkBeginCommandBuffer(graphics_cmd, &begin_info);
	VkAssert(result);
	if (device->MarkersEnabled) {
		device->vkCmdBeginDebugMarkerRegion(graphics_cmd, "Draw Terrain", glm::vec4(0.0f, 0.9f, 0.1f, 1.0f));
	}

	if (!readyNodes.empty()) {

		// Record commands for all nodes now
		vkCmdSetViewport(graphics_cmd, 0, 1, &viewport);
		vkCmdSetScissor(graphics_cmd, 0, 1, &scissor);
		vkCmdBindPipeline(graphics_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->vkHandle());
		vkCmdBindDescriptorSets(graphics_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

		for (auto iter = readyNodes.begin(); iter != readyNodes.end(); ++iter) {
			// Push constant block contains our 3 matrices, update that now.
			Mesh* curr = (*iter).second;
			uboData.model = curr->get_model_matrix();
			vkCmdPushConstants(graphics_cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(vsUBO), &uboData);
			vkCmdPushConstants(graphics_cmd, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(vsUBO), sizeof(glm::vec4), &LOD_COLOR_ARRAY[(*iter).first->Depth()]);
			// Generates draw commands
			curr->render(graphics_cmd);

		}
	}

	if (device->MarkersEnabled) {
		device->vkCmdEndDebugMarkerRegion(graphics_cmd);
	}

	result = vkEndCommandBuffer(graphics_cmd);
	VkAssert(result);

	ImGui::Begin("Debug");
	int num_nodes = readyNodes.size();
	ImGui::InputInt("Number of Nodes", &num_nodes);
	ImGui::Checkbox("Render AABBs", &DrawAABBs);
	ImGui::End();

}

