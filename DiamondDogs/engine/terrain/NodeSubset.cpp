#include "stdafx.h"
#include "NodeSubset.h"
#include "TerrainNode.h"

void vulpes::terrain::NodeSubset::AddNode(const TerrainNode * node){
	// Since LOD level is based on distance, we use LOD level to compare
	// nodes and place highest LOD level first, in hopes that it renders first.
	if (sortNodesByDistance) {
		nodes.push_front(node);
		std::push_heap(nodes.begin(), nodes.end());
	}
	else {
		nodes.push_front(node);
	}
}

void vulpes::terrain::NodeSubset::BuildCommandBuffers(VkCommandBuffer& cmd) {
	while (!nodes.empty()) {
		auto& node = nodes.front();
		node->Render(cmd);
		nodes.pop_front();
	}
}
