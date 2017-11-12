#include "stdafx.h"
#include "Star.hpp"
#include "core/Instance.hpp"
#include "core/LogicalDevice.hpp"
#include "resource/Buffer.hpp"
#include "resource/ShaderModule.hpp"
#include "render/GraphicsPipeline.hpp"
#include "render/Swapchain.hpp"
#include "resource/PipelineCache.hpp"
#include "command/TransferPool.hpp"

using namespace vulpes;

// Simple method to get a stars color based on its temperature
inline glm::vec3 getStarColor(unsigned int temperature) {
	return glm::vec3(temperature * (0.0534f / 255.0f) - (43.0f / 255.0f),
		temperature * (0.0628f / 255.0f) - (77.0f / 255.0f),
		temperature * (0.0735f / 255.0f) - (115.0f / 255.0f));
}

Star::Star(const Device* _device, int lod_level, float _radius, unsigned int _temp, const glm::mat4 & projection, const glm::vec3 & position) : mesh0(mesh::Icosphere(lod_level)), device(_device), temperature(_temp) {
		
	mesh0.scale = glm::vec3(_radius);
	mesh0.position = position;
	mesh0.update_model_matrix();
	setupDescriptors();
	setupBuffers(projection);
	updateDescriptors();
		
	createShaders();

}

Star::~Star(){

	vkDestroyDescriptorSetLayout(device->vkHandle(), descriptorSetLayout, nullptr);
	vkDestroyDescriptorPool(device->vkHandle(), descriptorPool, nullptr);
	vkDestroyPipelineLayout(device->vkHandle(), pipelineLayout, nullptr);

}

void Star::BuildPipeline(const VkRenderPass& renderpass) {

	pipelineCache = std::make_unique<PipelineCache>(device, static_cast<uint16_t>(typeid(Star).hash_code()));

	setupPipelineInfo();
	setupPipelineCreateInfo(renderpass);

	const std::array<VkPipelineShaderStageCreateInfo, 2> shader_infos{ vert->PipelineInfo(), frag->PipelineInfo() };
	pipelineCreateInfo.pStages = shader_infos.data();
	VkPipelineVertexInputStateCreateInfo vert_info = mesh::Vertices::PipelineInfo();
	const auto attr = mesh::Vertices::AttrDescr();
	const auto bind = mesh::Vertices::BindDescr();
	vert_info.pVertexAttributeDescriptions = attr.data();
	vert_info.pVertexBindingDescriptions = bind.data();
	pipelineCreateInfo.pVertexInputState = &vert_info;

	pipeline = std::make_unique<GraphicsPipeline>(device);
	pipeline->Init(pipelineCreateInfo, pipelineCache->vkHandle());

}

void Star::BuildMesh(TransferPool* transfer_pool){
		
	mesh0.create_buffers(device);
	vsUboData.model = mesh0.get_model_matrix();
	auto cmd = transfer_pool->Begin();
	mesh0.record_transfer_commands(cmd);
	transfer_pool->End();
	transfer_pool->Submit();

}

