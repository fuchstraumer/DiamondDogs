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

		

		template<typename uint8_t>
		struct HeightmapPNG {

			HeightmapPNG() = default;

			HeightmapPNG(const char* filename);

			uint32_t Width, Height;
			std::vector<uint8_t> Pixels;
		};

		struct HeightSample {

			HeightSample() {}

			bool operator<(const HeightSample& other) const;
			bool operator>(const HeightSample& other) const;
			bool operator==(const HeightSample& other) const;

			glm::vec3 Sample, Normal;
		};

		struct HeightmapNoise {

			HeightmapNoise(const size_t& num_samples, const glm::vec3&  starting_pos, const float& step_size);

			std::vector<HeightSample> samples;

			
		};

		class HeightNode {
		public:

			// Used once, for root node.
			HeightNode(const glm::ivec3& node_grid_coordinates, std::vector<HeightSample>& init_samples);

			// Used for most nodes.
			HeightNode(const glm::ivec3& node_grid_coordinates, const HeightNode& parent);

			~HeightNode() = default;

			void SampleFromResiduals(glm::ivec3& node_coords, const Sampler& sampler);
			
			void SampleFromParent(const HeightNode& node);

			float GetHeight(const glm::vec2 world_pos) const;
			glm::vec3 GetNormal(const glm::vec2 world_pos) const;

			float Sample(const size_t& x, const size_t& y) const;
			float Sample(const size_t& idx) const;

			const glm::ivec3& GridCoords() const noexcept;
			size_t GridSize() const noexcept;

			static void SetRootNodeSize(const size_t& new_size);
			static void SetRootNodeLength(const double& new_length);

			static size_t RootNodeSize;
			static double RootNodeLength;

			float MinZ, MaxZ;
		protected:

			size_t nodeSize = 32;
			size_t gridSize;
			
			glm::ivec3 gridCoords;
			glm::ivec3 parentGridCoords;
			std::vector<HeightSample> samples;
		};

	}
}

#endif // !VULPES_HEIGHTMAP_H
