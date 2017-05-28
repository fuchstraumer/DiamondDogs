#include "stdafx.h"
#include "TerrainNode.h"
#include "engine\renderer\resource\Buffer.h"
#include "engine\util\sphere.h"
#include "NodeSubset.h"
#include "glm\ext.hpp"
void vulpes::terrain::TerrainNode::Subdivide() {
	double child_length = SideLength / 2.0;
	double child_offset = SideLength / 4.0;
	glm::ivec2 grid_pos = glm::ivec2(2 * LogicalCoordinates.x, 2 * LogicalCoordinates.y);
	glm::vec3 pos = glm::vec3(SpatialCoordinates.x, 0.0f, SpatialCoordinates.z);
	children[0] = std::make_unique<TerrainNode>(device, Depth + 1, grid_pos, pos + glm::vec3(-child_offset, 0.0f, child_offset), child_length, MaxLOD);
	children[1] = std::make_unique<TerrainNode>(device, Depth + 1, glm::ivec2(grid_pos.x + 1, grid_pos.y), pos + glm::vec3(child_offset, 0.0f, child_offset), child_length, MaxLOD);
	children[2] = std::make_unique<TerrainNode>(device, Depth + 1, glm::ivec2(grid_pos.x, grid_pos.y + 1), pos + glm::vec3(-child_offset, 0.0f, -child_offset), child_length, MaxLOD);
	children[3] = std::make_unique<TerrainNode>(device, Depth + 1, glm::ivec2(grid_pos.x + 1, grid_pos.y + 1), pos + glm::vec3(child_offset, 0.0f, -child_offset), child_length, MaxLOD);
}

vulpes::terrain::TerrainNode::TerrainNode(const Device* device, const size_t& depth, const glm::ivec2& logical_coords, const glm::vec3& position, const double& length, const size_t& max_lod) : LogicalCoordinates(logical_coords), SideLength(length), Depth(depth), aabb({ position - static_cast<float>(length / 2.0), position + static_cast<float>(length / 2.0) }), SpatialCoordinates(position), device(device), MaxLOD(max_lod) {
}


void vulpes::terrain::TerrainNode::CreateMesh() {
	if (mesh.Ready()) {
		return;
	}
	mesh = std::move(Mesh(SpatialCoordinates, glm::vec3(SideLength, 1.0f, SideLength)));
	int i = 0;
	for (auto& vert : aabb_vertices) {
		mesh.add_vertex(vertex_t{ (vert / 2.0f) + glm::vec3(0.5f, 0.5f, -0.5f) });
		mesh.add_triangle(aabb_indices[i * 3], aabb_indices[i * 3 + 1], aabb_indices[i * 3 + 2]);
		++i;
	}
	mesh.create_buffers(device);
}

void vulpes::terrain::TerrainNode::TransferMesh(VkCommandBuffer & cmd) {
	mesh.record_transfer_commands(cmd);
}

bool vulpes::terrain::TerrainNode::Leaf() const noexcept {
	for (auto& child : children) {
		if (child != nullptr) {
			return false;
		}
	}
	return true;
}

void vulpes::terrain::TerrainNode::Update(const glm::dvec3 & camera_position, NodeSubset* active_nodes, const util::view_frustum& view) {
	// Get distance from camera to bounds of this node.
	// Radius of sphere is 1.1 times current node side length, which specifies
	// the range from a node we consider to be the LOD switch distance
	const util::Sphere lod_sphere{ camera_position, SideLength * 1.10 };
	const util::Sphere render_sphere{ camera_position, glm::length(mesh.scale) };
	// If this Node isn't already part of the highest detail level and we're in LOD switch
	// range, subdivide the node and set status.
	if (Depth < MaxLOD && !lod_sphere.CoincidesWith(this->aabb)) {
		if (Leaf()) {
			Subdivide();
		}

		for (auto& child : children) {
			child->Update(camera_position, active_nodes, view);
		}

		Status = NodeStatus::Subdivided;
	}
	else {
		
		if (glm::distance(aabb.Center(), camera_position) > MaxRenderDistance) {
			Status = NodeStatus::OutOfRange;
		}

		// Update statuses, add to renderlist if still active.
		if (render_sphere.CoincidesWithFrustum(view)) {
			if (mesh.Ready()) {
				Status = NodeStatus::Active;
			}
			else {
				CreateMesh();
				Status = NodeStatus::NeedsTransfer;
				active_nodes->AddNode(this, false);
			}
		}
		else if (!render_sphere.CoincidesWithFrustum(view)) {
			Status = NodeStatus::OutOfFrustum;
		}
	}
	
}

void vulpes::terrain::TerrainNode::BuildCommandBuffer(VkCommandBuffer & cmd) const {
	mesh.render(cmd);
}

void vulpes::terrain::TerrainNode::Prune(){
	for (auto& child : children) {
		if (child == nullptr) {
			continue;
		}
		child->Prune();
		child.reset();
	}
	mesh.cleanup();
}

double vulpes::terrain::TerrainNode::Size(){
	return pow(2, static_cast<double>(Depth));
}

