#include "stdafx.h"
#include "NodeRenderer.h"
#include <imgui\imgui.h>
#include "TerrainNode.h"

#include "common\CommonDef.h"

#include "engine\renderer\core\LogicalDevice.h"
#include "engine\renderer\resource\ShaderModule.h"
#include "engine\renderer\render\GraphicsPipeline.h"
#include "engine\renderer\resource\Buffer.h"
#include "engine\renderer\command\CommandPool.h"
#include "engine\renderer\command\TransferPool.h"
#include "engine\renderer\resource\Texture.h"

bool vulpes::terrain::NodeRenderer::DrawAABBs = false;
float vulpes::terrain::NodeRenderer::MaxRenderDistance = 100000.0f;

// Used to simply color nodes based on LOD level
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

vulpes::terrain::NodeRenderer::NodeRenderer(const Device * parent_dvc) : device(parent_dvc) {

	static const std::array<VkDescriptorPoolSize, 1> pools{
		VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
	};

	VkDescriptorPoolCreateInfo pool_info = vk_descriptor_pool_create_info_base;
	pool_info.maxSets = 1;
	pool_info.poolSizeCount = static_cast<uint32_t>(pools.size());
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

	transferPool = new TransferPool(device);
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
	VkPipelineVertexInputStateCreateInfo vert_info = mesh::Vertices::PipelineInfo();

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

	auto descr = mesh::Vertices::BindDescr();
	auto attr = mesh::Vertices::AttrDescr();

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

}

void vulpes::terrain::NodeRenderer::AddNode(TerrainNode * node, bool ready){
	if (ready) {
		readyNodes.insert(node);
	}
	else {
		transferNodes.push_front(node);
	}
}

void vulpes::terrain::NodeRenderer::RemoveNode(TerrainNode* node) {
	readyNodes.erase(node);
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

		auto iter = readyNodes.begin();
		while (iter != readyNodes.end()) {
			TerrainNode* curr = *iter;
			if (!curr) {
				readyNodes.erase(iter++);
			}
			switch (curr->Status) {
			case NodeStatus::NeedsUnload:
				readyNodes.erase(iter++);
				break;
			case NodeStatus::Subdivided:
				readyNodes.erase(iter++);
				break;
			case NodeStatus::Active:
				// Push constant block contains our 3 matrices, update that now.
				uboData.model = curr->mesh.get_model_matrix();
				vkCmdPushConstants(graphics_cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(vsUBO), &uboData);
				vkCmdPushConstants(graphics_cmd, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(vsUBO), sizeof(glm::vec4), &LOD_COLOR_ARRAY[curr->Depth()]);
				// Generates draw commands
				curr->mesh.render(graphics_cmd);
				++iter;
				break;
			case NodeStatus::NeedsTransfer:
				++iter;
				break;
			default:
				readyNodes.erase(iter++);
				break;
			}
		}
	}
	result = vkEndCommandBuffer(graphics_cmd);
	VkAssert(result);


	bool submit_transfer = false;
	if (!transferNodes.empty()) {
		transferPool->Begin();
		submit_transfer = true;
	}

	auto start = std::chrono::high_resolution_clock::now();
	size_t node_count = 0;
	while (!transferNodes.empty()) {
		TerrainNode* curr = transferNodes.front();
		curr->CreateMesh(device);
		curr->mesh.record_transfer_commands(transferPool->CmdBuffer());
		curr->Status = NodeStatus::Active;
		auto inserted = readyNodes.insert(curr);
		transferNodes.pop_front();
		++node_count;
	}

	if (submit_transfer) {
		auto end = std::chrono::high_resolution_clock::now();
		auto dur = end - start;
		transferPool->End();
		transferPool->Submit();
		Buffer::DestroyStagingResources(device);
	}

	ImGui::Begin("Debug");
	size_t num_nodes = readyNodes.size();
	ImGui::InputInt("Number of Nodes", reinterpret_cast<int*>(&num_nodes));
	ImGui::Checkbox("Render AABBs", &DrawAABBs);
	ImGui::Checkbox("Update LOD", &UpdateLOD);
	ImGui::End();

}

