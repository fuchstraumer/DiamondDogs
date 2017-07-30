#pragma once
#ifndef STAR_H
#define STAR_H
#include "engine\objects\mesh\Icosphere.h"
#include "VulpesRender/include/ForwardDecl.h"
#include "VulpesRender/include/resource/Texture.h"
#include "VulpesRender/include/render/GraphicsPipeline.h"

namespace vulpes {

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
		// Creates a star, randomly selecting all values from within reasonable ranges
		Star(const Device* device, int lod_level, float _radius, unsigned int temp, const glm::mat4& projection, const glm::vec3& position = glm::vec3(0.0f));
		// Creates a star, using supplied values or reasonably shuffled defaults otherwise.
		~Star();
		Star() = default;
		Star& operator=(Star&& other);
		
		void BuildPipeline(const VkRenderPass& renderpass, std::shared_ptr<PipelineCache>& cache);
		void BuildMesh(TransferPool* transfer_pool);
		void RecordCommands(VkCommandBuffer& dest_cmd);

		void UpdateUBOs(const glm::mat4& view, const glm::vec3& camera_position);
		void UpdateUBOfs();

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

		const Device* device;
		std::unique_ptr<GraphicsPipeline> pipeline;
		std::shared_ptr<PipelineCache> pipelineCache;
		std::unique_ptr<Buffer> vsUBO;
		VkDescriptorSetLayout descriptorSetLayout;
		VkDescriptorSet descriptorSet;
		VkDescriptorPool descriptorPool;
		VkPipelineLayout pipelineLayout;
		std::unique_ptr<ShaderModule> vert, frag;

		GraphicsPipelineInfo pipelineInfo;
		VkGraphicsPipelineCreateInfo pipelineCreateInfo;

		vs_ubo_data vsUboData;
		

		// Temperature selects color, and specifies which texture coordinate to use for all
		// vertices in this object since the base color is uniform
		unsigned int temperature;
		// Radius is useful to know, but will mainly be set in the mesh since we care about it most there
		float radius;
		// Meshes used for up-close rendering.
		mesh::Icosphere mesh0;
		//Icosphere mesh1;
		// Corona object for this star

		// Used to permute the star
		uint64_t frame;
		// Distance we switch LODs

	};

}

#endif // !STAR_H
