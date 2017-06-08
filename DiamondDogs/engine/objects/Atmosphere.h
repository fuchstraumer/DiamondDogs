#pragma once
#ifndef VULPES_ATMOSPHERE_OBJECT_H
#define VULPES_ATMOSPHERE_OBJECT_H

#include "stdafx.h"
#include "mesh\Icosphere.h"
#include "engine\renderer\ForwardDecl.h"
namespace vulpes {	
	class Atmosphere {

		struct atmo_ubo_data {
			glm::vec4 cameraPos;
			glm::vec4 lightDir;
			glm::vec4 invWavelength;
			glm::vec4 lightPos;
			glm::vec4 cHoRiR; // (cameraHeight, outerRadius, innerRadius)
		};

		struct vs_ubo_data {
			glm::mat4 model;
			glm::mat4 view;
			glm::mat4 projection;
		};
	public:

		Atmosphere(const Device* device, const float& radius, const glm::mat4& projection, const glm::vec3& position = glm::vec3(0.0f));

		~Atmosphere();

		void BuildPipeline(const VkRenderPass& renderpass, const Swapchain* swapchain, std::shared_ptr<PipelineCache>& cache);
		void BuildMesh(CommandPool* pool, const VkQueue& queue);
		void RecordCommands(VkCommandBuffer& dest_cmd);

		void UpdateUBOs(const glm::mat4& view, const glm::vec3& camera_position);

	protected:
		const Device* device;
		GraphicsPipeline* pipeline;
		std::shared_ptr<PipelineCache> pipelineCache;
		Buffer *ubo, *atmoUBO;
		vs_ubo_data uboData;
		atmo_ubo_data atmoUboData;
		VkDescriptorSetLayout descriptorSetLayout;
		VkDescriptorSet descriptorSet;
		VkDescriptorPool descriptorPool;
		VkPipelineLayout pipelineLayout;
		ShaderModule *vert, *frag;
		mesh::Icosphere mesh;
		std::array<VkSpecializationMapEntry, 7> specializationMap;
	};

}

#endif // !VULPES_ATMOSPHERE_OBJECT_H
