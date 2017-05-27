#include "stdafx.h"
#include "NodeSubset.h"
#include "TerrainNode.h"
#include "engine\renderer\core\LogicalDevice.h"
#include "engine\renderer\resource\ShaderModule.h"
#include "engine\renderer\render\GraphicsPipeline.h"
#include "engine\renderer\resource\Buffer.h"
#include "engine\renderer\command\CommandPool.h"

vulpes::terrain::NodeSubset::NodeSubset(const Device * parent_dvc) : device(parent_dvc) {

	static std::array<VkDescriptorPoolSize, 1> pools{
		VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
	};
	VkDescriptorPoolCreateInfo pool_info = vk_descriptor_pool_create_info_base;
	pool_info.maxSets = 1;
	pool_info.poolSizeCount = 1;
	pool_info.pPoolSizes = pools.data();

	VkResult result = vkCreateDescriptorPool(device->vkHandle(), &pool_info, nullptr, &descriptorPool);
	VkAssert(result);

	static std::array<VkDescriptorSetLayoutBinding, 2> bindings{
		VkDescriptorSetLayoutBinding{ 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr },
	};

	VkDescriptorSetLayoutCreateInfo layout_info = vk_descriptor_set_layout_create_info_base;
	layout_info.bindingCount = 2;
	layout_info.pBindings = bindings.data();

	result = vkCreateDescriptorSetLayout(device->vkHandle(), &layout_info, nullptr, &descriptorSetLayout);
	VkAssert(result);

	VkPipelineLayoutCreateInfo pipeline_info = vk_pipeline_layout_create_info_base;
	pipeline_info.setLayoutCount = 1;
	pipeline_info.pSetLayouts = &descriptorSetLayout;

	result = vkCreatePipelineLayout(device->vkHandle(), &pipeline_info, nullptr, &pipelineLayout);
	VkAssert(result);

	VkDescriptorSetAllocateInfo alloc_info = vk_descriptor_set_alloc_info_base;
	alloc_info.descriptorPool = descriptorPool;
	alloc_info.descriptorSetCount = 1;
	alloc_info.pSetLayouts = &descriptorSetLayout;

	result = vkAllocateDescriptorSets(device->vkHandle(), &alloc_info, &descriptorSet);
	VkAssert(result);

	vert = new ShaderModule(device, "shaders/aabb/aabb.vert", VK_SHADER_STAGE_VERTEX_BIT);
	frag = new ShaderModule(device, "shaders/aabb/aabb.frag", VK_SHADER_STAGE_FRAGMENT_BIT);

}

void vulpes::terrain::NodeSubset::CreatePipeline(const VkRenderPass & renderpass, const Swapchain * swapchain, std::shared_ptr<PipelineCache>& cache) {
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
	pipeline->Init(pipeline_create_info, pipelineCache->vkHandle());
}

void vulpes::terrain::NodeSubset::CreateUBO(const glm::mat4 & projection) {
	ubo = new Buffer(device);
	ubo->CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, sizeof(vsUBO));
	uboData.projection = projection;

	auto vs_info = ubo->GetDescriptor();
	const std::array<VkWriteDescriptorSet, 2> writes{
		VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &vs_info, nullptr },
	};

	vkUpdateDescriptorSets(device->vkHandle(), 1, writes.data(), 0, nullptr);

}

void vulpes::terrain::NodeSubset::AddNode(const TerrainNode * node){
	// Since LOD level is based on distance, we use LOD level to compare
	// nodes and place highest LOD level first, in hopes that it renders first.
	nodes.insert(std::make_pair(node->Status, node));
}

void vulpes::terrain::NodeSubset::BuildCommandBuffers(VkCommandBuffer& cmd) {
	// First, attempt to quickly transfer resources.
	auto transfer_cmd = transferPool->Start();
	for (auto& node : nodes) {
		
	}
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->vkHandle());
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
	for(const auto& node : nodes) {
	
	}
}

void vulpes::terrain::NodeSubset::Update() {
	if (nodes.count(NodeStatus::MeshUnbuilt) != 0) {
		auto transfer_iter = nodes.find(NodeStatus::MeshUnbuilt);
	}
	if (nodes.count(NodeStatus::Active) != 0) {
		auto active_iter = nodes.find(NodeStatus::Active);
	}
}

void vulpes::terrain::NodeSubset::UpdateUBO(const glm::mat4 & view) {
	uboData.view = view;

}
