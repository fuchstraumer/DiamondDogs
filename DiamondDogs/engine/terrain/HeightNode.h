#pragma once
#ifndef VULPES_TERRAIN_HEIGHT_NODE_H
#define VULPES_TERRAIN_HEIGHT_NODE_H

#include "stdafx.h"
#include "TerrainSampler.h"
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

		static std::vector<HeightSample> GetNoiseHeightmap(const size_t& num_samples, const glm::vec3& starting_location, const float& noise_step_size);


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

			void SampleFromResiduals(glm::ivec3& node_coords, const Sampler& sampler);
			
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
