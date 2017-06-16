#include "stdafx.h"
#include "Skybox.h"
#include "engine\renderer\resource\Texture.h"
#include "engine\renderer\core\LogicalDevice.h"
#include "engine\renderer\resource\Buffer.h"
#include "engine\renderer\resource\ShaderModule.h"
#include "engine\renderer\render\GraphicsPipeline.h"
#include "engine\renderer\core\PhysicalDevice.h"
#include "engine\renderer\render\MSAA.h"

namespace vulpes {
	namespace obj {
		Skybox::Skybox(const Device* _device) : device(_device) {
			std::array<glm::vec3, 8> positions{
				{
					glm::vec3(-1.0f, -1.0f, +1.0f), // Point 0, left lower front UV{0,0}
					glm::vec3(+1.0f, -1.0f, +1.0f), // Point 1, right lower front UV{1,0}
					glm::vec3(+1.0f, +1.0f, +1.0f), // Point 2, right upper front UV{1,1}
					glm::vec3(-1.0f, +1.0f, +1.0f), // Point 3, left upper front UV{0,1}
					glm::vec3(+1.0f, -1.0f, -1.0f), // Point 4, right lower rear
					glm::vec3(-1.0f, -1.0f, -1.0f), // Point 5, left lower rear
					glm::vec3(-1.0f, +1.0f, -1.0f), // Point 6, left upper rear
					glm::vec3(+1.0f, +1.0f, -1.0f), } // Point 7, right upper rear
			};
			// Build mesh (six faces defining the cube)
			auto buildface = [this](const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3) {
				// We'll need four indices and four vertices for the two tris defining a face.
				uint32_t i0, i1, i2, i3;
				vertex_t v0, v1, v2, v3;

				// Set the vertex positions.
				v0.Position = p0;
				v1.Position = p1;
				v2.Position = p2;
				v3.Position = p3;
				// Add the verts to the Mesh's vertex container. Returns index to added vert.
				i0 = add_vertex(std::move(v0));
				i1 = add_vertex(std::move(v1));
				i2 = add_vertex(std::move(v2));
				i3 = add_vertex(std::move(v3));
				// Add the triangles to the mesh, via indices
				add_triangle(i0, i1, i2); // Needs UVs {0,0}{1,0}{0,1}
				add_triangle(i0, i2, i3); // Needs UVs {1,0}{0,1}{1,1}
			};
			// Front
			buildface(positions[0], positions[1], positions[2], positions[3]); // Using Points 0, 1, 2, 3 and Normal 0
																			   // Right
			buildface(positions[1], positions[4], positions[7], positions[2]); // Using Points 1, 4, 7, 2 and Normal 1
																			   // Top
			buildface(positions[3], positions[2], positions[7], positions[6]); // Using Points 3, 2, 7, 6 and Normal 2
																			   // Left
			buildface(positions[5], positions[0], positions[3], positions[6]); // Using Points 5, 0, 3, 6 and Normal 3
																			   // Bottom
			buildface(positions[5], positions[4], positions[1], positions[0]); // Using Points 5, 4, 1, 0 and Normal 4
																			   // Back
			buildface(positions[4], positions[5], positions[6], positions[7]); // Using Points 4, 5, 6, 7 and Normal 5

			auto& phys_device = device->GetPhysicalDevice();
			assert(phys_device.Properties.limits.maxImageDimensionCube >= 4096);
			texture = new TextureCubemap("rsrc/img/skybox/deep_thought_bc7.dds", device);
			texture->SetFormat(VK_FORMAT_BC7_UNORM_BLOCK);
		}

		Skybox::~Skybox() {
			vkDestroyDescriptorPool(device->vkHandle(), descriptorPool, nullptr);
			vkDestroyDescriptorSetLayout(device->vkHandle(), descriptorSetLayout, nullptr);
			vkDestroyPipelineLayout(device->vkHandle(), pipelineLayout, nullptr);
			delete texture;
			delete ubo;
			delete ebo;
			delete vbo;
			delete vert;
			delete frag;
			delete pipeline;
		}

