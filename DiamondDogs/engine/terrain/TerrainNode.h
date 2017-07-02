#pragma once
#ifndef VULPES_TERRAIN_NODE_H
#define VULPES_TERRAIN_NODE_H
#include "stdafx.h"

#include "engine\util\AABB.h"
#include "engine\renderer\ForwardDecl.h"
#include "engine\objects\mesh\PlanarMesh.h"
#include "HeightNode.h"
#include "DataProducer.h"

namespace vulpes {

	namespace terrain {

		class NodeRenderer;

		class TerrainNode {
			TerrainNode(const TerrainNode& other) = delete;
			TerrainNode& operator=(const TerrainNode& other) = delete;
		public:

			static size_t MaxLOD;
			static double SwitchRatio;

			TerrainNode(const glm::ivec3& parent_coords, const glm::ivec3& logical_coords, const glm::vec3& position, const double& length);

			~TerrainNode();

			void Subdivide();

			// updates this nodes status, and then the node adds itself to the renderer's pool of nodes if its going to be rendered.
			void Update(const glm::vec3 & camera_position, const util::view_frustum& view, NodeRenderer* node_pool);

			// true if all of the Child pointers are nullptr
			bool Leaf() const;

			// Recursive method to clean up node tree
			void Prune();

			int Depth() const;

			std::array<std::shared_ptr<TerrainNode>, 4> Children;
			std::shared_ptr<HeightNode> HeightData;

			// Load data from this when flag == ready, adding it to HeightData
			std::shared_ptr<DataRequest> upsampleRequest;

			// used w/ node renderer to decide if this is being rendered and/or if it needs data transferred
			// to the device (so we can batch transfer commands)
			NodeStatus Status;

			void CreateMesh(const Device* dvc);
			void CreateHeightData(const HeightNode& parent_node);
			void SetHeightData(const std::shared_ptr<HeightNode>& height_node);

			mesh::PlanarMesh mesh;

			// Coordinates of this node in the grid defining the quadtree
			// xy are the grid positions, z is the LOD level.
			glm::ivec3 GridCoordinates;
			glm::ivec3 ParentGridCoordinates;

			// world-relative spatial coordinates. 
			// TODO: Investigate root-node relative, for the sake of large-scale rendering.
			glm::vec3 SpatialCoordinates;

			// Length of one side of the node: should be equivalent to (1 / depth) * L, where L is the
			// length of the root nodes side.
			double SideLength;

			util::AABB aabb;
		};

	}

}

#endif // !VULPES_TERRAIN_NODE_H
