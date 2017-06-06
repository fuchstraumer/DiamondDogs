#include "stdafx.h"
#include "AABB.h"
#include "engine\renderer\core\LogicalDevice.h"
#include "engine\objects\mesh.h"

namespace vulpes {

	namespace util {

		const VkAllocationCallbacks* AABB::allocators = nullptr;
		AABB::ubo_data AABB::uboData = AABB::ubo_data();
		std::unique_ptr<ShaderModule> AABB::vert = std::unique_ptr<ShaderModule>(nullptr);
		std::unique_ptr<ShaderModule> AABB::frag = std::unique_ptr<ShaderModule>(nullptr);
		std::unique_ptr<GraphicsPipeline> AABB::pipeline = std::unique_ptr<GraphicsPipeline>(nullptr);
		std::unique_ptr<PipelineCache> AABB::cache = std::unique_ptr<PipelineCache>(nullptr);
		const Device* AABB::device = nullptr;
		VkPipelineLayout AABB::pipelineLayout = VK_NULL_HANDLE;
		std::unordered_multimap<glm::ivec3, AABB*> AABB::aabbPool = std::unordered_multimap<glm::ivec3, AABB*>();

		static const std::array<glm::vec3, 8> aabb_vertices{
			glm::vec3{-1.0f,-1.0f, 1.0f },
			glm::vec3{ 1.0f,-1.0f, 1.0f },
			glm::vec3{ 1.0f, 1.0f, 1.0f },
			glm::vec3{-1.0f, 1.0f, 1.0f },
			glm::vec3{-1.0f,-1.0f,-1.0f },
			glm::vec3{ 1.0f,-1.0f,-1.0f },
			glm::vec3{ 1.0f, 1.0f,-1.0f },
			glm::vec3{-1.0f, 1.0f,-1.0f },
		};

		static constexpr std::array<uint32_t, 36> aabb_indices{
			0,1,2, 2,3,0,
			1,5,6, 6,2,1,
			7,6,5, 5,4,7,
			4,0,3, 3,7,4,
			4,5,1, 1,0,4,
			3,2,6, 6,7,3,
		};

		glm::dvec3 vulpes::util::AABB::Extents() const {
			return (Min - Max);
		}

		glm::dvec3 vulpes::util::AABB::Center() const {
			return (Min + Max) / 2.0;
		}

		void AABB::CreateMesh() {
			mesh = std::move(Mesh(Center(), Extents()));
			int i = 0;
			for (auto& vert : aabb_vertices) {
				mesh.add_vertex(vertex_t{ (vert / 2.0f) + glm::vec3(0.5f, 0.5f, -0.5f) });
				mesh.add_triangle(aabb_indices[i * 3], aabb_indices[i * 3 + 1], aabb_indices[i * 3 + 2]);
				++i;
			}
			mesh.create_buffers(device);
		}

		void AABB::Render(VkCommandBuffer& cmd) {

		}

		void AABB::SetupRenderData(const Device* dvc, const VkRenderPass & renderpass, const Swapchain * swapchain, const glm::mat4 & projection) {
			uboData.projection = projection;
			device = dvc;

			VkPipelineLayoutCreateInfo pipeline_layout_info = vk_pipeline_layout_create_info_base;
			pipeline_layout_info.setLayoutCount = 0;
			pipeline_layout_info.pSetLayouts = nullptr;

			// Use push constants instead of descriptors to update info.
			VkPushConstantRange ranges[2]{ VkPushConstantRange{ VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ubo_data) }, VkPushConstantRange{ VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(ubo_data), sizeof(glm::vec4) } };
			pipeline_layout_info.pushConstantRangeCount = 2;
			pipeline_layout_info.pPushConstantRanges = ranges;
			VkResult result = vkCreatePipelineLayout(device->vkHandle(), &pipeline_layout_info, allocators, &pipelineLayout);
			VkAssert(result);

			vert = std::make_unique<ShaderModule>(device, "shaders/aabb/aabb.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
			frag = std::make_unique<ShaderModule>(device, "shaders/aabb/aabb.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

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

			pipeline = std::make_unique<GraphicsPipeline>(device, swapchain);
			pipeline->Init(pipeline_create_info, cache->vkHandle());
		}

		void AABB::RenderAABBs(const glm::mat4 & view, VkCommandBuffer& cmd, const VkViewport& viewport, const VkRect2D& scissor) {
			uboData.view = view;
			vkCmdSetViewport(cmd, 0, 1, &viewport);
			vkCmdSetScissor(cmd, 0, 1, &scissor);
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->vkHandle());
			//vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 0, nullptr , 0, nullptr);
			size_t aabb_pool_size = aabbPool.size();
			for (auto& aabb : aabbPool) {
				uboData.model = aabb.second->mesh.get_model_matrix();
				vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ubo_data), &uboData);
				aabb.second->mesh.render(cmd);
			}
		}

		void AABB::CleanupVkResources() {
			vkDestroyPipelineLayout(device->vkHandle(), pipelineLayout, allocators);
			cache.reset();
			pipeline.reset();
			vert.reset();
			frag.reset();
		}

	}

}

