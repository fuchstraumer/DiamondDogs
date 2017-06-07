#include "stdafx.h"
#include "TerrainQuadtree.h"
#include "engine/renderer/resource/Buffer.h"
#include "engine\util\sphere.h"

size_t vulpes::terrain::TerrainQuadtree::MaxLOD = 12;
double vulpes::terrain::TerrainQuadtree::SwitchRatio = 1.20;

void vulpes::terrain::TerrainQuadtree::pruneNode(const std::shared_ptr<TerrainNode>& node) {
	if (!node->Leaf()) {
		for (auto& child : node->Children) {
			pruneNode(child);
		}
	}
	activeNodes.erase(node);
	nodeRenderer.RemoveNode(node.get());
}

vulpes::terrain::TerrainQuadtree::TerrainQuadtree(const Device* device, const float & split_factor, const size_t & max_detail_level, const double& root_side_length, const glm::vec3& root_tile_position) : nodeRenderer(device) {
	root = new TerrainNode(glm::ivec3(0, 0, 0), root_tile_position, root_side_length);
	activeNodes.insert(std::shared_ptr<TerrainNode>(root));
	// cachedNodes.insert(std::make_pair(glm::ivec2(0, 0), std::shared_ptr<TerrainNode>(root));
}

void vulpes::terrain::TerrainQuadtree::SetupNodePipeline(const VkRenderPass & renderpass, const Swapchain * swapchain, std::shared_ptr<PipelineCache>& cache, const glm::mat4 & projection) {
	nodeRenderer.CreatePipeline(renderpass, swapchain, cache, projection);
}

void vulpes::terrain::TerrainQuadtree::UpdateQuadtree(const glm::vec3 & camera_position, const glm::mat4& view) {

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

	nodeSubset::iterator iter = activeNodes.begin();

	while(iter != activeNodes.end()) {
		// Get distance from camera to bounds of this node.
		// Radius of sphere is 1.1 times current node side length, which specifies
		// the range from a node we consider to be the LOD switch distance
		auto& curr = *iter;
		const util::Sphere lod_sphere{ camera_position, curr->SideLength * SwitchRatio };
		const util::Sphere aabb_sphere{ curr->SpatialCoordinates + static_cast<float>(curr->SideLength), curr->SideLength };

		// Depth is less than max subdivide level and we're in subdivide range.
		if (curr->Depth() < MaxLOD && lod_sphere.CoincidesWith(curr->aabb)) {
			if (curr->Leaf()) {
				curr->Status = NodeStatus::Subdivided;
				if (NodeRenderer::DrawAABBs) {
					util::AABB::aabbPool.erase(curr->GridCoordinates);
					curr->aabb.mesh.cleanup();
				}
				curr->mesh.cleanup();
				curr->Subdivide();
			}
			for (auto& child : curr->Children) {
				activeNodes.insert(child);
			}
			nodeRenderer.RemoveNode(&(*curr));
			activeNodes.erase(iter++);
			continue;
		}
		else if ((glm::distance(camera_position, curr->SpatialCoordinates) > NodeRenderer::MaxRenderDistance) || (curr->Status == NodeStatus::NeedsUnload)) {
			curr->Status = NodeStatus::NeedsUnload;
			if (!curr->Leaf()) {
				pruneNode(curr);
				++iter;
			}
			else {
				nodeRenderer.RemoveNode(&(*curr));
				activeNodes.erase(iter++);
			}
			continue;
		}
		else {
			if (aabb_sphere.CoincidesWithFrustum(view_f)) {
				if (curr->mesh.Ready()) {
					curr->Status = NodeStatus::Active;
					nodeRenderer.AddNode(&(*curr), true);
				}
				else {
					curr->Status = NodeStatus::NeedsTransfer;
					nodeRenderer.AddNode(&(*curr), false);
				}
			}
			if (!curr->Leaf()) {
				for (auto& child : curr->Children) {
					pruneNode(child);
				}
			}
			++iter;
		}
		
	}
}

void vulpes::terrain::TerrainQuadtree::RenderNodes(VkCommandBuffer & graphics_cmd, VkCommandBufferBeginInfo& begin_info, const glm::mat4 & view, const VkViewport& viewport, const VkRect2D& rect) {
	nodeRenderer.Render(graphics_cmd, begin_info, view, viewport, rect); 
}

