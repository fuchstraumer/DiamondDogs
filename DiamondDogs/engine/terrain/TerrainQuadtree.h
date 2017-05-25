#pragma once
#ifndef VULPES_TERRAIN_QUADTREE_H
#define VULPES_TERRAIN_QUADTREE_H
#include "stdafx.h"
#include "TerrainNode.h"
/*
	
	Parent quadtree class that spawns/manages quadtree nodes

*/

namespace vulpes {

	namespace terrain {

		class TerrainQuadtree {

			std::unique_ptr<TerrainNode> Root;

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

			TerrainQuadtree(const TerrainQuadtree&) = delete;
			TerrainQuadtree& operator=(const TerrainQuadtree&) = delete;
		public:

			TerrainQuadtree(const float& split_factor, const size_t& max_detail_level);

			glm::dvec3 GetCameraPos() const;

			float GetCameraDistance(const util::AABB& bounds) const;

			// Distance at which we will subdivide.
			float GetSubDivideDistance() const;



		};

	}

}

#endif // !VULPES_TERRAIN_QUADTREE_H
