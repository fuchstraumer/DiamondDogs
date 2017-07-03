#pragma once
#ifndef VULPES_TERRAIN_QUADTREE_H
#define VULPES_TERRAIN_QUADTREE_H
#include "stdafx.h"
#include "TerrainNode.h"
#include "NodeRenderer.h"
#include "engine\renderer\ForwardDecl.h"
#include "engine\util\view_frustum.h"


namespace vulpes {

	namespace terrain {

		using nodeCache = std::unordered_map<glm::ivec3, std::shared_ptr<HeightNode>>;

		class TerrainQuadtree {

			TerrainNode* root;

			NodeRenderer nodeRenderer;
			nodeCache cachedHeightData;

			TerrainQuadtree(const TerrainQuadtree&) = delete;
			TerrainQuadtree& operator=(const TerrainQuadtree&) = delete;

		public:

			TerrainQuadtree(const Device* device, const float& split_factor, const size_t& max_detail_level, const double& root_side_length, const glm::vec3& root_tile_position);

			~TerrainQuadtree();

			void SetupNodePipeline(const VkRenderPass& renderpass, const Swapchain* swapchain, std::shared_ptr<PipelineCache>& cache, const glm::mat4& projection);

			void UpdateQuadtree(const glm::vec3 & camera_position, const glm::mat4& view);

			void RenderNodes(VkCommandBuffer& graphics_cmd, VkCommandBufferBeginInfo& begin_info, const glm::mat4& view, const glm::vec3& camera_pos, const VkViewport& viewport, const VkRect2D& rect);

		};

	}

}

#endif // !VULPES_TERRAIN_QUADTREE_H
