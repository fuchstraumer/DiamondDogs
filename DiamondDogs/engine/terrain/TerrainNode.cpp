#include "stdafx.h"
#include "TerrainNode.h"


void vulpes::terrain::TerrainNode::Subdivide() {
	double child_length = SideLength / 2.0;
	double child_offset = SideLength / 4.0;
	glm::ivec3 grid_pos = glm::ivec3(2 * GridCoordinates.x, 2 * GridCoordinates.y, Depth() + 1);
	glm::vec3 pos = glm::vec3(SpatialCoordinates.x, 0.0f, SpatialCoordinates.z);
	Children[0] = std::make_shared<TerrainNode>(grid_pos, pos, child_length);
	Children[1] = std::make_shared<TerrainNode>(glm::ivec3(grid_pos.x + 1, grid_pos.y, grid_pos.z), pos + glm::vec3(child_length, 0.0f, 0.0f), child_length);
	Children[2] = std::make_shared<TerrainNode>(glm::ivec3(grid_pos.x, grid_pos.y + 1, grid_pos.z), pos + glm::vec3(0.0f, 0.0f, -child_length), child_length);
	Children[3] = std::make_shared<TerrainNode>(glm::ivec3(grid_pos.x + 1, grid_pos.y + 1, grid_pos.z), pos + glm::vec3(child_length, 0.0f, -child_length), child_length);
}

vulpes::terrain::TerrainNode::TerrainNode(const glm::ivec3& logical_coords, const glm::vec3& position, const double& length) : 
	GridCoordinates(logical_coords), SideLength(length), aabb({ position, position + static_cast<float>(length) }), 
	SpatialCoordinates(position) {}

vulpes::terrain::TerrainNode::~TerrainNode() {
	if (!Leaf()) {
		for (auto& child : Children) {
			child.reset();
		}
	}
}

bool vulpes::terrain::TerrainNode::Leaf() const {
	for (auto& child : Children) {
		if (child) {
			return false;
		}
	}
	return true;
}

void vulpes::terrain::TerrainNode::Prune(){
	for (auto& child : Children) {
		if (!child) {
			continue;
		}
		child->Prune();
		child.reset();
	}
}

int vulpes::terrain::TerrainNode::Depth() const {
	return GridCoordinates.z;
}
