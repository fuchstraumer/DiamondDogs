#pragma once
#ifndef SKYBOX_H
#define SKYBOX_H
#include "stdafx.h"
#include "ForwardDecl.hpp"
#include "resource/Texture.hpp"
#include "resource/Buffer.hpp"
#include "resource/ShaderModule.hpp"
#include "render/GraphicsPipeline.hpp"
#include "resource/PipelineLayout.hpp"
#include "resource/DescriptorSet.hpp"

namespace vulpes {

		class Skybox {
			struct vs_ubo_data {
				glm::mat4 view, projection;
			};
			struct vertex_t {
				glm::vec3 Position;
			};
		public:

			Skybox(const Device* _device);

			

			~Skybox();

			void CreateData(TransferPool* transfer_pool, DescriptorPool* descriptor_pool, const glm::mat4& projection);

			void CreatePipeline(const VkRenderPass& renderpass);

			void UpdateUBO(const glm::mat4& view);

			void RecordCommands(VkCommandBuffer& cmd);

			uint32_t AddVertex(const vertex_t& v);

			void AddTriangle(const uint32_t& i0, const uint32_t& i1, const uint32_t& i2);

			// Setup descriptor stuff
		private:

			void createMesh();
			void createResources(TransferPool* transfer_pool);
			void uploadData(TransferPool* transfer_pool);
			void createBuffers();
			void createTexture();
			void createShaders();

			void setupDescriptorSet(DescriptorPool* descriptor_pool);
			void setupPipelineLayout();

			const VkVertexInputBindingDescription bind_descr{ 0, sizeof(vertex_t), VK_VERTEX_INPUT_RATE_VERTEX };
			const VkVertexInputAttributeDescription attr_descr{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 };

			std::vector<vertex_t> vertices;
			std::vector<uint32_t> indices;
			std::unique_ptr<Texture<gli::texture_cube>> texture;
			std::unique_ptr<Buffer> vbo, ebo;

			vs_ubo_data uboData;
			std::unique_ptr<ShaderModule> vert, frag;
			std::unique_ptr<GraphicsPipeline> pipeline;

			GraphicsPipelineInfo pipelineStateInfo;
			VkGraphicsPipelineCreateInfo pipelineCreateInfo;

			const Device* device;

			std::unique_ptr<PipelineCache> pipelineCache;

			std::unique_ptr<DescriptorSet> descriptorSet;
			std::unique_ptr<PipelineLayout> pipelineLayout;

		};



}
#endif // !SKYBOX_H