void Star::RecordCommands(VkCommandBuffer & dest_cmd, const VkCommandBufferBeginInfo& begin_info) {
	vkBeginCommandBuffer(dest_cmd, &begin_info);
	vkCmdBindPipeline(dest_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->vkHandle());
	vkCmdBindDescriptorSets(dest_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
	vkCmdSetViewport(dest_cmd, 0, 1, &viewport);
	vkCmdSetScissor(dest_cmd, 0, 1, &scissor);
	vkCmdPushConstants(dest_cmd, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(fs_ubo_data), &fsUboData);
	mesh0.render(dest_cmd);
	vkEndCommandBuffer(dest_cmd);

}

void Star::SetViewport(const VkViewport & _viewport) {
	viewport = _viewport;
}

void Star::SetScissor(const VkRect2D & _scissor) {
	scissor = _scissor;
}

void Star::UpdateUBOs(const glm::mat4 & view, const glm::vec3 & camera_position) {
		
	vsUboData.view = view;
	vsUboData.cameraPos = glm::vec4(camera_position.x, camera_position.y, camera_position.z, 0.0f);
	vsUBO->CopyToMapped(&vsUboData, sizeof(vsUboData), 0);

	fsUboData.cameraPos = glm::vec4(camera_position.x, camera_position.y, camera_position.z, 0.0f);
	if (fsUboData.frame < std::numeric_limits<uint64_t>::max()) {
		fsUboData.frame++;
	}
	else {
		fsUboData.frame = 0;
	}


}

void Star::setupDescriptors() {

	// Must proceed in this order.
	createDescriptorPool();
	createDescriptorSetLayout();
	createPipelineLayout();
	allocateDescriptors();

}

void Star::createDescriptorPool() {

	static std::array<VkDescriptorPoolSize, 1> pools{
		VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
	};

	VkDescriptorPoolCreateInfo pool_info = vk_descriptor_pool_create_info_base;
	pool_info.maxSets = 1;
	pool_info.poolSizeCount = static_cast<uint32_t>(pools.size());
	pool_info.pPoolSizes = pools.data();

	VkResult result = vkCreateDescriptorPool(device->vkHandle(), &pool_info, nullptr, &descriptorPool);
	VkAssert(result);

}

void Star::createDescriptorSetLayout() {

	static const std::array<VkDescriptorSetLayoutBinding, 1> bindings{
		VkDescriptorSetLayoutBinding{ 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr },
	};

	VkDescriptorSetLayoutCreateInfo layout_info = vk_descriptor_set_layout_create_info_base;
	layout_info.bindingCount = static_cast<uint32_t>(bindings.size());
	layout_info.pBindings = bindings.data();

	VkResult result = vkCreateDescriptorSetLayout(device->vkHandle(), &layout_info, nullptr, &descriptorSetLayout);
	VkAssert(result);

}

void Star::createPipelineLayout() {

	std::array<VkPushConstantRange, 1> push_range{ VkPushConstantRange{ VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(fs_ubo_data) } };

	VkPipelineLayoutCreateInfo pipeline_info = vk_pipeline_layout_create_info_base;
	pipeline_info.setLayoutCount = 1;
	pipeline_info.pSetLayouts = &descriptorSetLayout;
	pipeline_info.pushConstantRangeCount = static_cast<uint32_t>(push_range.size());
	pipeline_info.pPushConstantRanges = push_range.data();

	VkResult result = vkCreatePipelineLayout(device->vkHandle(), &pipeline_info, nullptr, &pipelineLayout);
	VkAssert(result);

}

void Star::allocateDescriptors() {

	VkDescriptorSetAllocateInfo alloc_info = vk_descriptor_set_alloc_info_base;
	alloc_info.descriptorPool = descriptorPool;
	alloc_info.descriptorSetCount = 1;
	alloc_info.pSetLayouts = &descriptorSetLayout;

	VkResult result = vkAllocateDescriptorSets(device->vkHandle(), &alloc_info, &descriptorSet);
	VkAssert(result);

}

void Star::setupBuffers(const glm::mat4& projection) {

	vsUBO = std::make_unique<Buffer>(device);
	vsUBO->CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, sizeof(vs_ubo_data));
	vsUboData.projection = projection;
	vsUBO->Map();

	fsUboData.colorShift = glm::vec4(0.0f);
	fsUboData.frame = 1;
	fsUboData.cameraPos = glm::vec4(0.0f);

}

void Star::updateDescriptors() {

	VkDescriptorBufferInfo vs_info;
	vs_info = vsUBO->GetDescriptor();

	const std::array<VkWriteDescriptorSet, 1> writes{
		VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &vs_info, nullptr },
	};

	vkUpdateDescriptorSets(device->vkHandle(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

}

void Star::createShaders() {

	vert = std::make_unique<ShaderModule>(device, "rsrc/shaders/star/star.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
	frag = std::make_unique<ShaderModule>(device, "rsrc/shaders/star/star.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

}

void Star::setupPipelineInfo() {

	pipelineInfo.RasterizationInfo.cullMode = VK_CULL_MODE_NONE;
	pipelineInfo.DynamicStateInfo.dynamicStateCount = 2;
	static const VkDynamicState states[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	pipelineInfo.DynamicStateInfo.pDynamicStates = states;
	pipelineInfo.MultisampleInfo.rasterizationSamples = vulpes::Instance::VulpesInstanceConfig.MSAA_SampleCount;;

}

void Star::setupPipelineCreateInfo(const VkRenderPass& renderpass) {

	pipelineCreateInfo = vk_graphics_pipeline_create_info_base;
	pipelineCreateInfo.flags = 0;
	pipelineCreateInfo.stageCount = 2;
	pipelineCreateInfo.pInputAssemblyState = &pipelineInfo.AssemblyInfo;
	pipelineCreateInfo.pTessellationState = nullptr;
	pipelineCreateInfo.pViewportState = &pipelineInfo.ViewportInfo;
	pipelineCreateInfo.pRasterizationState = &pipelineInfo.RasterizationInfo;
	pipelineCreateInfo.pMultisampleState = &pipelineInfo.MultisampleInfo;
	pipelineCreateInfo.pDepthStencilState = &pipelineInfo.DepthStencilInfo;
	pipelineCreateInfo.pColorBlendState = &pipelineInfo.ColorBlendInfo;
	pipelineCreateInfo.pDynamicState = &pipelineInfo.DynamicStateInfo;
	pipelineCreateInfo.layout = pipelineLayout;
	pipelineCreateInfo.renderPass = renderpass;
	pipelineCreateInfo.subpass = 0;
	pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
	pipelineCreateInfo.basePipelineIndex = -1;

}

