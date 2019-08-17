#include "TerrainQuadtree.hpp"
#include "TerrainNode.hpp"
#include "AABB.hpp"

TerrainQuadtree::TerrainQuadtree(const float & split_factor, const size_t & max_detail_level, const double& root_side_length, const glm::vec3& root_tile_position) : MaxLOD(max_detail_level) {
	root = std::make_unique<TerrainNode>(glm::ivec3(0, 0, 0), glm::ivec3(0, 0, 0), root_tile_position, root_side_length);
	TerrainNode::MaxLOD = MaxLOD;
	TerrainNode::SwitchRatio = split_factor;
	auto root_noise = GetNoiseHeightmap(HeightNode::RootSampleGridSize, glm::vec3(0.0f), static_cast<float>(HeightNode::RootSampleGridSize / (HeightNode::RootNodeLength * 2.0)));
	root->HeightData = std::make_unique<HeightNode>(glm::ivec3(0, 0, 0), root_noise);
}

void TerrainQuadtree::UpdateQuadtree(const glm::vec3 & camera_position, const glm::mat4& view, const glm::mat4& projection) {

	// Create new view frustum
    
	glm::mat4 matrix = projection * view;
    ViewFrustum view_f;
	// Updated as right, left, top, bottom, back (near), front (far)
	view_f[0] = glm::vec4(matrix[0].w + matrix[0].x, matrix[1].w + matrix[1].x, matrix[2].w + matrix[2].x, matrix[3].w + matrix[3].x);
	view_f[1] = glm::vec4(matrix[0].w - matrix[0].x, matrix[1].w - matrix[1].x, matrix[2].w - matrix[2].x, matrix[3].w - matrix[3].x);
	view_f[2] = glm::vec4(matrix[0].w - matrix[0].y, matrix[1].w - matrix[1].y, matrix[2].w - matrix[2].y, matrix[3].w - matrix[3].y);
	view_f[3] = glm::vec4(matrix[0].w + matrix[0].y, matrix[1].w + matrix[1].y, matrix[2].w + matrix[2].y, matrix[3].w + matrix[3].y);
	view_f[4] = glm::vec4(matrix[0].w + matrix[0].z, matrix[1].w + matrix[1].z, matrix[2].w + matrix[2].z, matrix[3].w + matrix[3].z);
	view_f[5] = glm::vec4(matrix[0].w - matrix[0].z, matrix[1].w - matrix[1].z, matrix[2].w - matrix[2].z, matrix[3].w - matrix[3].z);

	for (size_t i = 0; i < 6; ++i) {
		float length = std::sqrtf(view_f[i].x * view_f[i].x + view_f[i].y * view_f[i].y + view_f[i].z * view_f[i].z + view_f[i].w * view_f[i].w);
		view_f[i] /= length;
	}

	root->Update(camera_position, view_f);
	
}
