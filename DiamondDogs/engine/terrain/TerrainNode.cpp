#include "stdafx.h"
#include "TerrainNode.h"
#include "engine\renderer\resource\Buffer.h"
#include "engine\util\sphere.h"
#include "NodeSubset.h"
#include "glm\ext.hpp"

bool vulpes::terrain::TerrainNode::DrawAABB = false;

gli::texture2d vulpes::terrain::TerrainNode::heightmap = gli::texture2d(gli::load("rsrc/img/terrain_height.dds"));
gli::texture2d vulpes::terrain::TerrainNode::normalmap = gli::texture2d(gli::load("rsrc/img/terrain_normals.dds"));

void vulpes::terrain::TerrainNode::Subdivide() {
	double child_length = SideLength / 2.0;
	double child_offset = SideLength / 4.0;
	glm::ivec2 grid_pos = glm::ivec2(2 * LogicalCoordinates.x, 2 * LogicalCoordinates.y);
	glm::vec3 pos = glm::vec3(SpatialCoordinates.x, 0.0f, SpatialCoordinates.z);
	children[0] = std::make_unique<TerrainNode>(device, Depth + 1, grid_pos, pos, child_length, MaxLOD, switchRatio);
	children[1] = std::make_unique<TerrainNode>(device, Depth + 1, glm::ivec2(grid_pos.x + 1, grid_pos.y), pos + glm::vec3(child_length, 0.0f, 0.0f), child_length, MaxLOD, switchRatio);
	children[2] = std::make_unique<TerrainNode>(device, Depth + 1, glm::ivec2(grid_pos.x, grid_pos.y + 1), pos + glm::vec3(0.0f, 0.0f, -child_length), child_length, MaxLOD, switchRatio);
	children[3] = std::make_unique<TerrainNode>(device, Depth + 1, glm::ivec2(grid_pos.x + 1, grid_pos.y + 1), pos + glm::vec3(child_length, 0.0f, -child_length), child_length, MaxLOD, switchRatio);
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

constexpr size_t N_VERTS_PER_SIDE = 4;

void vulpes::terrain::TerrainNode::CreateMesh() {
	if (mesh.Ready()) {
		return;
	}
	mesh = std::move(Mesh(SpatialCoordinates, glm::vec3(SideLength, SideLength / 2.0f, SideLength)));
	
	{

		int vcount2 = N_VERTS_PER_SIDE + 1;
		int num_tris = N_VERTS_PER_SIDE * N_VERTS_PER_SIDE * 6;
		int num_verts = vcount2 * vcount2;
		mesh.vertices.resize(num_verts);

		float scale = 1.0f / N_VERTS_PER_SIDE;
		glm::vec3 offset = glm::vec3(0.5f, 0.0f, -0.5f);
		int idx = 0;

		auto height_sample = [this](uint32_t x, uint32_t y) {
			const auto dim = static_cast<uint32_t>(heightmap.extent().x);
			const auto h_scale = dim / (N_VERTS_PER_SIDE * (Depth + 1));

		};

		for (int y = 0; y < vcount2; ++y) {
			for (int x = 0; x < vcount2; ++x) {
				mesh.vertices.positions[idx] = glm::vec3(static_cast<float>(x) * scale - 0.5f + offset.x, 0.0f, static_cast<float>(y)*scale - 1.0f);
				mesh.vertices.normals_uvs[idx].uv = glm::vec2((x * scale) / Depth, (y * scale) / Depth);
				++idx;
			}
		}

		mesh.indices.resize(num_tris);
		idx = 0;
		for (int y = 0; y < N_VERTS_PER_SIDE; ++y) {
			for (int x = 0; x < N_VERTS_PER_SIDE; ++x) {
				mesh.indices[idx+0] = (y * vcount2) + x;
				mesh.indices[idx+1] = ((y + 1) * vcount2) + x;
				mesh.indices[idx+2] = (y * vcount2) + x + 1;
				mesh.indices[idx+3] = ((y + 1) * vcount2) + x;
				mesh.indices[idx+4] = ((y + 1) * vcount2) + x + 1;
				mesh.indices[idx+5] = (y * vcount2) + x + 1;
				idx += 6;
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

bool vulpes::terrain::TerrainNode::Leaf() const {
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
	const util::Sphere lod_sphere{ camera_position, SideLength * switchRatio };
	const util::Sphere aabb_sphere{ aabb.Center(), SideLength };
	 
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

