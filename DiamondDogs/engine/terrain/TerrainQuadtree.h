#pragma once
#ifndef VULPES_TERRAIN_QUADTREE_H
#define VULPES_TERRAIN_QUADTREE_H
#include "stdafx.h"
#include <forward_list>
#include "TerrainNode.h"
#include "NodeRenderer.h"
#include "engine\renderer\ForwardDecl.h"
#include "engine\util\view_frustum.h"
/*
	
	Parent quadtree class that spawns/manages quadtree nodes

*/

namespace vulpes {

	namespace terrain {

		/*
			
			Subset of nodes that represent the currently observable terrain data.
			This subset is used for rendering.
		*/

		using nodeSubset = std::unordered_set<std::shared_ptr<TerrainNode>>;
		// stores nodes by grid coordinates
		using nodeCache = std::unordered_map<glm::ivec3, std::shared_ptr<TerrainNode>>;

		class TerrainQuadtree {

			TerrainNode* root;

			NodeRenderer nodeRenderer;
			nodeSubset activeNodes;
			static size_t MaxLOD;
			static double SwitchRatio;
			// Will use this to keep recent nodes around, and also cache by locality using grid coords
			// Should consider "age" member of a node, and deleting nodes in cache at certain age.
			nodeCache cachedNodes;
			// take time to update faces that are primitively culled during mesh construction.
			bool updateCulledFaces = false;

			TerrainQuadtree(const TerrainQuadtree&) = delete;
			TerrainQuadtree& operator=(const TerrainQuadtree&) = delete;

			void pruneNode(const std::shared_ptr<TerrainNode>& node);
			void eraseChildren(const std::shared_ptr<TerrainNode>& node);

		public:

			TerrainQuadtree(const Device* device, const float& split_factor, const size_t& max_detail_level, const double& root_side_length, const glm::vec3& root_tile_position);

			void SetupNodePipeline(const VkRenderPass& renderpass, const Swapchain* swapchain, std::shared_ptr<PipelineCache>& cache, const glm::mat4& projection);

			void UpdateQuadtree(const glm::vec3 & camera_position, const glm::mat4& view);

			void RenderNodes(VkCommandBuffer& graphics_cmd, VkCommandBufferBeginInfo& begin_info, const glm::mat4& view, const VkViewport& viewport, const VkRect2D& rect);

		};

	}

}

#endif // !VULPES_TERRAIN_QUADTREE_H
