#pragma once
#ifndef VULPES_TERRAIN_NODE_H
#define VULPES_TERRAIN_NODE_H

#include "stdafx.h"
#include "engine\util\AABB.h"

namespace vulpes {

	namespace terrain {

		class TerrainNode {
		private:

			std::array<std::shared_ptr<TerrainNode>, 4> Children;
			std::array<std::shared_ptr<TerrainNode>, 4> Neighbors;
			std::weak_ptr<const TerrainNode> Parent;

			// depth in quadtree: 0 is the root, N is the deepest level etc
			size_t depth;
			
			/*
				The logical coordinates of a node specify its position in the quadtree "grid". 
				A node of depth 3 and in the upper-right corner has logical coordinates of 
				(3, 3) - the magnitude of the x/y coordinates is found as 3^depth - 1.
			*/
			glm::ivec2 logicalCoordinates;

			/*
				Spatial coordinates of a node are given relative to the root node, but these are
				used during rendering to place the node at an appropriate position in world-space.
			*/
			glm::vec3 spatialCoordinates;

			// Length of one side of the node: should be equivalent to (1 / depth) * L, where L is the
			// length of the root nodes side.
			double length;

			util::AABB aabb;

			TerrainNode(const TerrainNode& other) = delete;
			TerrainNode& operator=(const TerrainNode& other) = delete;

			// Creates children.
			void subdivide(); 

		public:

			TerrainNode(const TerrainNode* parent, glm::ivec2 logical_coords, glm::vec3 spatial_coords, double _length);

			// true if all of the Child pointers are nullptr
			bool Leaf() const noexcept;

			// Returns number of nodes below this one in the heirarchy
			size_t NumBelow() const noexcept;
			

			void Update();



		};

	}

}

#endif // !VULPES_TERRAIN_NODE_H
