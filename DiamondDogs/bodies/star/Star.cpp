#include "stdafx.h"
#include "Star.h"
#include "engine\renderer\core\LogicalDevice.h"
#include "engine\renderer\resource\Buffer.h"
#include "engine\renderer\resource\ShaderModule.h"
#include "engine\renderer\render\GraphicsPipeline.h"
#include "engine\renderer\render\Swapchain.h"

namespace vulpes {

	// Simple method to get a stars color based on its temperature
	inline glm::vec3 getStarColor(unsigned int temperature) {
		return glm::vec3(temperature * (0.0534f / 255.0f) - (43.0f / 255.0f),
			temperature * (0.0628f / 255.0f) - (77.0f / 255.0f),
			temperature * (0.0735f / 255.0f) - (115.0f / 255.0f));
	}

	Star::Star(const Device* _device, int lod_level, float _radius, unsigned int _temp, const glm::mat4 & projection, const glm::vec3 & position) : mesh0(mesh::Icosphere(lod_level)), device(_device), radius(_radius), temperature(_temp) {
		static std::array<VkDescriptorPoolSize, 1> pools{
			VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 },
		};

		mesh0.scale = glm::vec3(radius);
		mesh0.position = position;
		VkDescriptorPoolCreateInfo pool_info = vk_descriptor_pool_create_info_base;
		pool_info.maxSets = 1;
		pool_info.poolSizeCount = 1;
		pool_info.pPoolSizes = pools.data();

		VkResult result = vkCreateDescriptorPool(device->vkHandle(), &pool_info, nullptr, &descriptorPool);
		VkAssert(result);

		static std::array<VkDescriptorSetLayoutBinding, 2> bindings{
			VkDescriptorSetLayoutBinding{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr },
			VkDescriptorSetLayoutBinding{1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
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

		vsUBO = new Buffer(device);
		vsUBO->CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, sizeof(vs_ubo_data));
		vsUboData.projection = projection;
		vsUBO->Map();

		fsUBO = new Buffer(device);
		fsUBO->CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, sizeof(fs_ubo_data));
		//fsUboData.temperature = temperature;
		fsUboData.colorShift = glm::vec3(0.0f);
		fsUboData.frame = 1;
		fsUboData.cameraPos = glm::vec3(0.0f);
		fsUBO->Map();

		VkDescriptorSetAllocateInfo alloc_info = vk_descriptor_set_alloc_info_base;
		alloc_info.descriptorPool = descriptorPool;
		alloc_info.descriptorSetCount = 1;
		alloc_info.pSetLayouts = &descriptorSetLayout;

		result = vkAllocateDescriptorSets(device->vkHandle(), &alloc_info, &descriptorSet);
		VkAssert(result);

		VkDescriptorBufferInfo vs_info, fs_info;
		vs_info = vsUBO->GetDescriptor();
		vs_info.range = 32;
		fs_info = fsUBO->GetDescriptor();
		fs_info.range = fsUBO->AllocSize();

		const std::array<VkWriteDescriptorSet, 2> writes{
			VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &vs_info, nullptr},
			VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 1, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &fs_info, nullptr },
		};

		vkUpdateDescriptorSets(device->vkHandle(), 2, writes.data(), 0, nullptr);

		vert = new ShaderModule(device, "shaders/star/close/star_close.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		frag = new ShaderModule(device, "shaders/star/close/star_close.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		
	}

	Star::~Star(){
		vkDestroyDescriptorSetLayout(device->vkHandle(), descriptorSetLayout, nullptr);
		vkDestroyDescriptorPool(device->vkHandle(), descriptorPool, nullptr);
		vkDestroyPipelineLayout(device->vkHandle(), pipelineLayout, nullptr);
		delete frag;
		delete vert;
		delete fsUBO;
		delete vsUBO;
		delete pipeline;
	}

	void Star::BuildPipeline(const VkRenderPass& renderpass, const Swapchain* swapchain, std::shared_ptr<PipelineCache>& _cache) {
#ifndef NDEBUG
		auto start = std::chrono::high_resolution_clock::now();
#endif // NDEBUG

		pipelineCache = _cache;

		const std::array<VkPipelineShaderStageCreateInfo, 2> shader_infos{ vert->PipelineInfo(), frag->PipelineInfo() };
		VkPipelineVertexInputStateCreateInfo vert_info = mesh::Vertices::PipelineInfo();

		GraphicsPipelineInfo pipeline_info;

		pipeline_info.RasterizationInfo.cullMode = VK_CULL_MODE_NONE;

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
		pipeline->Init(pipeline_create_info, pipelineCache->vkHandle());
#ifndef NDEBUG
		auto end = std::chrono::high_resolution_clock::now();
		auto duration = end - start;
		std::cerr << "Pipeline creation time: " << std::chrono::duration_cast<std::chrono::microseconds>(duration).count() << " microseconds" << std::endl;
#endif // !NDEBUG

	}

	void Star::BuildMesh(CommandPool * pool, const VkQueue & queue){
		mesh0.create_vbo(device, pool, queue);
		
		vsUboData.model = mesh0.get_model_matrix();
		vsUboData.normTransform = glm::transpose(glm::inverse(vsUboData.model));
	}

	void Star::RecordCommands(VkCommandBuffer & dest_cmd) {
		vkCmdBindPipeline(dest_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->vkHandle());
		vkCmdBindDescriptorSets(dest_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
		mesh0.render(dest_cmd);
	}

	void Star::UpdateUBOs(const glm::mat4 & view, const glm::vec3 & camera_position) {
		vsUboData.view = view;
		vsUboData.cameraPos = camera_position;
		vsUBO->CopyTo(&vsUboData);
		fsUboData.cameraPos = camera_position;
		if (fsUboData.frame < std::numeric_limits<uint64_t>::max()) {
			fsUboData.frame++;
		}
		else {
			fsUboData.frame = 0;
		}
		fsUBO->CopyTo(&fsUboData);
	}

}