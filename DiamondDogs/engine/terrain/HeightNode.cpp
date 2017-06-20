#include "stdafx.h"
#include "HeightNode.h"
#include "engine\util\FastNoise.h"
#include "engine/util/lodepng.h"
#include "glm/ext.hpp"
#include "engine/util/Noise.h"


namespace vulpes {
	namespace terrain {

		size_t HeightNode::RootSampleGridSize = 261;
		double HeightNode::RootNodeLength = 10000;

		HeightNode::HeightNode(const glm::ivec3 & node_grid_coordinates, std::vector<HeightSample>& init_samples) : gridCoords(node_grid_coordinates), sampleGridSize(RootSampleGridSize), meshGridSize(RootSampleGridSize - 5) {
			samples = std::move(init_samples);
			auto min_max = std::minmax_element(samples.cbegin(), samples.cend());
			MinZ = samples.at(min_max.first - samples.cbegin()).Sample.x;
			MaxZ = samples.at(min_max.second - samples.cbegin()).Sample.x;
		}

		HeightNode::HeightNode(const glm::ivec3 & node_grid_coordinates, const HeightNode & parent, const bool& sample_now) : gridCoords(node_grid_coordinates), parentGridCoords(parent.GridCoords()), sampleGridSize(37) {
			if (sample_now) {
				SampleFromParent(parent);
			}
		}

