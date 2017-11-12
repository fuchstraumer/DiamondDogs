#pragma once
#ifndef VULPES_TERRAIN_HEIGHT_NODE_H
#define VULPES_TERRAIN_HEIGHT_NODE_H

#include "stdafx.h"
#include "engine\util\noise.hpp"

namespace vulpes {

	namespace terrain {

		enum class NodeStatus {
		Undefined, // Initial value for all nodes. If a node has this, it has not been properly constructed (or has been deleted)
		Active, // Being used in next frame
		Subdivided, // Has been subdivided, render children instead of this.
		NeedsUnload, // Erase and unload resources.
		NeedsTransfer,
		RequestData, // Request height data from the upsampling DataProducer
	};


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

		class HeightNode {
		public:

			// Used for root node 
			HeightNode(const glm::ivec3& node_grid_coordinates, std::vector<HeightSample> init_samples);

			// Used for most nodes.
			HeightNode(const glm::ivec3& node_grid_coordinates, const HeightNode* parent);

			~HeightNode() = default;
			
			void SampleFromParent();

			void SetSamples(std::vector<HeightSample>&& samples);

			float GetHeight(const glm::vec2 world_pos) const;

			float Sample(const size_t& x, const size_t& y) const;
			float Sample(const size_t& idx) const;

			const HeightSample& operator[](const size_t& idx) const;
			HeightSample& operator[](const size_t& idx);

			const glm::ivec3& GridCoords() const noexcept;

			size_t MeshGridSize() const noexcept;
			size_t SampleGridSize() const noexcept;

			size_t NumSamples() const noexcept;
			std::vector<glm::vec2> GetHeights() const;
			std::vector<HeightSample> GetSamples() const;

			static void SetRootNodeSize(const size_t& new_size);
			static void SetRootNodeLength(const double& new_length);

			static size_t RootSampleGridSize;
			static double RootNodeLength;
			
			float MinZ, MaxZ;
			const HeightNode* Parent;
		protected:

			std::mutex samplesMutex;
			size_t sampleGridSize = 16;
			size_t meshGridSize;
			std::vector<HeightSample> samples;
			glm::ivec3 gridCoords;
			size_t nodeID;
		};

		static inline std::unique_ptr<HeightNode> CreateHeightNodeFromParent(const glm::ivec3& grid_coords, const HeightNode* parent_node) {
			return std::move(std::make_unique<HeightNode>(grid_coords, parent_node));
		}

	}
}

#endif // !VULPES_HEIGHTMAP_H
