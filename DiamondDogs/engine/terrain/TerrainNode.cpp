#include "stdafx.h"
#include "TerrainNode.h"
#include "engine\renderer\resource\Buffer.h"

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

