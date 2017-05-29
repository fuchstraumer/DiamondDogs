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

		static const std::array<glm::vec3, 8> aabb_vertices{
			glm::vec3{-1.0f,-1.0f, 1.0f},
			glm::vec3{ 1.0f,-1.0f, 1.0f},
			glm::vec3{ 1.0f, 1.0f, 1.0f},
			glm::vec3{-1.0f, 1.0f, 1.0f},
			glm::vec3{-1.0f,-1.0f,-1.0f},
			glm::vec3{ 1.0f,-1.0f,-1.0f},
			glm::vec3{ 1.0f, 1.0f,-1.0f},
			glm::vec3{-1.0f, 1.0f,-1.0f},
		};

		static constexpr std::array<uint32_t, 36> aabb_indices{
			0,1,2, 2,3,0, 
			1,5,6, 6,2,1, 
			7,6,5, 5,4,7, 
			4,0,3, 3,7,4, 
			4,5,1, 1,0,4, 
			3,2,6, 6,7,3,
		};

		static constexpr size_t MemoryResidencyTime = 10e6;

		class NodeSubset;

		class TerrainNode {
		public:

			static bool DrawAABB;

			TerrainNode(const Device* device, const size_t& depth, const glm::ivec2& logical_coords, const glm::vec3& position, const double& length, const size_t& max_lod, const double& switch_ratio);

			~TerrainNode();

			void CreateMesh();

			void TransferMesh(VkCommandBuffer& cmd);

			TerrainNode(const TerrainNode& other) = delete;
			TerrainNode& operator=(const TerrainNode& other) = delete;

			// Creates children.
			void Subdivide();

			void InitChild(size_t i);

			// true if all of the Child pointers are nullptr
			bool Leaf() const noexcept;

			void Update(const glm::vec3 & camera_position, NodeSubset* active_nodes, const util::view_frustum& view);

			void BuildCommandBuffer(VkCommandBuffer& cmd) const;

			// Recursive method to clean up node tree
			void Prune();

			double Size();

			std::array<std::unique_ptr<TerrainNode>, 4> children;
			NodeStatus Status;
			// depth in quadtree: 0 is the root, N is the deepest level etc
			size_t Depth;
			size_t MaxLOD;
			/*
				The logical coordinates of a node specify its position (this coordinate == lower left corner)
				in the quadtree "grid". 

				A node of depth 3 and in the upper-right corner has logical coordinates of 
				(3, 3) - the magnitude of the x/y coordinates is found as 3^depth - 1.
			*/
			glm::ivec2 LogicalCoordinates;

			/*
				Spatial coordinates of a node are given relative to the root node, but these are
				used during rendering to place the node at an appropriate position in world-space.
			*/
			glm::vec3 SpatialCoordinates;

			// Length of one side of the node: should be equivalent to (1 / depth) * L, where L is the
			// length of the root nodes side.
			double SideLength;

			util::AABB aabb;
			
			Mesh mesh;

			Mesh aabb_mesh;

			double switchRatio;

			const float MaxRenderDistance = 9000.0f;

			const Device* device;

		};

	}

}

#endif // !VULPES_TERRAIN_NODE_H
