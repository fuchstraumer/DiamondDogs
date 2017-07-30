#include "stdafx.h"
#include "TerrainQuadtree.h"
#include "engine\util\sphere.h"


vulpes::terrain::TerrainQuadtree::TerrainQuadtree(const Device* device, const float & split_factor, const size_t & max_detail_level, const double& root_side_length, const glm::vec3& root_tile_position) : nodeRenderer(device), MaxLOD(max_detail_level) {
	root = new TerrainNode(glm::ivec3(0, 0, 0), glm::ivec3(0, 0, 0), root_tile_position, root_side_length);
	TerrainNode::MaxLOD = MaxLOD;
	TerrainNode::SwitchRatio = split_factor;
	auto root_noise = GetNoiseHeightmap(HeightNode::RootSampleGridSize, glm::vec3(0.0f), static_cast<float>(HeightNode::RootSampleGridSize / (HeightNode::RootNodeLength * 2.0)));
	auto root_height = std::make_shared<HeightNode>(glm::ivec3(0, 0, 0), root_noise);
	root->SetHeightData(root_height);
}

vulpes::terrain::TerrainQuadtree::~TerrainQuadtree() { 
	delete root;
}

void vulpes::terrain::TerrainQuadtree::SetupNodePipeline(const VkRenderPass & renderpass, const Swapchain * swapchain, std::shared_ptr<PipelineCache>& cache, const glm::mat4 & projection) {
	nodeRenderer.CreatePipeline(renderpass, cache, projection);
}

void vulpes::terrain::TerrainQuadtree::UpdateQuadtree(const glm::vec3 & camera_position, const glm::mat4& view) {
	if (nodeRenderer.UpdateLOD) {
		// Create new view frustum
		util::view_frustum view_f;
		glm::mat4 matrix = nodeRenderer.uboData.projection * view;
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

		root->Update(camera_position, view_f, &nodeRenderer);
	}
}

void vulpes::terrain::TerrainQuadtree::RenderNodes(VkCommandBuffer & graphics_cmd, VkCommandBufferBeginInfo& begin_info, const glm::mat4 & view, const glm::vec3& camera_pos, const VkViewport& viewport, const VkRect2D& rect) {
	nodeRenderer.Render(graphics_cmd, begin_info, view, camera_pos, viewport, rect); 
}

