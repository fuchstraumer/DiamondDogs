#include "stdafx.h"
#include "Skybox.hpp"
#include "core/Instance.hpp"
#include "core/LogicalDevice.hpp"
#include "resource/Buffer.hpp"
#include "resource/ShaderModule.hpp"
#include "render/GraphicsPipeline.hpp"
#include "core/PhysicalDevice.hpp"
#include "resource/PipelineCache.hpp"
#include "command/CommandPool.hpp"

namespace vulpes {

	static const std::array<glm::vec3, 8> positions{
			glm::vec3(-1.0f, -1.0f, +1.0f), // Point 0, left lower front UV{0,0}
			glm::vec3(+1.0f, -1.0f, +1.0f), // Point 1, right lower front UV{1,0}
			glm::vec3(+1.0f, +1.0f, +1.0f), // Point 2, right upper front UV{1,1}
			glm::vec3(-1.0f, +1.0f, +1.0f), // Point 3, left upper front UV{0,1}
			glm::vec3(+1.0f, -1.0f, -1.0f), // Point 4, right lower rear
			glm::vec3(-1.0f, -1.0f, -1.0f), // Point 5, left lower rear
			glm::vec3(-1.0f, +1.0f, -1.0f), // Point 6, left upper rear
			glm::vec3(+1.0f, +1.0f, -1.0f), // Point 7, right upper rear
	};


	Skybox::Skybox(const Device* _device) : device(_device), ebo(nullptr), vbo(nullptr), vert(nullptr), frag(nullptr), pipeline(nullptr) {
		createMesh();
	}

	void Skybox::createMesh() {

		auto build_face = [this](const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3) {

			uint32_t i0, i1, i2, i3;
			vertex_t v0, v1, v2, v3;

			v0.Position = p0;
			v1.Position = p1;
			v2.Position = p2;
			v3.Position = p3;

			i0 = AddVertex(std::move(v0));
			i1 = AddVertex(std::move(v1));
			i2 = AddVertex(std::move(v2));
			i3 = AddVertex(std::move(v3));

			AddTriangle(i0, i1, i2);
			AddTriangle(i0, i2, i3);

		};

		build_face(positions[0], positions[1], positions[2], positions[3]);
		build_face(positions[1], positions[4], positions[7], positions[2]);
		build_face(positions[3], positions[2], positions[7], positions[6]);
		build_face(positions[5], positions[0], positions[3], positions[6]);
		build_face(positions[5], positions[4], positions[1], positions[0]);
		build_face(positions[4], positions[5], positions[6], positions[7]);

	}
	
	Skybox::~Skybox() {
		descriptorSet.reset();
		texture.reset();
		vbo.reset();
		ebo.reset();
		vert.reset();
		frag.reset();
		pipelineLayout.reset();
	}

	void Skybox::CreateData(TransferPool* transfer_pool, DescriptorPool* descriptor_pool, const glm::mat4& projection) {

		createResources(transfer_pool);
		setupDescriptorSet(descriptor_pool);
		setupPipelineLayout();
		uboData.projection = projection;

	}

	void Skybox::createResources(TransferPool* transfer_pool) {

		createBuffers();
		createTexture();
		uploadData(transfer_pool);

		createShaders();

	}

