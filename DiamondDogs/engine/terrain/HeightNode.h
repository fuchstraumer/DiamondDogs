#pragma once
#ifndef VULPES_TERRAIN_HEIGHT_NODE_H
#define VULPES_TERRAIN_HEIGHT_NODE_H

#include "stdafx.h"
#include "engine\util\noise.h"

namespace vulpes {

	namespace terrain {

		struct HeightSample {

			HeightSample() {}

			bool operator<(const HeightSample& other) const;
			bool operator>(const HeightSample& other) const;
			bool operator==(const HeightSample& other) const;

			glm::vec3 Sample, Normal;
		};

		static inline std::vector<HeightSample> GetNoiseHeightmap(const size_t & num_samples, const glm::vec3 & starting_location, const float & noise_step_size) {
			std::vector<HeightSample> samples;
			samples.resize(num_samples * num_samples);
			glm::vec3 init_pos = starting_location;
			NoiseGen ng;

			for (size_t j = 0; j < num_samples; ++j) {
				for (size_t i = 0; i < num_samples; ++i) {
					glm::vec3 npos(init_pos.x + i * noise_step_size, init_pos.y + j * noise_step_size, init_pos.z);
					// doing ridged-multi fractal bit here, as it broke elsewhere.
					// TODO: Figure out why this method fails in the noise generator class: but not when done right here.
					float ampl = 1.0f;
					for (size_t k = 0; k < 12; ++k) {
						samples[i + (j * num_samples)].Sample.x += 1.0f - fabsf(ng.sdnoise3(npos.x, npos.y, npos.z, nullptr) * ampl);
						npos *= 2.10f;
						ampl *= 0.80f;
					}
				}
			}

			return samples;
		}

		// HeightTile represents the height data itself: it also facilitates access
		// to it, and retrieving simple information about the height data contained
		struct HeightTile {

			float SampleAt(const glm::ivec2& local_grid_pos) const;
			float GetHeight(const glm::vec2& world_pos) const;

			size_t TileSize;
			std::vector<HeightSample> Samples;
			float MinHeight, MaxHeight;


		};

		class HeightNode {
		public:

			// Used for root node 
			HeightNode(const glm::ivec3& node_grid_coordinates, std::vector<HeightSample> init_samples);

			// Used for most nodes.
			HeightNode(const glm::ivec3& node_grid_coordinates, const HeightNode& parent, const bool& sample_now = false);

			~HeightNode() = default;
			
			void SampleFromParent(const HeightNode& node);

			void SetSamples(std::vector<HeightSample>&& samples);

			float GetHeight(const glm::vec2 world_pos) const;

			float Sample(const size_t& x, const size_t& y) const;
			float Sample(const size_t& idx) const;

			const HeightSample& operator[](const size_t& idx) const;
			HeightSample& operator[](const size_t& idx);

			const glm::ivec3& GridCoords() const noexcept;
			const glm::ivec3& ParentGridCoords() const noexcept;

			size_t MeshGridSize() const noexcept;
			size_t SampleGridSize() const noexcept;

			size_t NumSamples() const noexcept;
			std::vector<glm::vec2> GetHeights() const;
			std::vector<HeightSample> GetSamples() const;

			size_t NodeID();

			static void SetRootNodeSize(const size_t& new_size);
			static void SetRootNodeLength(const double& new_length);

			static size_t RootSampleGridSize;
			static double RootNodeLength;
			
			float MinZ, MaxZ;
			const HeightNode* Parent;
		protected:

			size_t sampleGridSize = 16;
			size_t meshGridSize;
			std::vector<HeightSample> samples;
			glm::ivec3 gridCoords;
			size_t nodeID;
		};

	}
}

#endif // !VULPES_HEIGHTMAP_H