		void Skybox::CreateData(CommandPool * pool, const VkQueue & queue, const glm::mat4& projection) {

			texture->Create(pool, queue);

			vbo = new Buffer(device);
			vbo->CreateBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vertices.size() * sizeof(vertex_t));
			vbo->CopyTo(vertices.data(), pool, queue, vertices.size() * sizeof(vertex_t));

			ebo = new Buffer(device);
			ebo->CreateBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_HEAP_DEVICE_LOCAL_BIT, indices.size() * sizeof(uint32_t));
			ebo->CopyTo(indices.data(), pool, queue, indices.size() * sizeof(uint32_t));

			ubo = new Buffer(device);
			ubo->CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, sizeof(vs_ubo_data));
			uboData.projection = projection;

			vert = new ShaderModule(device, "shaders/skybox/skybox.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
			frag = new ShaderModule(device, "shaders/skybox/skybox.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

			static const VkDescriptorPoolSize descr_pool[2]{ VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}, VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1} };

			VkDescriptorPoolCreateInfo pool_info = vk_descriptor_pool_create_info_base;
			pool_info.maxSets = 1;
			pool_info.poolSizeCount = 2;
			pool_info.pPoolSizes = descr_pool;

			VkResult result = vkCreateDescriptorPool(device->vkHandle(), &pool_info, nullptr, &descriptorPool);
			VkAssert(result);

			static const VkDescriptorSetLayoutBinding layout_bindings[2]{
				VkDescriptorSetLayoutBinding{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
				VkDescriptorSetLayoutBinding{1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}
			};

			VkDescriptorSetLayoutCreateInfo layout_info = vk_descriptor_set_layout_create_info_base;
			layout_info.bindingCount = 2;
			layout_info.pBindings = layout_bindings;

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

			VkDescriptorBufferInfo buffer_info = ubo->GetDescriptor();
			VkDescriptorImageInfo image_info = texture->GetDescriptor();

			const VkWriteDescriptorSet write_descr[2]{
				VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &buffer_info, nullptr},
				VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &image_info, nullptr, nullptr }

			};

			vkUpdateDescriptorSets(device->vkHandle(), 2, write_descr, 0, nullptr);

		}

		void Skybox::CreatePipeline(const VkRenderPass& renderpass, const Swapchain* swapchain, std::shared_ptr<PipelineCache>& cache) {
#ifndef NDEBUG
			auto start = std::chrono::high_resolution_clock::now();
#endif // NDEBUG
			const std::array<VkPipelineShaderStageCreateInfo, 2> shader_infos{ vert->PipelineInfo(), frag->PipelineInfo() };
			pipelineCache = cache;
			GraphicsPipelineInfo pipeline_info;

			pipeline_info.RasterizationInfo.cullMode = VK_CULL_MODE_NONE;

			pipeline_info.DepthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

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

		void Skybox::UpdateUBO(const glm::mat4 & view) {
			uboData.view = glm::mat4(glm::mat3(view));
			ubo->CopyTo(&uboData, sizeof(vs_ubo_data));
		}

		void Skybox::RecordCommands(VkCommandBuffer & cmd) {
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->vkHandle());
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
			static const VkDeviceSize offsets[]{ 0 };
			vkCmdBindVertexBuffers(cmd, 0, 1, &vbo->vkHandle(), offsets);
			vkCmdBindIndexBuffer(cmd, ebo->vkHandle(), 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(cmd, static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);
		}

		uint32_t Skybox::add_vertex(const vertex_t & v) {
			vertices.push_back(v);
			return static_cast<uint32_t>(vertices.size() - 1);
		}

		void Skybox::add_triangle(const uint32_t & i0, const uint32_t & i1, const uint32_t & i2) {
			indices.push_back(i0);
			indices.push_back(i1);
			indices.push_back(i2);
		}

	} // !NAMESPACE_OBJ

} // !NAMESPACE_VULPES