		static constexpr bool save_to_file = false;

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
			}
		}
		
		void HeightNode::SampleFromParent(const HeightNode & node) {
			// See: proland/preprocess/terrain/HeightMipmap.cpp to find original implementation
			size_t tile_size = std::min(RootSampleGridSize << gridCoords.z, sampleGridSize);

			// Tile size is the number of vertices we directly use: we add 5 as we sample the borders,
			// which helps with the upsampling process.
			meshGridSize = tile_size - 5;

			// These aren't the parent grid coords: these are the offsets from the parent height samples that we 
			// use to sample from the parents data.
			int parent_x, parent_y;
			parent_x = 1 + (gridCoords.x % 2) * meshGridSize / 2;
			parent_y = 1 + (gridCoords.y % 2) * meshGridSize / 2;

			samples.resize(sampleGridSize * sampleGridSize);

			/*
				Upsampling: Here we check the current i,j pair representing our local grid coordinates.
				Based on where we are on the grid, we either directly sample or perform various interpolations/
				mixings to weight the sample from the parent. This avoids obvious borders between regions
				and lets one heightmap image continue to work for smaller and smaller tiles.

				We change the residual magnitude and functions based on the LOD level of this tile, though, 
				as we will still be losing some detail at high LOD levels.
			*/

			for (size_t j = 0; j < sampleGridSize; ++j) {
				for (size_t i = 0; i < sampleGridSize; ++i) {
					float sample;
					if (j % 2 == 0) {
						if (i % 2 == 0) {
							sample = node.Sample(i / 2 + parent_x + (j / 2 + parent_y) * sampleGridSize);
						}
						else {
							float z0, z1, z2, z3;
							z0 = node.Sample(i / 2 + parent_x - 1 + (j / 2 + parent_y) * sampleGridSize);
							z1 = node.Sample(i / 2 + parent_x + (j / 2 + parent_y) * sampleGridSize);
							z2 = node.Sample(i / 2 + parent_x + 1 + (j / 2 + parent_y) * sampleGridSize);
							z3 = node.Sample(i / 2 + parent_x + 2 + (j / 2 + parent_y) * sampleGridSize);
							sample = ((z1 + z2) * 9.0f - (z0 + z3)) / 16.0f;
						}
					}
					else {
						if (i % 2 == 0) {
							float z0, z1, z2, z3;
							z0 = node.Sample(i / 2 + parent_x + (j / 2 - 1 + parent_y)*sampleGridSize);
							z1 = node.Sample(i / 2 + parent_x + (j / 2 + parent_y)*sampleGridSize);
							z2 = node.Sample(i / 2 + parent_x + (j / 2 + 1 + parent_y)*sampleGridSize);
							z3 = node.Sample(i / 2 + parent_x + (j / 2 + 2 + parent_y)*sampleGridSize);
							sample = ((z1 + z2) * 9.0f - (z0 + z3)) / 16.0f;
						}
						else {
							int dI, dJ;
							sample = 0.0f;
							for (dJ = -1; dJ <= 2; ++dJ) {
								float f = (dJ == -1 || dJ == 2) ? (-1.0f / 16.0f) : (9.0f / 16.0f);
								for (dI = -1; dI <= 2; ++dI) {
									float g = (dI == -1 || dI == 2) ? (-1.0f / 16.0f) : (9.0f / 16.0f);
									sample += f * g * node.Sample(i / 2 + dI + parent_x + (j / 2 + dJ + parent_y)*sampleGridSize);
								}
							}
						}
					}

					samples[i + (j * sampleGridSize)].Sample.x = std::move(sample);
					// Parent height is used to morph between LOD levels, so that we don't notice much pop-in as new mesh tiles are loaded.
					samples[i + (j * sampleGridSize)].Sample.y = node.Sample(i / 2 + parent_x + (j / 2 + parent_y)*sampleGridSize);
				}
			}
			auto min_max = std::minmax_element(samples.cbegin(), samples.cend());
			float min_z, max_z;
			MinZ = min_z = samples.at(min_max.first - samples.cbegin()).Sample.x;
			MaxZ = max_z = samples.at(min_max.second - samples.cbegin()).Sample.x;

		}

		float HeightNode::GetHeight(const glm::vec2 world_pos) const {

			double curr_size = RootNodeLength / (1 << gridCoords.z);
			double s = curr_size / 2.0;
			// Make sure query is in range of current node.
			//if (abs(world_pos.x) >= curr_size + 1.0 || abs(world_pos.y) >= curr_size + 1.0) {
			//	throw std::out_of_range("Attempted to sample out of range of heightnode");
			//}
			auto curr_grid_size = GridSize();
			float x, y;
			x = world_pos.x;
			y = world_pos.y;

			if (x == curr_size) {
				x = sampleGridSize - 3.0f;
			}
			else {
				x = 2.0f + (fmod(x, curr_size) / curr_size) * curr_grid_size;
			}
			if (y == curr_size) {
				y = sampleGridSize - 3.0f;
			}
			else {
				y = 2.0f + (fmod(y, curr_size) / curr_size) * curr_grid_size;
			}

			//x = 2.0f + (fmod(x, curr_size) / curr_size) * curr_grid_size;
			//y = 2.0f + (fmod(y, curr_size) / curr_size) * curr_grid_size;
			size_t sx = floorf(x), sy = floorf(y);

			return Sample(sx, sy);
		}

		glm::vec3 HeightNode::GetNormal(const glm::vec2 world_pos) const {
			double curr_size = RootNodeLength / (1 << gridCoords.z);

			// Make sure query is in range of current node.
			if (abs(world_pos.x) >= curr_size + 1.0 || abs(world_pos.y) >= curr_size + 1.0) {
				throw std::out_of_range("Attempted to sample out of range of heightnode");
			}

			float x, y;
			x = world_pos.x;
			y = world_pos.y;

			size_t curr_grid_size = GridSize();

			x = 2.0f + (fmod(x, curr_size) / curr_size) * curr_grid_size;
			y = 2.0f + (fmod(y, curr_size) / curr_size) * curr_grid_size;

			// Sample coords
			size_t sx = floorf(x), sy = floorf(y);

			return glm::normalize(samples[sx + (sy * sampleGridSize)].Normal);
		}

		float HeightNode::Sample(const size_t & x, const size_t & y) const{
			return samples[x + (y * sampleGridSize)].Sample.x;
		}

		float HeightNode::Sample(const size_t & idx) const{
			return samples[idx].Sample.x;
		}

		static inline std::vector<HeightSample> MakeCheckerboard(const size_t width, const size_t height) {
			std::vector<HeightSample> results(width * height);
			for (size_t j = 0; j < height; ++j) {
				for (size_t i = 0; i < width; ++i) {
					results[i + (j * width)].Sample.x = (i & 1 ^ j & 1) ? -30.0f : 30.0f;
				}
			}
			return results;
		}

		HeightmapNoise::HeightmapNoise(const size_t & num_samples, const glm::vec3& starting_pos, const float& step_size) {
			samples.resize(num_samples * num_samples);
			glm::vec2 xy = starting_pos.xz;
			//samples = MakeCheckerboard(num_samples, num_samples);
			for (size_t j = 0; j < num_samples; ++j) {
				for (size_t i = 0; i < num_samples; ++i) {
					samples[i + (j * num_samples)].Sample.x = 25.0f * SNoise::FBM(glm::vec2(xy.x + (i * step_size), xy.y + (j * step_size)), 45867, 1.9, 12, 2.2f, 1.8f);
				}
			}
		}

		const glm::ivec3 & HeightNode::GridCoords() const noexcept{
			return gridCoords;
		}

		size_t HeightNode::GridSize() const noexcept {
			return meshGridSize;
		}

		size_t HeightNode::NumSamples() const noexcept {
			return sampleGridSize * sampleGridSize;
		}

		std::vector<float> HeightNode::GetHeights() const {
			std::vector<float> results(sampleGridSize * sampleGridSize);
			for (size_t j = 0; j < sampleGridSize; ++j) {
				for (size_t i = 0; i < sampleGridSize; ++i) {
					results[i + (j * sampleGridSize)] = samples[i + (j * sampleGridSize)].Sample.x;
				}
			}
			return results;
		}

		void HeightNode::SetRootNodeSize(const size_t & new_size) {
			RootSampleGridSize = new_size;
		}

		void HeightNode::SetRootNodeLength(const double & new_length) {
			RootNodeLength = new_length;
		}

		bool HeightSample::operator<(const HeightSample & other) const {
			return Sample.x < other.Sample.x;
		}

		bool HeightSample::operator>(const HeightSample & other) const {
			return Sample.x > other.Sample.x;
		}

		bool HeightSample::operator==(const HeightSample & other) const {
			return Sample == other.Sample;
		}

}
}