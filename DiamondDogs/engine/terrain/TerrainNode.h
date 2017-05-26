#pragma once
#ifndef VULPES_TERRAIN_NODE_H
#define VULPES_TERRAIN_NODE_H

#include "stdafx.h"
#include "engine\util\AABB.h"
#include "engine\renderer\ForwardDecl.h"
#include "engine\objects\Mesh.h"
namespace vulpes {

	namespace terrain {

		enum class CubemapFace {
			FRONT,
			BACK,
			LEFT,
			RIGHT,
			TOP,
			BOTTOM,
		};

		static constexpr size_t NodeDimensionOrder = 6;
		static constexpr size_t NodeDimension = 1 << NodeDimensionOrder;
		static constexpr double MinSwitchDistance = 2.0 * static_cast<double>(NodeDimension);


		using height_sampler = std::function<float(glm::vec3&)>;

		class TerrainNode {
		public:
		
			enum class node_status {
				Undefined, // Likely not constructed fully or used at all
				OutOfFrustum,
				OutOfRange,
				Active, // Being used in next renderpass
			};

			std::array<std::shared_ptr<TerrainNode>, 4> children;
			std::array<bool, 4> neighbors;
			const TerrainNode* parent;

			// depth in quadtree: 0 is the root, N is the deepest level etc
			size_t Depth;
			
			/*
				The logical coordinates of a node specify its position (this coordinate == lower left corner)
				in the quadtree "grid". 

				A node of depth 3 and in the upper-right corner has logical coordinates of 
				(3, 3) - the magnitude of the x/y coordinates is found as 3^depth - 1.
			*/
			glm::vec2 LogicalCoordinates;

			/*
				Spatial coordinates of a node are given relative to the root node, but these are
				used during rendering to place the node at an appropriate position in world-space.
			*/
			glm::vec3 SpatialCoordinates;

			// Length of one side of the node: should be equivalent to (1 / depth) * L, where L is the
			// length of the root nodes side.
			double SideLength;

			util::AABB aabb;

			TerrainNode(const TerrainNode& other) = delete;
			TerrainNode& operator=(const TerrainNode& other) = delete;

			// Creates children.
			void CreateChild(const size_t& idx);
			
			Mesh mesh;

			TerrainNode(const TerrainNode* parent, glm::vec2 logical_coords, double _length, const CubemapFace& face);

			void CreateMesh();

			// true if all of the Child pointers are nullptr
			bool Leaf() const noexcept;

			void Update();

			void Render(VkCommandBuffer& cmd);

			// Recursive method to clean up node tree
			void Prune();

			double Size();

			node_status Status;

			// used to join edges.
			CubemapFace Face;

			void CalculateExtrema();

		};

	}

}

#endif // !VULPES_TERRAIN_NODE_H
