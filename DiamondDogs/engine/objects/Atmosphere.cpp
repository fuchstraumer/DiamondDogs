#include "stdafx.h"
#include "Atmosphere.h"
#include "engine\renderer\core\LogicalDevice.h"
#include "engine\renderer\resource\Buffer.h"
#include "engine\renderer\resource\ShaderModule.h"
#include "engine\renderer\render\GraphicsPipeline.h"
#include "engine/renderer/resource/PipelineCache.h"
#include "engine\renderer\render\Swapchain.h"

static const glm::vec4 light_color = glm::vec4(0.98f, 0.95f, 0.93f, 1.0f);

vulpes::Atmosphere::Atmosphere(const Device * _device, const float & radius, const glm::mat4 & projection, const glm::vec3 & position) : mesh(mesh::Icosphere(6, radius, position)), device(_device), pipeline(nullptr) {

	uboData.projection = projection;

	atmoUboData.cHoRiR.z = radius;
	atmoUboData.cHoRiR.y = radius * 1.18f;
	atmoUboData.lightPos.xyz = glm::vec3(1e5f, 0.0f, -1e5f);
	atmoUboData.invWavelength.xyz = glm::vec3(1.0f / powf(0.650f, 4.0f), 1.0f / powf(0.570f, 4.0f), 1.0f / powf(0.475f, 4.0f));
	atmoUboData.lightDir.xyz = position - atmoUboData.lightPos.xyz;

	// Setup descriptor pool, two ubos
	static const VkDescriptorPoolSize pool_size{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 };

	VkDescriptorPoolCreateInfo pool_info = vk_descriptor_pool_create_info_base;
	pool_info.maxSets = 1;
	pool_info.poolSizeCount = 1;
	pool_info.pPoolSizes = &pool_size;

	VkResult result = vkCreateDescriptorPool(device->vkHandle(), &pool_info, nullptr, &descriptorPool);
	VkAssert(result);

	static const std::array<VkDescriptorSetLayoutBinding, 2> bindings{
		VkDescriptorSetLayoutBinding{ 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr },
		VkDescriptorSetLayoutBinding{ 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
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

	ubo = new Buffer(device);
	ubo->CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, sizeof(vs_ubo_data));
	ubo->Map();

	atmoUBO = new Buffer(device);
	atmoUBO->CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, sizeof(atmo_ubo_data));
	atmoUBO->Map();

	VkDescriptorSetAllocateInfo alloc_info = vk_descriptor_set_alloc_info_base;
	alloc_info.descriptorPool = descriptorPool;
	alloc_info.descriptorSetCount = 1;
	alloc_info.pSetLayouts = &descriptorSetLayout;

	result = vkAllocateDescriptorSets(device->vkHandle(), &alloc_info, &descriptorSet);
	VkAssert(result);

	VkDescriptorBufferInfo ubo_info, atmo_info;
	ubo_info = ubo->GetDescriptor();
	atmo_info = atmoUBO->GetDescriptor();

	const std::array<VkWriteDescriptorSet, 2> writes{
		VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &ubo_info, nullptr },
		VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 1, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &atmo_info, nullptr },
	};

	vkUpdateDescriptorSets(device->vkHandle(), 2, writes.data(), 0, nullptr);

	vert = new ShaderModule(device, "shaders/atmosphere/atmosphere.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
	frag = new ShaderModule(device, "shaders/atmosphere/atmosphere.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
}

vulpes::Atmosphere::~Atmosphere(){
	vkDestroyDescriptorSetLayout(device->vkHandle(), descriptorSetLayout, nullptr);
	vkDestroyDescriptorPool(device->vkHandle(), descriptorPool, nullptr);
	vkDestroyPipelineLayout(device->vkHandle(), pipelineLayout, nullptr);
	delete frag;
	delete vert;
	delete atmoUBO;
	delete ubo;
}

void vulpes::Atmosphere::BuildPipeline(const VkRenderPass & renderpass, const Swapchain * swapchain, std::shared_ptr<PipelineCache>& cache){
#ifndef NDEBUG
	auto start = std::chrono::high_resolution_clock::now();
#endif // NDEBUG

	pipelineCache = cache;

	const std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages{ vert->PipelineInfo(), frag->PipelineInfo() };
	GraphicsPipelineInfo pipeline_info;

	pipeline_info.RasterizationInfo.cullMode = VK_CULL_MODE_NONE;

	// Set this through dynamic state so we can do it when rendering.
	pipeline_info.DynamicStateInfo.dynamicStateCount = 2;
	static const VkDynamicState states[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	pipeline_info.DynamicStateInfo.pDynamicStates = states;

	VkPipelineVertexInputStateCreateInfo vert_info = mesh::Vertices::PipelineInfo();

	VkGraphicsPipelineCreateInfo pipeline_create_info = vk_graphics_pipeline_create_info_base;
	pipeline_create_info.flags = 0;
	pipeline_create_info.stageCount = 2;
	pipeline_create_info.pStages = shader_stages.data();
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
	pipeline->Init(pipeline_create_info, pipelineCache->vkHandle());

#ifndef NDEBUG
	auto end = std::chrono::high_resolution_clock::now();
	auto duration = end - start;
	std::cerr << "Pipeline creation time: " << std::chrono::duration_cast<std::chrono::microseconds>(duration).count() << " microseconds" << std::endl;
#endif // !NDEBUG

}

void vulpes::Atmosphere::BuildMesh(CommandPool * pool, const VkQueue & queue) {
	mesh.create_buffers(device);
	uboData.model = mesh.get_model_matrix();
}

void vulpes::Atmosphere::RecordCommands(VkCommandBuffer & dest_cmd) {
	vkCmdBindPipeline(dest_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->vkHandle());
	vkCmdBindDescriptorSets(dest_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
	mesh.render(dest_cmd);
}

void vulpes::Atmosphere::UpdateUBOs(const glm::mat4 & view, const glm::vec3 & camera_position){
	uboData.view = view;
	ubo->CopyTo(&uboData);
	atmoUboData.cameraPos.xyz = camera_position;
	atmoUboData.cHoRiR.x = glm::length(camera_position);
	atmoUBO->CopyTo(&atmoUboData);
}

