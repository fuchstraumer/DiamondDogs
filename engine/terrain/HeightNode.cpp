#include "stdafx.h"
#include "HeightNode.h"
#include "glm/ext.hpp"
#include "common/CommonUtil.h"
#include "engine/util/Noise.h"


namespace vulpes {
	namespace terrain {

		size_t HeightNode::RootSampleGridSize = 16;
		double HeightNode::RootNodeLength = 10000;

		HeightNode::HeightNode(const glm::ivec3 & node_grid_coordinates, std::vector<HeightSample> init_samples) : gridCoords(node_grid_coordinates), sampleGridSize(RootSampleGridSize), meshGridSize(RootSampleGridSize - 5), samples(std::move(init_samples)) {
			auto min_max = std::minmax_element(samples.cbegin(), samples.cend());
			MinZ = samples.at(min_max.first - samples.cbegin()).Sample.x;
			MaxZ = samples.at(min_max.second - samples.cbegin()).Sample.x;
		}

		HeightNode::HeightNode(const glm::ivec3 & node_grid_coordinates, const HeightNode & parent, const bool& sample_now) : gridCoords(node_grid_coordinates),  meshGridSize(sampleGridSize - 5), Parent(&parent) {
			if (sample_now) {
				SampleFromParent(parent);
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
			parent_x = 1 + static_cast<int>((gridCoords.x % 2) * meshGridSize / 2);
			parent_y = 1 + static_cast<int>((gridCoords.y % 2) * meshGridSize / 2);

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

		void HeightNode::SetSamples(std::vector<HeightSample>&& _samples) {
			samples = std::move(_samples);
		}

		float HeightNode::GetHeight(const glm::vec2 world_pos) const {

			double curr_size = RootNodeLength / (1 << gridCoords.z);
			auto curr_grid_size = MeshGridSize();
			double x, y;
			x = static_cast<double>(world_pos.x);
			y = static_cast<double>(world_pos.y);

			if (x == curr_size) {
				x = static_cast<double>(sampleGridSize - 3.0);
			}
			else {
				x = 2.0 + (fmod(x, curr_size) / curr_size) * curr_grid_size;
			}
			if (y == curr_size) {
				y = static_cast<double>(sampleGridSize - 3.0);
			}
			else {
				y = 2.0 + (fmod(y, curr_size) / curr_size) * curr_grid_size;
			}

			size_t sx = static_cast<size_t>(floor(x)), sy = static_cast<size_t>(floor(y));

			return Sample(sx, sy);
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

		const HeightSample & HeightNode::operator[](const size_t & idx) const {
			return samples[idx];
		}

		HeightSample & HeightNode::operator[](const size_t & idx) {
			return samples[idx];
		}

		const glm::ivec3 & HeightNode::GridCoords() const noexcept{
			return gridCoords;
		}

		size_t HeightNode::MeshGridSize() const noexcept {
			return meshGridSize;
		}

		size_t HeightNode::SampleGridSize() const noexcept {
			return sampleGridSize;
		}

		size_t HeightNode::NumSamples() const noexcept {
			return sampleGridSize * sampleGridSize;
		}

		std::vector<glm::vec2> HeightNode::GetHeights() const {
			std::vector<glm::vec2> results(sampleGridSize * sampleGridSize);
			for (size_t j = 0; j < sampleGridSize; ++j) {
				for (size_t i = 0; i < sampleGridSize; ++i) {
					results[i + (j * sampleGridSize)] = samples[i + (j * sampleGridSize)].Sample.xy;
				}
			}
			return results;
		}

		std::vector<HeightSample> HeightNode::GetSamples() const {
			return samples;
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