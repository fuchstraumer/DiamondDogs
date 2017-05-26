#include "stdafx.h"
#include "TerrainQuadtree.h"

vulpes::terrain::TerrainQuadtree::TerrainQuadtree(const float & split_factor, const size_t & max_detail_level, const double& root_side_length, const glm::vec3& root_tile_position) {
	root = std::make_unique<TerrainNode>(nullptr, glm::ivec2(0, 0), root_tile_position, root_side_length);
	sideLengths[0] = root_side_length;
	for (size_t i = 1; i < sideLengths.size(); ++i) {
		sideLengths[i] = sideLengths[i - 1] / 2.0;
	}
	// Create first four children of the root.
	root->Status = TerrainNode::node_status::Active;
	
	nodes.resize(std::pow(4, MAX_LOD_LEVEL));

	
}
