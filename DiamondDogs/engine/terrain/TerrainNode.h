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

		static constexpr double MaxRenderDistance = 3000.0;

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

		class NodeSubset;

		enum class CubemapFace {
			FRONT,
			BACK,
			LEFT,
			RIGHT,
			TOP,
			BOTTOM,
		};

		static constexpr size_t MaxLOD = 10;


		using height_sampler = std::function<float(glm::vec3&)>;

		class TerrainNode {
		public:
		
			enum class node_status {
				Undefined, // Likely not constructed fully or used at all
				OutOfFrustum,
				OutOfRange,
				Active, // Being used in next renderpass
				Subdivided, // Has been subdivided, render children instead of this.
			};

			std::array<std::unique_ptr<TerrainNode>, 4> children;
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

			TerrainNode(const TerrainNode& other) = delete;
			TerrainNode& operator=(const TerrainNode& other) = delete;

			// Creates children.
			void Subdivide();
			
			Mesh mesh;

			TerrainNode(const TerrainNode* parent, glm::ivec2 logical_coords, const glm::vec3& position, double _length, const CubemapFace& face);

			bool operator<(const TerrainNode& other) const;

			void CreateMesh(const Device* render_device, CommandPool* cmd_pool, const VkQueue& queue);

			// true if all of the Child pointers are nullptr
			bool Leaf() const noexcept;

			void Update(const glm::dvec3 & camera_position, NodeSubset* active_nodes, const util::view_frustum& view);

			void Render(VkCommandBuffer& cmd) const;

			// Recursive method to clean up node tree
			void Prune();

			double Size();

			node_status Status = node_status::Undefined;

			// used to join edges.
			CubemapFace Face;

			void CalculateExtrema();

		};

	}

}

#endif // !VULPES_TERRAIN_NODE_H
