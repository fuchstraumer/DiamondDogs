#pragma once
#ifndef VULPES_TERRAIN_NODE_H
#define VULPES_TERRAIN_NODE_H

#include "stdafx.h"
#include "engine\util\AABB.h"
#include "engine\util\view_frustum.h"
#include "engine\renderer\ForwardDecl.h"
#include "engine\objects\Mesh.h"
namespace vulpes {

	namespace terrain {

		class NodeSubset;

		class TerrainNode {
			TerrainNode(const TerrainNode& other) = delete;
			TerrainNode& operator=(const TerrainNode& other) = delete;
		public:

			TerrainNode(const Device* device, const size_t& depth, const glm::ivec2& logical_coords, const glm::vec3& position, const double& length, const size_t& max_lod, const double& switch_ratio);

			~TerrainNode();

			void Update(const glm::vec3 & camera_position, NodeSubset* active_nodes, const util::view_frustum& view);

			// Create creates CPU-side data, and RecordTransferCmd prepares this for transfer to the GPU.
			// These are separated as individual transfer would be expensive, and batching can really help performance.
			void CreateMesh();
			void RecordTransferCmd(VkCommandBuffer& cmd);
			void RecordGraphicsCmds(VkCommandBuffer& cmd) const;

			void Subdivide();
			// true if all of the Child pointers are nullptr
			bool Leaf() const;
			// Recursive method to clean up node tree
			void Prune();


			std::array<std::unique_ptr<TerrainNode>, 4> Children;
			NodeStatus Status;

			// depth in quadtree: 0 is the root, N is the deepest level etc
			size_t Depth;
			size_t MaxLOD;
	
			// Coordinates of this node in the grid defining the quadtree
			glm::ivec2 GridCoordinates;

			// world-relative spatial coordinates. 
			// TODO: Investigate root-node relative, for the sake of large-scale rendering.
			glm::vec3 SpatialCoordinates;

			// Length of one side of the node: should be equivalent to (1 / depth) * L, where L is the
			// length of the root nodes side.
			double SideLength;
			double SwitchRatio;

			util::AABB aabb;
			Mesh mesh;

			const Device* device;

			static float MaxRenderDistance;
			static bool DrawAABB;
			// Textures should be shared between all nodes, UV coords easily found with grid coords.
			static gli::texture2d heightmap, normalmap;

		};

	}

}

#endif // !VULPES_TERRAIN_NODE_H
