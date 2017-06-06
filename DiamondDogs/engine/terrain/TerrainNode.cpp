#include "stdafx.h"
#include "TerrainNode.h"
#include "NodeSubset.h"

bool vulpes::terrain::TerrainNode::DrawAABB = true;
float vulpes::terrain::TerrainNode::MaxRenderDistance = 12000.0f;

void vulpes::terrain::TerrainNode::Subdivide() {
	double child_length = SideLength / 2.0;
	double child_offset = SideLength / 4.0;
	glm::ivec2 grid_pos = glm::ivec2(2 * GridCoordinates.x, 2 * GridCoordinates.y);
	glm::vec3 pos = glm::vec3(SpatialCoordinates.x, 0.0f, SpatialCoordinates.z);
	Children[0] = std::make_unique<TerrainNode>(device, Depth + 1, grid_pos, pos, child_length, MaxLOD, SwitchRatio);
	Children[1] = std::make_unique<TerrainNode>(device, Depth + 1, glm::ivec2(grid_pos.x + 1, grid_pos.y), pos + glm::vec3(child_length, 0.0f, 0.0f), child_length, MaxLOD, SwitchRatio);
	Children[2] = std::make_unique<TerrainNode>(device, Depth + 1, glm::ivec2(grid_pos.x, grid_pos.y + 1), pos + glm::vec3(0.0f, 0.0f, -child_length), child_length, MaxLOD, SwitchRatio);
	Children[3] = std::make_unique<TerrainNode>(device, Depth + 1, glm::ivec2(grid_pos.x + 1, grid_pos.y + 1), pos + glm::vec3(child_length, 0.0f, -child_length), child_length, MaxLOD, SwitchRatio);
}

vulpes::terrain::TerrainNode::TerrainNode(const Device* device, const size_t& depth, const glm::ivec2& logical_coords, const glm::vec3& position, const double& length, const size_t& max_lod, const double& switch_ratio) : 
	GridCoordinates(logical_coords), SideLength(length), Depth(depth), aabb({ position, position + static_cast<float>(length) }), 
	SpatialCoordinates(position), device(device), MaxLOD(max_lod), SwitchRatio(switch_ratio) {}

vulpes::terrain::TerrainNode::~TerrainNode() {
	if (!Leaf()) {
		for (auto& child : Children) {
			child.reset();
		}
	}
	if (DrawAABB) {
		util::AABB::aabbPool.erase(GridCoordinates);
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
