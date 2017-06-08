#pragma once
#ifndef VULPES_TERRAIN_HEIGHT_NODE_H
#define VULPES_TERRAIN_HEIGHT_NODE_H

#include "stdafx.h"
#include "TerrainSampler.h"
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

		struct Heightmap {
			Heightmap(const char* filename) {
				Pixels.reserve(2048 * 2048 * 4);
				unsigned width, height;
				lodepng::decode(Pixels, width, height, filename);
				Width = static_cast<uint32_t>(width);
				Height = static_cast<uint32_t>(height);
			}

			uint32_t Width, Height;
			std::vector<unsigned char> Pixels;
		};

		struct HeightSample : public glm::vec2 {
			float& Height() { return this->x; };
			// we use this height to "morph" between LODs.
			// becomes fourth element of terrain vertex.
			float& ParentHeight() { return this->y; };
		};

		class HeightNode {
		public:

			// Used once, for root node.
			HeightNode(const glm::ivec3& node_grid_coordinates);

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

		protected:

			size_t nodeSize = 4;
			size_t gridSize;
			static const size_t rootNodeSize = 4;
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

			// Completes node tasks launched with "LoadNode()", dumps them in nodeCache, returns quantity loaded.
			size_t LoadNodes();

			bool LoadNode(const glm::ivec3& node_pos, const glm::ivec3& parent_pos);
			bool HasNode(const glm::ivec3& node_pos);

			const Heightmap& GetHeightmap() const noexcept;
			uint8_t SampleHeightmap() const;

		protected:
			static std::shared_ptr<HeightNode> CreateNode(const glm::ivec3& pos, const HeightNode& parent);
			Heightmap heightmap;
			std::unordered_map<glm::ivec3, std::shared_ptr<HeightNode>> nodeCache;
			// Nodes in-progress that have been launched with an async task. Futures stored here, 
			std::unordered_map<glm::ivec3, std::future<std::shared_ptr<HeightNode>>> wipNodeCache;
		};


			
	}
}

#endif // !VULPES_HEIGHTMAP_H
