#pragma once
#ifndef VULPES_TERRAIN_NODE_SUBSET_H
#define VULPES_TERRAIN_NODE_SUBSET_H

#include "stdafx.h"
#include "engine\renderer\ForwardDecl.h"
#include "engine\util\AABB.h"
#include "TerrainNode.h"

namespace vulpes {

	namespace terrain {

		struct NodeSubset {
			std::forward_list<TerrainNode*> nodes;
			glm::vec3 viewerPosition;
			static float viewDistanceRatio;
			static float maxDrawDistance;
			static const bool sortNodesByDistance = false;
			static std::array<float, MAX_LOD_LEVEL> visibilityRanges;
			static std::array<float, MAX_LOD_LEVEL> morphEndPts;
			static std::array<float, MAX_LOD_LEVEL> morphBeginPts;
			size_t maxActiveLOD;
			size_t minActiveLOD;

		public:

			NodeSubset(const std::vector<TerrainNode*> input_nodes, const glm::vec3& viewer_pos, const float& draw_dist, const std::array<glm::vec4, 6>& view_frustum, const float& lod_distance_ratio, float morphStartRatio = 0.66f);

			void BuildCommandBuffers();
		};

	}

}

#endif // !VULPES_TERRAIN_NODE_SUBS_ET_H
