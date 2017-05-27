#include "stdafx.h"
#include "TerrainNode.h"
#include "engine\renderer\resource\Buffer.h"
#include "engine\util\sphere.h"
#include "NodeSubset.h"

void vulpes::terrain::TerrainNode::Subdivide() {
	double child_length = SideLength / 2.0f;
	glm::ivec2 grid_pos = glm::ivec2(2 * LogicalCoordinates.x, 2 * LogicalCoordinates.y);
	glm::vec3 pos = glm::vec3(SpatialCoordinates.x, SpatialCoordinates.y, SpatialCoordinates.z);
	children[0] = std::make_unique<TerrainNode>(this, grid_pos, pos, child_length, Face);
	children[1] = std::make_unique<TerrainNode>(this, glm::ivec2(grid_pos.x + 1, grid_pos.y), pos, child_length, Face);
	children[2] = std::make_unique<TerrainNode>(this, glm::ivec2(grid_pos.x, grid_pos.y + 1), glm::vec3(pos.x + child_length, pos.y, pos.z), child_length, Face);
	children[3] = std::make_unique<TerrainNode>(this, glm::ivec2(grid_pos.x + 1, grid_pos.y + 1), glm::vec3(pos.x + child_length, pos.y + child_length, pos.z), child_length, Face);
}

vulpes::terrain::TerrainNode::TerrainNode(const size_t& depth, const glm::ivec2& logical_coords, const glm::vec3& position, const double& length) : LogicalCoordinates(logical_coords), SideLength(length), Depth(depth), aabb({ position, position + static_cast<float>(length) }) {}


void vulpes::terrain::TerrainNode::CreateMesh(const Device* render_device) {
	if (mesh.Ready()) {
		return;
	}
	mesh = Mesh(SpatialCoordinates, glm::vec3(static_cast<float>(SideLength / 10.0)));
	int i = 0;
	for (auto& vert : aabb_vertices) {
		mesh.add_vertex(vertex_t{ vert });
		mesh.add_triangle(aabb_indices[i * 3], aabb_indices[i * 3 + 1], aabb_indices[i * 3 + 2]);
		++i;
	}
}

bool vulpes::terrain::TerrainNode::Leaf() const noexcept {
	return std::all_of(children.cbegin(), children.cend(), [](const std::shared_ptr<TerrainNode>& node) { return node.get() != nullptr; });
}

void vulpes::terrain::TerrainNode::Update(const glm::dvec3 & camera_position, NodeSubset* active_nodes, const util::view_frustum& view) {
	// Get distance from camera to bounds of this node.
	// Radius of sphere is 1.1 times current node side length, which specifies
	// the range from a node we consider to be the LOD switch distance
	const util::Sphere lod_sphere{ camera_position, SideLength * 1.10 };
	const util::Sphere render_sphere{ camera_position, glm::length(aabb.Extents()) };
	// If this Node isn't already part of the highest detail level and we're in LOD switch
	// range, subdivide the node and set status.
	if (Depth < MaxLOD && lod_sphere.CoincidesWith(this->aabb)) {
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
			active_nodes->AddNode(this);
			Prune();
		}

		// Update statuses, add to renderlist if still active.
		if (render_sphere.CoincidesWithFrustum(view)) {
			if (mesh.Ready()) {
				Status = NodeStatus::Active;
			}
			else {
				Status = NodeStatus::MeshUnbuilt;
			}
		}
		else {
			Status = NodeStatus::OutOfFrustum;
			Prune();
		}
	}
	active_nodes->AddNode(this);
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

