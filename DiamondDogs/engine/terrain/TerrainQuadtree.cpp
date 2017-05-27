#include "stdafx.h"
#include "TerrainQuadtree.h"
#include "engine/renderer/resource/Buffer.h"

vulpes::terrain::TerrainQuadtree::TerrainQuadtree(const Device* device, const float & split_factor, const size_t & max_detail_level, const double& root_side_length, const glm::vec3& root_tile_position) : activeNodes(device) {
	root = std::make_unique<TerrainNode>(nullptr, glm::ivec2(0, 0), root_tile_position, root_side_length, CubemapFace::TOP);
}

void vulpes::terrain::TerrainQuadtree::UpdateQuadtree(const glm::dvec3 & camera_position, const glm::mat4& view) {
	if (!activeNodes.nodes.empty()) {
		activeNodes.nodes.clear();
	}

	// Create new view frustum
	util::view_frustum view_f;
	glm::mat4 matrix = activeNodes.uboData.projection * view;
	// Updated as right, left, top, bottom, back (near), front (far)
	view_f[0] = glm::vec4(matrix[0].w + matrix[0].x, matrix[1].w + matrix[1].x, matrix[2].w + matrix[2].x, matrix[3].w + matrix[3].x);
	view_f[1] = glm::vec4(matrix[0].w - matrix[0].x, matrix[1].w - matrix[1].x, matrix[2].w - matrix[2].x, matrix[3].w - matrix[3].x);
	view_f[2] = glm::vec4(matrix[0].w - matrix[0].y, matrix[1].w - matrix[1].y, matrix[2].w - matrix[2].y, matrix[3].w - matrix[3].y);
	view_f[3] = glm::vec4(matrix[0].w + matrix[0].y, matrix[1].w + matrix[1].y, matrix[2].w + matrix[2].y, matrix[3].w + matrix[3].y);
	view_f[4] = glm::vec4(matrix[0].w + matrix[0].z, matrix[1].w + matrix[1].z, matrix[2].w + matrix[2].z, matrix[3].w + matrix[3].z);
	view_f[5] = glm::vec4(matrix[0].w - matrix[0].z, matrix[1].w - matrix[1].z, matrix[2].w - matrix[2].z, matrix[3].w - matrix[3].z);
	
	for (size_t i = 0; i < view_f.planes.size(); ++i) {
		float length = std::sqrtf(view_f[i].x * view_f[i].x + view_f[i].y * view_f[i].y + view_f[i].z * view_f[i].z + view_f[i].w * view_f[i].w);
		view_f[i] /= length;
	}

	root->Update(camera_position, &activeNodes, view_f);
	if (!activeNodes.nodes.empty()) {
		activeNodes.UpdateUBO(view);
	}
}

void vulpes::terrain::TerrainQuadtree::BuildCommandBuffers(VkCommandBuffer & cmd) {
	// We don't explicitly call the root, we hand this off to the node subset and let it build commands
	// only for the currently active nodes in this frame.
	activeNodes.BuildCommandBuffers(cmd);
}
