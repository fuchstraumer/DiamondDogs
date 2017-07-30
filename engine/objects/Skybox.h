#pragma once
#ifndef SKYBOX_H
#define SKYBOX_H
#include "stdafx.h"

#include "resource/Texture.h"
#include "resource/Buffer.h"
#include "resource/ShaderModule.h"
#include "render/GraphicsPipeline.h"

namespace vulpes {

	namespace obj {

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

			void CreateData(CommandPool* pool, const VkQueue& queue, const glm::mat4& projection);

			void CreatePipeline(const VkRenderPass& renderpass, std::shared_ptr<PipelineCache>& cache);

			void UpdateUBO(const glm::mat4& view);

			void RecordCommands(VkCommandBuffer& cmd);

			uint32_t add_vertex(const vertex_t& v);

			void add_triangle(const uint32_t& i0, const uint32_t& i1, const uint32_t& i2);
			// Setup descriptor stuff
		private:
			const VkVertexInputBindingDescription bind_descr{ 0, sizeof(vertex_t), VK_VERTEX_INPUT_RATE_VERTEX };
			const VkVertexInputAttributeDescription attr_descr{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 };
			std::vector<vertex_t> vertices;
			std::vector<uint32_t> indices;
			std::unique_ptr<Texture<gli::texture_cube>> texture;
			std::unique_ptr<Buffer> vbo, ebo, ubo;
			vs_ubo_data uboData;
			std::unique_ptr<ShaderModule> vert, frag;
			std::unique_ptr<GraphicsPipeline> pipeline;
			const Device* device;
			std::shared_ptr<PipelineCache> pipelineCache;
			VkDescriptorSetLayout descriptorSetLayout;
			VkDescriptorSet descriptorSet;
			VkDescriptorPool descriptorPool;
			VkPipelineLayout pipelineLayout;
		};

	}

}
#endif // !SKYBOX_H
