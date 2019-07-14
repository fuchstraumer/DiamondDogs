#include "TerrainNode.hpp"
#include "NodeRenderer.hpp"

size_t TerrainNode::MaxLOD = 16;
float TerrainNode::SwitchRatio = 1.80f;

void TerrainNode::Subdivide() {
	double child_length = SideLength / 2.0;
	glm::ivec3 grid_pos = glm::ivec3(2 * GridCoordinates.x, 2 * GridCoordinates.y, LOD_Level() + 1);
	glm::vec3 pos = glm::vec3(SpatialCoordinates.x, 0.0f, SpatialCoordinates.z);

	// Create child node, then create child's height data object (populated later by compute shader)
	Children[0] = std::make_shared<TerrainNode>(GridCoordinates, grid_pos, pos + glm::vec3(0.0f, 0.0f, -child_length), child_length);
	Children[0]->CreateHeightData(HeightData.get());
	Children[1] = std::make_shared<TerrainNode>(GridCoordinates, glm::ivec3(grid_pos.x + 1, grid_pos.y, grid_pos.z), pos + glm::vec3(child_length, 0.0f, -child_length), child_length);
	Children[1]->CreateHeightData(HeightData.get());
	Children[2] = std::make_shared<TerrainNode>(GridCoordinates, glm::ivec3(grid_pos.x, grid_pos.y + 1, grid_pos.z), pos + glm::vec3(0.0f, 0.0f, 0.0f), child_length);
	Children[2]->CreateHeightData(HeightData.get());
	Children[3] = std::make_shared<TerrainNode>(GridCoordinates, glm::ivec3(grid_pos.x + 1, grid_pos.y + 1, grid_pos.z), pos + glm::vec3(child_length, 0.0f, 0.0f), child_length);
	Children[3]->CreateHeightData(HeightData.get());
}

void TerrainNode::Update(const glm::vec3 & camera_position, const util::view_frustum & view, NodeRenderer* node_pool) {
	// Get distance from camera to bounds of this node.
	// Radius of sphere is 1.1 times current node side length, which specifies
	// the range from a node we consider to be the LOD switch distance
	const util::Sphere lod_sphere{ camera_position, SideLength * SwitchRatio };
	const util::Sphere aabb_sphere{ aabb.Center() + static_cast<float>(SideLength / 2.0f), SideLength / 2.0f };
	// TODO: Investigate why the "LOD radius" of nodes appears to be off-center relative to the viewer.
	// Depth is less than max subdivide level and we're in subdivide range.
	if (this->LOD_Level() < MaxLOD && lod_sphere.CoincidesWith(this->aabb)) {
		if (this->IsLeaf()) {
			Status = NodeStatus::Subdivided;
			mesh.cleanup();
			Subdivide();
		}
		for (auto& child : Children) {
			child->Update(camera_position, view, node_pool);
		}
	}
	else if (glm::distance(camera_position, SpatialCoordinates) > NodeRenderer::MaxRenderDistance) {
		Status = NodeStatus::NeedsUnload;
		if (!IsLeaf()) {
			Prune();
		}
	}
	else {
		// TODO: Frustum culling appears to be super broken, as it stands.
		if (aabb_sphere.CoincidesWithFrustum(view)) {
			if (!mesh.vertices.positions.empty()) {
				Status = NodeStatus::Active;
				node_pool->AddNode(this);
			}
			else {
				if (heightDataFuture.valid()) {
					HeightData = heightDataFuture.get();
					Status = NodeStatus::NeedsTransfer;
					node_pool->AddNode(this);
				}
			}
		}
		if (!IsLeaf()) {
			Prune();
		}
	}
}

TerrainNode::TerrainNode(const glm::ivec3& parent_coords, const glm::ivec3& logical_coords, const glm::vec3& position, const double& length) : 
	ParentGridCoordinates(parent_coords), GridCoordinates(logical_coords), SideLength(length), aabb({ position, position + static_cast<float>(length) }), 
	SpatialCoordinates(position) {}

TerrainNode::~TerrainNode() {
	if (!IsLeaf()) {
		for (auto& child : Children) {
			child.reset();
		}
	}
	mesh.cleanup();
}

bool TerrainNode::IsLeaf() const {
	for (auto& child : Children) {
		if (child) {
			return false;
		}
	}
	return true;
}

void TerrainNode::Prune(){
	for (auto& child : Children) {
		if (!child) {
			continue;
		}
		child->Prune();
		child.reset();
	}
}

int TerrainNode::LOD_Level() const {
	return GridCoordinates.z;
}

void TerrainNode::CreateMesh(const Device * dvc) {
	mesh = mesh::PlanarMesh(SideLength, GridCoordinates, SpatialCoordinates + glm::vec3(0.0f, 0.0f, -SideLength), glm::vec3(1.0f));
	mesh.Generate(HeightData.get());
	mesh.create_buffers(dvc);
}

void TerrainNode::CreateHeightData(const HeightNode* parent_node) {
	auto create_height_node_from_parent = [](const glm::ivec3& _grid_coordinates, const HeightNode* _parent_node) {
		return std::move(std::make_unique<HeightNode>(_grid_coordinates, _parent_node));
	};
	heightDataFuture = std::async(std::launch::async, create_height_node_from_parent, GridCoordinates, parent_node);
}
