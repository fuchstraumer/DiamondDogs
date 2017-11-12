#include "stdafx.h"
#include "NodeRenderer.hpp"
#include "TerrainNode.hpp"
#include "common\CommonDef.h"
#include "core/Instance.hpp"
#include "core/LogicalDevice.hpp"
#include "gui/imguiWrapper.hpp"
#include "resource/ShaderModule.hpp"
#include "render/GraphicsPipeline.hpp"
#include "resource/Buffer.hpp"
#include "command/CommandPool.hpp"
#include "command/TransferPool.hpp"
#include "resource/Texture.hpp"
#include "resource/PipelineCache.hpp"

bool vulpes::terrain::NodeRenderer::DrawAABBs = false;
float vulpes::terrain::NodeRenderer::MaxRenderDistance = 100000.0f;
vulpes::util::TaskPool vulpes::terrain::NodeRenderer::HeightDataTasks = std::move(vulpes::util::TaskPool());

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

vulpes::terrain::NodeRenderer::NodeRenderer(const Device * parent_dvc, TransferPool* transfer_pool) : device(parent_dvc), pipeline(nullptr), transferPool(transfer_pool) {

	auto init_hm = GetNoiseHeightmap(HeightNode::RootSampleGridSize + 5, glm::vec3(0.0f), 1.0f);
	glm::ivec3 grid_pos = glm::ivec3(0, 0, 0);
	std::shared_ptr<HeightNode> root = std::make_shared<HeightNode>(glm::ivec3(0, 0, 0), init_hm);
	
	setupDescriptors();

	setupPipelineLayout();

	allocateDescriptors();

	createShaders();

}

void vulpes::terrain::NodeRenderer::setupDescriptors() {
	static const std::array<VkDescriptorPoolSize, 1> pools{
		VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },
	};

	VkDescriptorPoolCreateInfo pool_info = vk_descriptor_pool_create_info_base;
	pool_info.maxSets = 1;
	pool_info.poolSizeCount = static_cast<uint32_t>(pools.size());
	pool_info.pPoolSizes = pools.data();

	VkResult result = vkCreateDescriptorPool(device->vkHandle(), &pool_info, nullptr, &descriptorPool);
	VkAssert(result);

	static std::array<VkDescriptorSetLayoutBinding, 2> bindings{
		VkDescriptorSetLayoutBinding{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }
	};

	VkDescriptorSetLayoutCreateInfo layout_info = vk_descriptor_set_layout_create_info_base;
	layout_info.bindingCount = 1;
	layout_info.pBindings = bindings.data();

	result = vkCreateDescriptorSetLayout(device->vkHandle(), &layout_info, nullptr, &descriptorSetLayout);
	VkAssert(result);
}

void vulpes::terrain::NodeRenderer::setupPipelineLayout() {
	
	VkPipelineLayoutCreateInfo pipeline_info = vk_pipeline_layout_create_info_base;
	pipeline_info.setLayoutCount = 1;
	pipeline_info.pSetLayouts = &descriptorSetLayout;
	VkPushConstantRange ranges[2]{ VkPushConstantRange{ VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(terrain_push_ubo) }, VkPushConstantRange{ VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(terrain_push_ubo), sizeof(glm::vec4) * 2 } };
	pipeline_info.pushConstantRangeCount = 2;
	pipeline_info.pPushConstantRanges = ranges;

	VkResult result = vkCreatePipelineLayout(device->vkHandle(), &pipeline_info, nullptr, &pipelineLayout);
	VkAssert(result);
}

void vulpes::terrain::NodeRenderer::allocateDescriptors() {

	VkDescriptorSetAllocateInfo alloc_info = vk_descriptor_set_alloc_info_base;
	alloc_info.descriptorPool = descriptorPool;
	alloc_info.descriptorSetCount = 1;
	alloc_info.pSetLayouts = &descriptorSetLayout;

	VkResult result = vkAllocateDescriptorSets(device->vkHandle(), &alloc_info, &descriptorSet);
	VkAssert(result);
}

