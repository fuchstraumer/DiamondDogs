#pragma once
#ifndef STAR_H
#define STAR_H
#include "engine\objects\mesh\Icosphere.hpp"
#include "VulpesRender/include/ForwardDecl.hpp"
#include "VulpesRender/include/resource/Texture.hpp"
#include "VulpesRender/include/render/GraphicsPipeline.hpp"


	class Star {

		struct vs_ubo_data {
			glm::mat4 projection, view, model;
			glm::vec4 cameraPos;
		};

		struct noiseCfg {
			float frequency = 0.02f;
			float octaves = 2.0f;
			float lacunarity = 1.8f;
			float persistence = 0.60f;
		};

		struct fs_ubo_data {
			glm::vec4 cameraPos;
			glm::vec4 colorShift;
			noiseCfg noiseParams;
			uint64_t frame;
		};

	public:

		Star(const vulpes::Device* device, int lod_level, float _radius, unsigned int temp, const glm::mat4& projection, const glm::vec3& position = glm::vec3(0.0f));
		~Star();

		void BuildPipeline(const VkRenderPass& renderpass);
		void BuildMesh(vulpes::TransferPool* transfer_pool);
		void RecordCommands(VkCommandBuffer& dest_cmd, const VkCommandBufferBeginInfo& begin_info);

		void SetViewport(const VkViewport& viewport);
		void SetScissor(const VkRect2D& scissor);

		void UpdateUBOs(const glm::mat4& view, const glm::vec3& camera_position);

		fs_ubo_data fsUboData;

	private:

		void setupDescriptors();

		void createDescriptorPool();
		void createDescriptorSetLayout();
		void createPipelineLayout();
		void allocateDescriptors();
		void setupBuffers(const glm::mat4& projection);
		void updateDescriptors();
		void createShaders();

		void setupPipelineInfo();
		void setupPipelineCreateInfo(const VkRenderPass& renderpass);

		const vulpes::Device* device;
		std::unique_ptr<vulpes::GraphicsPipeline> pipeline;
		std::unique_ptr<vulpes::PipelineCache> pipelineCache;
		std::unique_ptr<vulpes::ShaderModule> vert, frag;
		std::unique_ptr<vulpes::Buffer> vsUBO;

		VkDescriptorSetLayout descriptorSetLayout;
		VkDescriptorSet descriptorSet;
		VkDescriptorPool descriptorPool;
		VkPipelineLayout pipelineLayout;
		vulpes::GraphicsPipelineInfo pipelineInfo;
		VkGraphicsPipelineCreateInfo pipelineCreateInfo;
		VkViewport viewport;
		VkRect2D scissor;

		vs_ubo_data vsUboData;
		unsigned int temperature;
		vulpes::mesh::Icosphere mesh0;
		uint64_t frame;


	};



#endif // !STAR_H
