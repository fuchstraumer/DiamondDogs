#include "stdafx.h"
#include "TerrainQuadtree.h"
#include "engine/renderer/resource/Buffer.h"

vulpes::terrain::TerrainQuadtree::TerrainQuadtree(const Device* device, const float & split_factor, const size_t & max_detail_level, const double& root_side_length, const glm::vec3& root_tile_position) : activeNodes(device) {
	root = std::make_unique<TerrainNode>(device, 0, glm::ivec2(0, 0), root_tile_position, root_side_length, max_detail_level, split_factor);
}

void vulpes::terrain::TerrainQuadtree::SetupNodePipeline(const VkRenderPass & renderpass, const Swapchain * swapchain, std::shared_ptr<PipelineCache>& cache, const glm::mat4 & projection) {
	activeNodes.CreatePipeline(renderpass, swapchain, cache, projection);
}

void vulpes::terrain::TerrainQuadtree::UpdateQuadtree(const glm::dvec3 & camera_position, const glm::mat4& view) {

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
}

void vulpes::terrain::TerrainQuadtree::UpdateNodes(VkCommandBuffer & graphics_cmd, VkCommandBufferBeginInfo& begin_info, const glm::mat4 & view, const VkViewport& viewport, const VkRect2D& rect) {
	activeNodes.Update(graphics_cmd, begin_info, view, viewport, rect); \
	{
		// Get distance from camera to bounds of this node.
		// Radius of sphere is 1.1 times current node side length, which specifies
		// the range from a node we consider to be the LOD switch distance
		const util::Sphere lod_sphere{ camera_position, SideLength * SwitchRatio };
		const util::Sphere aabb_sphere{ SpatialCoordinates + static_cast<float>(SideLength), SideLength };

		// Depth is less than max subdivide level and we're in subdivide range.
		if (Depth < MaxLOD && lod_sphere.CoincidesWith(this->aabb)) {
			if (Leaf()) {
				Status = NodeStatus::Subdivided;
				mesh.cleanup();
				if (DrawAABB) {
					util::AABB::aabbPool.erase(GridCoordinates);
					aabb.mesh.cleanup();
				}
				Subdivide();
			}
			for (auto& child : Children) {
				child->Update(camera_position, active_nodes, view);
			}
		}
		else if (glm::distance(camera_position, SpatialCoordinates) > MaxRenderDistance) {
			Status = NodeStatus::NeedsUnload;
			if (!Leaf()) {
				Prune();
			}
		}
		else {
			if (aabb_sphere.CoincidesWithFrustum(view)) {
				if (mesh.Ready()) {
					Status = NodeStatus::Active;
					active_nodes->AddNode(this, true);
				}
				else {
					Status = NodeStatus::NeedsTransfer;
					active_nodes->AddNode(this, false);
				}
			}
			if (!Leaf()) {
				Prune();
			}
		}

	}
}

