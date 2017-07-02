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

		static inline void save_hm_to_file(const std::vector<float>& vals, const float& min, const float& max, const char* filename, unsigned width, unsigned height) {

			auto normalize = [&min, &max](const float& val) {
				return (val - min) / (max - min);
			};

			std::vector<float> normalized;
			normalized.resize(vals.size());

			for (size_t j = 0; j < height; ++j) {
				for (size_t i = 0; i < width; ++i) {
					normalized[i + (j * width)] = normalize(vals[i + (j * width)]);
				}
			}

			auto make_pixel = [](const float& val)->unsigned char {
				return static_cast<unsigned char>(val * 255.0f);
			};

			std::vector<unsigned char> pixels(width * height);

			for (size_t j = 0; j < height; ++j) {
				for (size_t i = 0; i < width; ++i) {
					pixels[i + (j * width)] = make_pixel(normalized[i + (j * width)]);
				}
			}


			unsigned error = lodepng::encode(filename, pixels, width, height, LodePNGColorType::LCT_GREY, 8);
			if (error) {
				std::cerr << lodepng_error_text(error) << std::endl;
				throw;
			}
		}

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

			// Used for root node 
			HeightNode(const glm::ivec3& node_grid_coordinates, std::vector<HeightSample> init_samples);

			// Used for most nodes.
			HeightNode(const glm::ivec3& node_grid_coordinates, const HeightNode& parent, const bool& sample_now = false);

			~HeightNode() = default;

			void SampleFromResiduals(glm::ivec3& node_coords, const Sampler& sampler);
			
			void SampleFromParent(const HeightNode& node);

			void SetSamples(const std::vector<HeightSample> &samples);
			void SetSamples(std::vector<HeightSample>&& samples);

			float GetHeight(const glm::vec2 world_pos) const;
			glm::vec3 GetNormal(const glm::vec2 world_pos) const;

			float Sample(const size_t& x, const size_t& y) const;
			float Sample(const size_t& idx) const;

			const glm::ivec3& GridCoords() const noexcept;
			size_t MeshGridSize() const noexcept;
			size_t SampleGridSize() const noexcept;
			size_t NumSamples() const noexcept;
			std::vector<glm::vec2> GetHeights() const;

			static void SetRootNodeSize(const size_t& new_size);
			static void SetRootNodeLength(const double& new_length);

			static size_t RootSampleGridSize;
			static double RootNodeLength;

			std::vector<HeightSample> samples;
			float MinZ, MaxZ;
			const HeightNode* Parent;
		protected:

			size_t sampleGridSize = 16;
			size_t meshGridSize;
			
			glm::ivec3 gridCoords;
			glm::ivec3 parentGridCoords;
			
		};

	}
}

#endif // !VULPES_HEIGHTMAP_H
