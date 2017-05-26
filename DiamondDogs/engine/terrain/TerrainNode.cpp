#include "stdafx.h"
#include "TerrainNode.h"
#include "engine\renderer\resource\Buffer.h"

vulpes::terrain::TerrainNode::TerrainNode(const TerrainNode * _parent, glm::vec2 logical_coords, double length, const CubemapFace& face) : LogicalCoordinates(logical_coords), SideLength(length), parent(_parent), Face(face) {
	Depth = (parent == nullptr) ? 0 : parent->Depth + 1;
	assert(Depth <= MAX_LOD_LEVEL);
}

bool vulpes::terrain::TerrainNode::Leaf() const noexcept {
	return std::all_of(children.cbegin(), children.cend(), [](const std::shared_ptr<TerrainNode>& node) { return node.get() == nullptr; });
}

void vulpes::terrain::TerrainNode::Render(VkCommandBuffer & cmd) {

}

void vulpes::terrain::TerrainNode::Prune(){
	for (auto& child : children) {
		if (child == nullptr) {
			continue;
		}
		child->Prune();
	}
	mesh.cleanup();
}

double vulpes::terrain::TerrainNode::Size(){
	return pow(2, static_cast<double>(Depth));
}

