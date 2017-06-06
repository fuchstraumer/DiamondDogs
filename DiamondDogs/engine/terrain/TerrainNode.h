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

		class NodeRenderer;

		class TerrainNode {
			TerrainNode(const TerrainNode& other) = delete;
			TerrainNode& operator=(const TerrainNode& other) = delete;
		public:

			TerrainNode(const glm::ivec3& logical_coords, const glm::vec3& position, const double& length, const size_t& max_lod, const double& switch_ratio);

			~TerrainNode();

			void Subdivide();
			// true if all of the Child pointers are nullptr
			bool Leaf() const;
			// Recursive method to clean up node tree
			void Prune();

			int Depth() const;

			std::array<std::shared_ptr<TerrainNode>, 4> Children;
			NodeStatus Status;


	
			// Coordinates of this node in the grid defining the quadtree
			glm::ivec3 GridCoordinates;

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
