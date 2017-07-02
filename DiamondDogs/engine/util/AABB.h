#pragma once
#ifndef VULPES_UTIL_AABB_H
#define VULPES_UTIL_AABB_H

#include "stdafx.h"
#include "engine\objects\mesh\Mesh.h"
#include "view_frustum.h"
#include "engine\renderer\render\GraphicsPipeline.h"
#include "engine\renderer\resource\ShaderModule.h"
#include "engine\renderer\resource\Buffer.h"

namespace vulpes {

	namespace util {

		struct AABB {

			glm::vec3 Min, Max;
			
			glm::vec3 Extents() const;

			glm::vec3 Center() const;

			void CreateMesh();

			void UpdateMinMax(const double& y_min, const double& y_max);

			void Render(VkCommandBuffer & cmd);

			static void SetupRenderData(const Device* dvc, const VkRenderPass& renderpass, const Swapchain* swapchain, const glm::mat4& projection);
			static void RenderAABBs(const glm::mat4& view, VkCommandBuffer& cmd, const VkViewport& viewport, const VkRect2D& scissor);
			static void CleanupVkResources();
			static VkPipelineLayout pipelineLayout;

			static const Device* device;
			static std::unique_ptr<PipelineCache> cache;
			static std::unique_ptr<ShaderModule> vert, frag;
			static std::unique_ptr<GraphicsPipeline> pipeline;
			mesh::Mesh<> mesh;
			
			struct ubo_data {
				glm::mat4 model;
				glm::mat4 view;
				glm::mat4 projection;
			};

			static ubo_data uboData;

			static const VkAllocationCallbacks* allocators;
			static std::unordered_multimap<glm::ivec3, AABB*> aabbPool;
		};

		

	}

}

#endif // !VULPES_UTIL_AABB_H
