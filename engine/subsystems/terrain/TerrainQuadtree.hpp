#pragma once
#ifndef VULPES_TERRAIN_QUADTREE_H
#define VULPES_TERRAIN_QUADTREE_H
#include "stdafx.h"
#include "TerrainNode.hpp"
#include "NodeRenderer.hpp"
#include "ForwardDecl.hpp"
#include "engine\util\view_frustum.hpp"


namespace vulpes {

	namespace terrain {

		using nodeCache = std::unordered_map<glm::ivec3, std::shared_ptr<HeightNode>>;

		class TerrainQuadtree {
			TerrainQuadtree(const TerrainQuadtree&) = delete;
			TerrainQuadtree& operator=(const TerrainQuadtree&) = delete;
		public:

			TerrainQuadtree(const Device* device, TransferPool* transfer_pool, const float& split_factor, const size_t& max_detail_level, const double& root_side_length, const glm::vec3& root_tile_position);

			void SetupNodePipeline(const VkRenderPass& renderpass, const glm::mat4& projection);
			void UpdateQuadtree(const glm::vec3 & camera_position, const glm::mat4& view);
			void RenderNodes(VkCommandBuffer& graphics_cmd, VkCommandBufferBeginInfo& begin_info, const glm::mat4& view, const glm::vec3& camera_pos, const VkViewport& viewport, const VkRect2D& rect);

		private:

			std::unique_ptr<TerrainNode> root;
			NodeRenderer nodeRenderer;
			nodeCache cachedHeightData;
			size_t MaxLOD;

		};

	}

}

#endif // !VULPES_TERRAIN_QUADTREE_H