	void Skybox::createBuffers() {
		
		vbo = std::make_unique<Buffer>(device);
		ebo = std::make_unique<Buffer>(device);

		vbo->CreateBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, sizeof(vertex_t) * vertices.size());
		ebo->CreateBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, sizeof(uint32_t) * indices.size());

	}

	void Skybox::createTexture() {

		texture = std::make_unique<Texture<gli::texture_cube>>(device);
		texture->CreateFromFile("rsrc/img/skybox/deep_thought_bc7.dds", VK_FORMAT_BC7_UNORM_BLOCK);

	}

	void Skybox::createShaders() {

		vert = std::make_unique<ShaderModule>(device, "rsrc/shaders/skybox/skybox.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		frag = std::make_unique<ShaderModule>(device, "rsrc/shaders/skybox/skybox.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

	}

	void Skybox::uploadData(TransferPool* transfer_pool) {

		auto& cmd = transfer_pool->Begin();
		vbo->CopyTo(vertices.data(), cmd, sizeof(vertex_t) * vertices.size(), 0);
		ebo->CopyTo(indices.data(), cmd, sizeof(uint32_t) * indices.size(), 0);
		texture->TransferToDevice(cmd);
		transfer_pool->End();
		transfer_pool->Submit();

	}

	void Skybox::setupDescriptorSet(DescriptorPool* descriptor_pool) {

		descriptorSet = std::make_unique<DescriptorSet>(device);
		descriptorSet->AddDescriptorBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0);
		descriptorSet->AddDescriptorInfo(texture->GetDescriptor(), 0);
		descriptorSet->Init(descriptor_pool);

	}

	void Skybox::setupPipelineLayout() {

		pipelineLayout = std::make_unique<PipelineLayout>(device);
		pipelineLayout->Create({ descriptorSet->vkLayout() }, { VkPushConstantRange{ VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(vs_ubo_data) } });

	}

	void Skybox::CreatePipeline(const VkRenderPass& renderpass) {

		const std::array<VkPipelineShaderStageCreateInfo, 2> shader_infos{ vert->PipelineInfo(), frag->PipelineInfo() };
		pipelineCache = std::make_unique<PipelineCache>(device, static_cast<uint16_t>(typeid(Skybox).hash_code()));
		GraphicsPipelineInfo pipeline_info;

		pipeline_info.RasterizationInfo.cullMode = VK_CULL_MODE_NONE;
		pipeline_info.DepthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		pipeline_info.MultisampleInfo.rasterizationSamples = vulpes::Instance::VulpesInstanceConfig.MSAA_SampleCount;

		// Set this through dynamic state so we can do it when rendering.
		pipeline_info.DynamicStateInfo.dynamicStateCount = 2;
		static const VkDynamicState states[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		pipeline_info.DynamicStateInfo.pDynamicStates = states;

		pipeline_info.VertexInfo.vertexBindingDescriptionCount = 1;
		pipeline_info.VertexInfo.pVertexBindingDescriptions = &bind_descr;
		pipeline_info.VertexInfo.vertexAttributeDescriptionCount = 1;
		pipeline_info.VertexInfo.pVertexAttributeDescriptions = &attr_descr;

		pipeline_info.MultisampleInfo.rasterizationSamples = Multisampling::SampleCount;

		VkGraphicsPipelineCreateInfo pipeline_create_info = vk_graphics_pipeline_create_info_base;
		pipeline_create_info.flags = 0;
		pipeline_create_info.stageCount = 2;
		pipeline_create_info.pStages = shader_infos.data();
		pipeline_create_info.pInputAssemblyState = &pipeline_info.AssemblyInfo;
		pipeline_create_info.pTessellationState = nullptr;
		pipeline_create_info.pViewportState = &pipeline_info.ViewportInfo;
		pipeline_create_info.pRasterizationState = &pipeline_info.RasterizationInfo;
		pipeline_create_info.pMultisampleState = &pipeline_info.MultisampleInfo;
		pipeline_create_info.pVertexInputState = &pipeline_info.VertexInfo;
		pipeline_create_info.pDepthStencilState = &pipeline_info.DepthStencilInfo;
		pipeline_create_info.pColorBlendState = &pipeline_info.ColorBlendInfo;
		pipeline_create_info.pDynamicState = &pipeline_info.DynamicStateInfo;
		pipeline_create_info.layout = pipelineLayout->vkHandle();
		pipeline_create_info.renderPass = renderpass;
		pipeline_create_info.subpass = 0;
		pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;
		pipeline_create_info.basePipelineIndex = -1;

		pipeline = std::make_unique<GraphicsPipeline>(device);
		pipeline->Init(pipeline_create_info, pipelineCache->vkHandle());

	}

	void Skybox::UpdateUBO(const glm::mat4 & view) {
		uboData.view = glm::mat4(glm::mat3(view));

	}

	void Skybox::RecordCommands(VkCommandBuffer & cmd) {
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->vkHandle());
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout->vkHandle(), 0, 1, &descriptorSet->vkHandle(), 0, nullptr);
		static const VkDeviceSize offsets[]{ 0 };
		vkCmdPushConstants(cmd, pipelineLayout->vkHandle(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(vs_ubo_data), &uboData);
		vkCmdBindVertexBuffers(cmd, 0, 1, &vbo->vkHandle(), offsets);
		vkCmdBindIndexBuffer(cmd, ebo->vkHandle(), 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(cmd, static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);
	}

	uint32_t Skybox::AddVertex(const vertex_t & v) {
		vertices.push_back(v);
		return static_cast<uint32_t>(vertices.size() - 1);
	}

	void Skybox::AddTriangle(const uint32_t & i0, const uint32_t & i1, const uint32_t & i2) {
		indices.insert(indices.cend(), std::initializer_list<uint32_t>{ i0, i1, i2 });
	}


} // !NAMESPACE_VULPES