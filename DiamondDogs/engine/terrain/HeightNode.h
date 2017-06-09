#pragma once
#ifndef VULPES_TERRAIN_HEIGHT_NODE_H
#define VULPES_TERRAIN_HEIGHT_NODE_H

#include "stdafx.h"
#include "TerrainSampler.h"
#include "engine\util\noise.h"

namespace vulpes {

	/*
			
		Effective upsampling filter applied when sampling from parent
	
		 1 -9 -9  1		   -1		  1 -9 -9  1
		-9 81 81 -9			9		 -9 81 81 -9
		-9 81 81 -9			9		 -9 81 81 -9
		 1 -9 -9  1/256	  -1/16	  1/256 -9 -9  1

	   -1   9  9  -1/16		x	  -1/16  9  9 -1

	     1 -9 -9  1/256	  -1/16   1/256 -9 -9  1
		-9 81 81 -9			9        -9 81 81 -9
		-9 81 81 -9			9		 -9 81 81 -9
		 1 -9 -9  1		   -1		  1 -9 -9  1
	
	*/

	namespace terrain {

		struct HeightmapNoise {

			HeightmapNoise(const size_t& num_samples, const glm::vec3&  starting_pos, const float& step_size);

			std::vector<float> samples;
		};

		template<typename uint8_t>
		struct HeightmapPNG {

			HeightmapPNG() = default;

			HeightmapPNG(const char* filename);

			uint32_t Width, Height;
			std::vector<uint8_t> Pixels;
		};

		struct HeightSample : public glm::vec2 {

			HeightSample() : glm::vec2() {}

			HeightSample(float x, float y) : glm::vec2(std::move(x), std::move(y)) {}

			float& Height() { return this->x; };
			// we use this height to "morph" between LODs.
			// becomes fourth element of terrain vertex.
			float& ParentHeight() { return this->y; };
		};

		class HeightNode {
		public:

			// Used once, for root node.
			HeightNode(const glm::ivec3& node_grid_coordinates, std::vector<float>& init_samples);

			// Used for most nodes.
			HeightNode(const glm::ivec3& node_grid_coordinates, const HeightNode& parent);

			~HeightNode() = default;

			void SampleResiduals(const Sampler& sampler);
			
			void SampleFromParent(const HeightNode& node);

			float Sample(const size_t& x, const size_t& y) const;
			float Sample(const size_t& idx) const;

			// Used once, with root node
			template<typename pixel_type>
			void SampleHeightmap(const std::vector<pixel_type>& pixels);

			const glm::ivec3& GridCoords() const noexcept;
			size_t GridSize() const noexcept;

			static const size_t RootNodeSize = 48;
		protected:

			size_t nodeSize = 53;
			size_t gridSize;
			
			glm::ivec3 gridCoords;
			glm::ivec3 parentGridCoords;
			std::vector<HeightSample> samples;
		};


		/*
		
			HeightNodeLoader: used by the node renderer class to load/create height
			nodes as needed. Caches nodes in an unordered map that stores height nodes
			based on the grid position, which cna be used to check for nodes before 
			just creating them.
			
		*/

		class HeightNodeLoader {
		public:

			HeightNodeLoader(const double& root_node_length, std::shared_ptr<HeightNode> root_node) : RootNodeLength(root_node_length) {
				nodeCache.insert(std::make_pair(root_node->GridCoords(), std::move(root_node)));
			}

			// Completes node tasks launched with "LoadNode()", dumps them in nodeCache, returns quantity loaded.
			size_t LoadNodes();

			bool SubdivideNode(const glm::ivec3& node_pos);
			bool LoadNode(const glm::ivec3& node_pos, const glm::ivec3& parent_pos);
			bool HasNode(const glm::ivec3& node_pos);

			float GetHeight(const size_t& lod_level, const glm::vec2 world_pos);

			//const Heightmap& GetHeightmap() const noexcept;
			// uint8_t SampleHeightmap() const;

			double RootNodeLength;

		protected:
			static std::shared_ptr<HeightNode> CreateNode(const glm::ivec3& pos, const HeightNode& parent);
			//Heightmap heightmap;
			std::unordered_map<glm::ivec3, std::shared_ptr<HeightNode>> nodeCache;
			// Nodes in-progress that have been launched with an async task. Futures stored here, 
			std::unordered_map<glm::ivec3, std::future<std::shared_ptr<HeightNode>>> wipNodeCache;
		};


			
		
	}
}

#endif // !VULPES_HEIGHTMAP_H
