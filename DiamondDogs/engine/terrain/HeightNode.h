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
				pixels.reserve(2048 * 2048);
				unsigned width, height;
				lodepng::decode(pixels, width, height, filename);
			}

			std::vector<unsigned char> pixels;
		};

		struct HeightSample : public glm::vec2 {
			float& Height() { return this->x; };
			// we use this height to "morph" between LODs.
			// becomes fourth element of terrain vertex.
			float& ParentHeight() { return this->y; };
		};

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
			static const size_t rootNodeSize = 4;
			glm::ivec3 gridCoords;
			std::vector<HeightSample> samples;
		};


		class HeightmapLoader {

		};


			
	}
}

#endif // !VULPES_HEIGHTMAP_H
