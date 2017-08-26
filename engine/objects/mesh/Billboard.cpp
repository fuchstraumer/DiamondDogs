#include "stdafx.h"
#include "Billboard.h"
#include "core/LogicalDevice.h"

using namespace vulpes;


Billboard::Billboard(const Device* dvc, const float & radius, const glm::vec3 & position) : device(dvc) {
	pushDataVS.center.xyz = position;
	pushDataVS.size = glm::vec4(radius);
}

void Billboard::SetViewport(const VkViewport & _viewport) {
	viewport = _viewport;
}

void Billboard::SetScissor(const VkRect2D & _scissor) {
	scissor = _scissor;
}

Billboard::corona_parameters & Billboard::CoronaParameters() {
	return pushDataFS.parameters;
}

void Billboard::Init(const VkRenderPass & renderpass, const glm::mat4 & projection) {
		
	pushDataVS.projection = projection;
		
	pipelineLayout = std::make_unique<PipelineLayout>(device);
	pipelineLayout->Create({ VkPushConstantRange{ VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushDataVS) }, VkPushConstantRange{ VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(pushDataVS), sizeof(pushDataFS) } });

	vbo = std::make_unique<Buffer>(device);
	vbo->CreateBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 12 * sizeof(float));
	vbo->CopyToMapped(const_cast<float*>(quad_vertices.data()), 12 * sizeof(float), 0);

	createShaders();
	setupPipelineInfo();
	createPipeline(renderpass);

}

void Billboard::RecordCommands(VkCommandBuffer & cmd, const VkCommandBufferBeginInfo & begin_info, const glm::mat4 & view) {

	pushDataVS.view = view;
	pushDataVS.cameraUp = glm::vec4(view[0][1], view[1][1], view[2][1], 0.0f);
	pushDataVS.cameraRight = glm::vec4(view[0][0], view[1][0], view[2][0], 1.0f);

	vkBeginCommandBuffer(cmd, &begin_info);
	{
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->vkHandle());
		vkCmdSetViewport(cmd, 0, 1, &viewport);
		vkCmdSetScissor(cmd, 0, 1, &scissor);
		vkCmdPushConstants(cmd, pipelineLayout->vkHandle(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushDataVS), &pushDataVS);
		vkCmdPushConstants(cmd, pipelineLayout->vkHandle(), VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(pushDataVS), sizeof(pushDataFS), &pushDataFS);
		static const VkDeviceSize offsets[1]{ 0 };
		vkCmdBindVertexBuffers(cmd, 0, 1, &vbo->vkHandle(), offsets);
		vkCmdDraw(cmd, 4, 1, 0, 1);
	}
	vkEndCommandBuffer(cmd);

	if (pushDataFS.frame < std::numeric_limits<uint32_t>::max()) {
		++pushDataFS.frame;
	}
	else {
		pushDataFS.frame = 0;
	}

}

void Billboard::setupPipelineInfo() {

	static const VkDynamicState dynamic_states[2] { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

	pipelineInfo.DynamicStateInfo.dynamicStateCount = 2;
	pipelineInfo.DynamicStateInfo.pDynamicStates = dynamic_states;

	static const VkVertexInputBindingDescription bind_descr[1]{ VkVertexInputBindingDescription{ 0, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_VERTEX } };
	static const VkVertexInputAttributeDescription attr_descr[2]{ VkVertexInputAttributeDescription{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 } };
		
	pipelineInfo.VertexInfo.vertexBindingDescriptionCount = 1;
	pipelineInfo.VertexInfo.pVertexBindingDescriptions = bind_descr;
	pipelineInfo.VertexInfo.vertexAttributeDescriptionCount = 1;
	pipelineInfo.VertexInfo.pVertexAttributeDescriptions = attr_descr;

	pipelineInfo.DepthStencilInfo.depthTestEnable = VK_TRUE;
	pipelineInfo.DepthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

	pipelineInfo.AssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

}

void Billboard::createPipeline(const VkRenderPass & renderpass) {

	pipelineCache = std::make_unique<PipelineCache>(device, static_cast<uint16_t>(typeid(Billboard).hash_code()));

	pipelineCreateInfo = pipelineInfo.GetPipelineCreateInfo();

	const VkPipelineShaderStageCreateInfo shader_stages[2]{ vert->PipelineInfo(), frag->PipelineInfo() };

	pipelineCreateInfo.stageCount = 2;
	pipelineCreateInfo.pStages = shader_stages;

	pipelineCreateInfo.basePipelineIndex = -1;
	pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
	pipelineCreateInfo.subpass = 0;
	pipelineCreateInfo.renderPass = renderpass;
	pipelineCreateInfo.layout = pipelineLayout->vkHandle();

	pipeline = std::make_unique<GraphicsPipeline>(device);
	pipeline->Init(pipelineCreateInfo, pipelineCache->vkHandle());

}


