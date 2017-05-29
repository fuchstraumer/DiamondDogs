#include "stdafx.h"
#include "TerrainNode.h"
#include "engine\renderer\resource\Buffer.h"
#include "engine\util\sphere.h"
#include "NodeSubset.h"
#include "glm\ext.hpp"

bool vulpes::terrain::TerrainNode::DrawAABB = true;

void vulpes::terrain::TerrainNode::Subdivide() {
	double child_length = SideLength / 2.0;
	double child_offset = SideLength / 4.0;
	glm::ivec2 grid_pos = glm::ivec2(2 * LogicalCoordinates.x, 2 * LogicalCoordinates.y);
	glm::vec3 pos = glm::vec3(SpatialCoordinates.x + child_offset, 0.0f, SpatialCoordinates.z - child_offset);
	children[0] = std::make_unique<TerrainNode>(device, Depth + 1, grid_pos, pos + glm::vec3(-child_offset, 0.0f, child_offset), child_length, MaxLOD, switchRatio);
	children[1] = std::make_unique<TerrainNode>(device, Depth + 1, glm::ivec2(grid_pos.x + 1, grid_pos.y), pos + glm::vec3(child_offset, 0.0f, child_offset), child_length, MaxLOD, switchRatio);
	children[2] = std::make_unique<TerrainNode>(device, Depth + 1, glm::ivec2(grid_pos.x, grid_pos.y + 1), pos + glm::vec3(-child_offset, 0.0f, -child_offset), child_length, MaxLOD, switchRatio);
	children[3] = std::make_unique<TerrainNode>(device, Depth + 1, glm::ivec2(grid_pos.x + 1, grid_pos.y + 1), pos + glm::vec3(child_offset, 0.0f, -child_offset), child_length, MaxLOD, switchRatio);
}

vulpes::terrain::TerrainNode::TerrainNode(const Device* device, const size_t& depth, const glm::ivec2& logical_coords, const glm::vec3& position, const double& length, const size_t& max_lod, const double& switch_ratio) : LogicalCoordinates(logical_coords), SideLength(length), Depth(depth), aabb({ position - static_cast<float>(length / 2.0), position + static_cast<float>(length / 2.0) }), SpatialCoordinates(position), device(device), MaxLOD(max_lod), switchRatio(switch_ratio) {}

vulpes::terrain::TerrainNode::~TerrainNode() {
	if (!Leaf()) {
		for (auto& child : children) {
			child.reset();
		}
	}
	mesh.cleanup();
	if (DrawAABB) {
		aabb_mesh.cleanup();
	}
}

constexpr size_t N_VERTS_PER_SIDE = 8;

void vulpes::terrain::TerrainNode::CreateMesh() {
	if (mesh.Ready()) {
		return;
	}
	mesh = std::move(Mesh(SpatialCoordinates, glm::vec3(SideLength - 0.001, SideLength / 2.0f, SideLength - 0.001)));
	
	{
		int i = 0;
		float step_size = 1.0f / N_VERTS_PER_SIDE;
		int v_count = N_VERTS_PER_SIDE;
		int v_count2 = v_count * v_count;
		for (int z = 0; z < v_count2; ++z) {
			for (int x = 0; x < v_count2; ++x) {
				mesh.add_vertex(glm::vec3(x * step_size - 1.0f, 0.0f, z * step_size - 1.0f));
			}
		}

		
		for (int z = 0; z < N_VERTS_PER_SIDE; ++z) {
			for (int x = 0; x < N_VERTS_PER_SIDE; ++x) {
				mesh.add_triangle((z * v_count2) + x, ((z + 1) * v_count2) + x, (z * v_count2) + x + 1);
				mesh.add_triangle(((z + 1) * v_count2) + x, (z * v_count2) + x + 1, (z * v_count2) + x + 1);
			}
		}

	}
	if (DrawAABB) {
		aabb_mesh = std::move(Mesh(SpatialCoordinates, glm::vec3(SideLength, SideLength / 2.0f, SideLength)));
		int i = 0;
		for (auto& vert : aabb_vertices) {
			aabb_mesh.add_vertex(vertex_t{ (vert / 2.0f) + glm::vec3(0.5f, 0.5f, -0.5f) });
			aabb_mesh.add_triangle(aabb_indices[i * 3], aabb_indices[i * 3 + 1], aabb_indices[i * 3 + 2]);
			++i;
		}
		aabb_mesh.create_buffers(device);
	}
	
	mesh.create_buffers(device);
}

void vulpes::terrain::TerrainNode::TransferMesh(VkCommandBuffer & cmd) {
	mesh.record_transfer_commands(cmd);
	if(DrawAABB){
		aabb_mesh.record_transfer_commands(cmd);
	}
}

bool vulpes::terrain::TerrainNode::Leaf() const noexcept {
	for (auto& child : children) {
		if (child) {
			return false;
		}
	}
	return true;
}

void vulpes::terrain::TerrainNode::Update(const glm::vec3 & camera_position, NodeSubset* active_nodes, const util::view_frustum& view) {
	// Get distance from camera to bounds of this node.
	// Radius of sphere is 1.1 times current node side length, which specifies
	// the range from a node we consider to be the LOD switch distance
	const util::Sphere lod_sphere{ camera_position, SideLength * 1.10 };
	const util::Sphere aabb_sphere{ SpatialCoordinates, SideLength };
	 
	// Depth is less than max subdivide level and we're in subdivide range.
	if (Depth < MaxLOD && lod_sphere.CoincidesWith(this->aabb)) {
		if (Leaf()) {
			Status = NodeStatus::Subdivided;
			mesh.cleanup();
			if (DrawAABB) {
				aabb_mesh.cleanup();
			}
			Subdivide();
		}
		for (auto& child : children) {
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

void vulpes::terrain::TerrainNode::BuildCommandBuffer(VkCommandBuffer & cmd) const {
	mesh.render(cmd);
	if (DrawAABB) {
		aabb_mesh.render(cmd);
	}
}

void vulpes::terrain::TerrainNode::Prune(){
	for (auto& child : children) {
		if (!child) {
			continue;
		}
		child->Prune();
		child.reset();
	}
}

double vulpes::terrain::TerrainNode::Size(){
	return pow(2, static_cast<double>(Depth));
}

