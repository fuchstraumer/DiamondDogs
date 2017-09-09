#pragma once
#ifndef VULPES_TERRAIN_NODE_H
#define VULPES_TERRAIN_NODE_H
#include "stdafx.h"

#include "engine\util\AABB.hpp"
#include "VulpesRender/include/ForwardDecl.hpp"
#include "engine\objects\mesh\PlanarMesh.hpp"
#include "HeightNode.hpp"

namespace vulpes {

	namespace terrain {

		enum class NodeStatus {
			Active,
			NeedsTransfer,
			NeedsUnload,
			Subdivided,
			DataRequested,
		};

		class NodeRenderer;

		class TerrainNode {
			TerrainNode(const TerrainNode& other) = delete;
			TerrainNode& operator=(const TerrainNode& other) = delete;
		public:

			static size_t MaxLOD;
			static float SwitchRatio;

			TerrainNode(const glm::ivec3& parent_coords, const glm::ivec3& logical_coords, const glm::vec3& position, const double& length);

			~TerrainNode();

			void Subdivide();

			// updates this nodes status, and then the node adds itself to the renderer's pool of nodes if its going to be rendered.
			void Update(const glm::vec3 & camera_position, const util::view_frustum& view, NodeRenderer* node_pool);

			// true if all of the Child pointers are nullptr
			bool IsLeaf() const;

			// Recursive method to clean up node tree
			void Prune();

			int LOD_Level() const;

			std::array<std::shared_ptr<TerrainNode>, 4> Children;
			std::unique_ptr<HeightNode> HeightData;

			// used w/ node renderer to decide if this is being rendered and/or if it needs data transferred
			// to the device (so we can batch transfer commands)
			NodeStatus Status;

			void CreateMesh(const Device* dvc);
			void CreateHeightData(const HeightNode* parent_node);

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
			float SideLength;
		private:

			std::future<std::unique_ptr<HeightNode>> heightDataFuture;
			util::AABB aabb;
		};

	}

}

#endif // !VULPES_TERRAIN_NODE_H