void vulpes::terrain::NodeRenderer::createShaders() {
	vert = std::make_unique<ShaderModule>(device, "rsrc/shaders/terrain/terrain.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
	frag = std::make_unique<ShaderModule>(device, "rsrc/shaders/terrain/terrain.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
}

vulpes::terrain::NodeRenderer::~NodeRenderer() {
	VkResult result = vkFreeDescriptorSets(device->vkHandle(), descriptorPool, 1, &descriptorSet);
	VkAssert(result);
	vkDestroyDescriptorPool(device->vkHandle(), descriptorPool, nullptr);
	vkDestroyDescriptorSetLayout(device->vkHandle(), descriptorSetLayout, nullptr);
	vkDestroyPipelineLayout(device->vkHandle(), pipelineLayout, nullptr);
}

void vulpes::terrain::NodeRenderer::CreatePipeline(const VkRenderPass & renderpass, const glm::mat4& projection) {

	updateUBO(projection);

	pipelineCache = std::make_unique<PipelineCache>(device, typeid(*this).hash_code());

	const std::array<VkPipelineShaderStageCreateInfo, 2> shader_infos{ vert->PipelineInfo(), frag->PipelineInfo() };
	VkPipelineVertexInputStateCreateInfo vert_info = mesh::Vertices::PipelineInfo();

	GraphicsPipelineInfo pipeline_info;

	pipeline_info.RasterizationInfo.cullMode = VK_CULL_MODE_NONE;
	//pipeline_info.RasterizationInfo.polygonMode = VK_POLYGON_MODE_LINE;
	// Set this through dynamic state so we can do it when rendering.
	pipeline_info.DynamicStateInfo.dynamicStateCount = 2;
	static const VkDynamicState states[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	pipeline_info.DynamicStateInfo.pDynamicStates = states;

	pipeline_info.MultisampleInfo.rasterizationSamples = vulpes::Instance::VulpesInstanceConfig.MSAA_SampleCount;

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

	pipeline = std::make_unique<GraphicsPipeline>(device);
	pipeline->Init(pipeline_create_info, pipelineCache->vkHandle());
}

void vulpes::terrain::NodeRenderer::updateUBO(const glm::mat4 & projection) {
	uboData.projection = projection;
}

void vulpes::terrain::NodeRenderer::AddNode(TerrainNode * node){
	if (node->Status == NodeStatus::NeedsTransfer) {
		transferNodes.push_front(std::shared_ptr<TerrainNode>(node));
	}
	else if (node->Status == NodeStatus::Active) {
		assert(node->mesh.Ready());
		readyNodes.insert(std::shared_ptr<TerrainNode>(node));
	}
	
}

void vulpes::terrain::NodeRenderer::Render(VkCommandBuffer& graphics_cmd, VkCommandBufferBeginInfo& begin_info, const glm::mat4 & view, const glm::vec3& view_pos, const VkViewport& viewport, const VkRect2D& scissor) {
	
	frameViewport = &viewport;
	frameScissorRect = &scissor;
	frameCameraPos = &view_pos;
	uboData.view = view;

	renderNodes(graphics_cmd, begin_info);

	transferNodesToDvc();

}

void vulpes::terrain::NodeRenderer::renderNodes(VkCommandBuffer& cmd_buffer, VkCommandBufferBeginInfo& begin_info) {
	
	VkResult result = vkBeginCommandBuffer(cmd_buffer, &begin_info);
	VkAssert(result);

	if (device->MarkersEnabled) {
		device->vkCmdBeginDebugMarkerRegion(cmd_buffer, "Draw Terrain", glm::vec4(0.0f, 0.9f, 0.1f, 1.0f));
	}

	if (!readyNodes.empty()) {
		vkCmdSetViewport(cmd_buffer, 0, 1, frameViewport);
		vkCmdSetScissor(cmd_buffer, 0, 1, frameScissorRect);
		vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->vkHandle());
		vkCmdBindDescriptorSets(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
		glm::vec4 camera_push = glm::vec4(frameCameraPos->x, frameCameraPos->y, frameCameraPos->z, 1.0f);
		vkCmdPushConstants(cmd_buffer, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(terrain_push_ubo) + sizeof(glm::vec4), sizeof(glm::vec4), glm::value_ptr(camera_push));
		auto iter = readyNodes.begin();
		while (iter != readyNodes.end()) {
			auto curr = *iter;
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
				vkCmdPushConstants(cmd_buffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(terrain_push_ubo), &uboData);
				vkCmdPushConstants(cmd_buffer, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(terrain_push_ubo), sizeof(glm::vec4), &LOD_COLOR_ARRAY[curr->LOD_Level()]);
				// Generates draw commands
				curr->mesh.render(cmd_buffer);
				++iter;
				break;
			case NodeStatus::NeedsTransfer:
				++iter;
				break;
			case NodeStatus::DataRequested:
				++iter;
				break;
			}
		}
	}

	result = vkEndCommandBuffer(cmd_buffer);
	VkAssert(result);

}

void vulpes::terrain::NodeRenderer::transferNodesToDvc() {

	if (transferNodes.empty()) {
		return;
	}

	transferPool->Begin();

	while (!transferNodes.empty()) {
		auto curr = transferNodes.front();
		transferNodeToDvc(curr);
		transferNodes.pop_front();
	}

	transferPool->End();
	transferPool->Submit();
	Buffer::DestroyStagingResources(device);

}

void vulpes::terrain::NodeRenderer::transferNodeToDvc(std::shared_ptr<TerrainNode> node_to_transfer) {

	// Create mesh data for curr
	node_to_transfer->CreateMesh(device);
	node_to_transfer->mesh.record_transfer_commands(transferPool->CmdBuffer());
	node_to_transfer->Status = NodeStatus::Active;

	// Node is ready for next frame.
	readyNodes.insert(node_to_transfer);
}

void vulpes::terrain::NodeRenderer::updateGUI() {
	ImGui::Begin("Debug");
	size_t num_nodes = readyNodes.size();
	ImGui::InputInt("Number of Nodes", reinterpret_cast<int*>(&num_nodes));
	ImGui::Checkbox("Update LOD", &UpdateLOD);
	ImGui::End();
}

void vulpes::terrain::NodeRenderer::RunTasks() {
	HeightDataTasks.Run();
}

