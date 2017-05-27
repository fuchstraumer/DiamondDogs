#pragma once
#ifndef VULPES_TERRAIN_QUADTREE_H
#define VULPES_TERRAIN_QUADTREE_H
#include "stdafx.h"
#include <forward_list>
#include "TerrainNode.h"
#include "NodeSubset.h"
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

		

		class TerrainQuadtree {

			std::unique_ptr<TerrainNode> root;

			NodeSubset activeNodes;

			// take time to update faces that are primitively culled during mesh construction.
			bool updateCulledFaces = false;

			TerrainQuadtree(const TerrainQuadtree&) = delete;
			TerrainQuadtree& operator=(const TerrainQuadtree&) = delete;
		public:

			TerrainQuadtree(const Device* device, const float& split_factor, const size_t& max_detail_level, const double& root_side_length, const glm::vec3& root_tile_position);

			void UpdateQuadtree(const glm::dvec3 & camera_position, const glm::mat4& view);

			void BuildCommandBuffers(VkCommandBuffer& cmd);

		};

	}

}

#endif // !VULPES_TERRAIN_QUADTREE_H
