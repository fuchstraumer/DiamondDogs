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

			friend class TerrainNode;

			struct vsUBO {
				glm::mat4 model;
				glm::mat4 view, projection;
			};

			std::unique_ptr<TerrainNode> root;

			NodeSubset activeNodes;

			// should be created with make_shared by this class, and shared_ptrs shared to others.
			// if node[node_to_create_idx] == nullptr, then call create func to create node.
			std::vector<std::shared_ptr<TerrainNode>> nodes;

			// Based on the viewer distance, a node will be subdivided when the number (side_length) * splitFactor 
			// is greater than the viewers distance from the object (i.e, the viewer is approaching a range where increased detail
			// would be visible.
			float splitFactor;

			// take time to update faces that are primitively culled during mesh construction.
			bool updateCulledFaces = false;

			// Quadtree depth at which no more nodes shall be generated.
			size_t maxDetailLevel; 

			float viewerHeight;

			float nextViewerHeight;

			glm::dvec3 localViewerPosition;

			float splitDistance, distanceFactor;

			// Pointer to this is shared among all nodes, they write to model section when rendering.
			std::shared_ptr<Buffer> masterUBO;
			std::shared_ptr<vsUBO> masterUBO_Data;

			// side length of a node at LOD level N, where 0 < N < MAX_LOD_LEVEL
			std::array<double, MAX_LOD_LEVEL> sideLengths;

			TerrainQuadtree(const TerrainQuadtree&) = delete;
			TerrainQuadtree& operator=(const TerrainQuadtree&) = delete;
		public:

			TerrainQuadtree(const float& split_factor, const size_t& max_detail_level, const double& root_side_length, const glm::vec3& root_tile_position);

			glm::dvec3 GetCameraPos() const;

			float GetCameraDistance(const util::AABB& bounds) const;

			// Distance at which we will subdivide.
			float GetSubDivideDistance() const;

			void UpdateActiveNodes(NodeSubset* active_nodes);

			void Render();

		};

	}

}

#endif // !VULPES_TERRAIN_QUADTREE_H
