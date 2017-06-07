#pragma once
#ifndef VULPES_TERRAIN_HEIGHT_NODE_H
#define VULPES_TERRAIN_HEIGHT_NODE_H

#include "stdafx.h"
#include "TerrainSampler.h"
namespace vulpes {

	/*
	
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

		class HeightNode {
		public:

			HeightNode(const glm::ivec3& node_grid_coordinates);

			~HeightNode() = default;

			void SampleResiduals(const Sampler& sampler);
			
			void SampleFromParent(const HeightNode& node);

			float Sample(const size_t& x, const size_t& y) const;
			float Sample(const size_t& idx) const;
			
			template<typename pixel_type>
			void SampleHeightmap(const std::vector<pixel_type>& pixels);

		protected:
			// max error in sampled data.
			float maxError;
			size_t nodeSize = 4;
			size_t gridSize;
			static size_t rootNodeSize;
			glm::ivec3 gridCoords;
			std::vector<float> residualSamples;
			std::vector<float> finalSamples;
		};

		size_t HeightNode::rootNodeSize = 4;

		class HeightmapLoader {
			template<typename pixel_type>
			static Heightmap<pixel_type>* CreateHeightmap(const char* filename);
		};


			
	}
}

#endif // !VULPES_HEIGHTMAP_H
