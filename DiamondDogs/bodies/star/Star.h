#pragma once
#ifndef STAR_H
#define STAR_H
#include "engine\objects\mesh\Icosphere.h"
#include "engine\renderer\ForwardDecl.h"
#include "engine/renderer/resource/Texture.h"
#include "engine/renderer/render/GraphicsPipeline.h"

namespace vulpes {

	class Star {

		struct vs_ubo_data {
			glm::mat4 projection, view, model, normTransform;
			glm::vec4 cameraPos;
		};

		struct fs_ubo_data {
			glm::vec4 cameraPos;
			glm::vec4 colorShift;
			uint64_t frame;
		};

	public:
		// Creates a star, randomly selecting all values from within reasonable ranges
		Star(const Device* device, int lod_level, float _radius, unsigned int temp, const glm::mat4& projection, const glm::vec3& position = glm::vec3(0.0f));
		// Creates a star, using supplied values or reasonably shuffled defaults otherwise.
		~Star();
		Star() = default;
		Star& operator=(Star&& other);
		
		void BuildPipeline(const VkRenderPass& renderpass, const Swapchain* swapchain, std::shared_ptr<PipelineCache>& cache);
		void BuildMesh(CommandPool* pool, const VkQueue& queue);
		void RecordCommands(VkCommandBuffer& dest_cmd);

		void UpdateUBOs(const glm::mat4& view, const glm::vec3& camera_position);
		void UpdateUBOfs();
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
		GraphicsPipeline* pipeline;
		std::shared_ptr<PipelineCache> pipelineCache;
		Buffer *vsUBO;
		Texture<gli::texture1d> blackbody;
		VkDescriptorSetLayout descriptorSetLayout;
		VkDescriptorSet descriptorSet;
		VkDescriptorPool descriptorPool;
		VkPipelineLayout pipelineLayout;
		ShaderModule *vert, *frag;

		GraphicsPipelineInfo pipelineInfo;
		VkGraphicsPipelineCreateInfo pipelineCreateInfo;

		vs_ubo_data vsUboData;
		fs_ubo_data fsUboData;

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
