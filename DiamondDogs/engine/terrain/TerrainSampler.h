#pragma once
#ifndef VULPES_TERRAIN_SAMPLER_H

#include "stdafx.h"

namespace vulpes {

	namespace terrain {

		/*
		
			class - TerrainSampler

			Stores data (of various types) for sampling when creating terrain nodes. 
			This class is abstract: HeightmapSampler is the first concrete implementation
			of this class. 

			Other implementations should come about for sampling things like colors, environmental data,
			atmospheric data, and more. 
		
		*/


		class Sampler {
			Sampler(const Sampler& other) = delete;
			Sampler& operator=(const Sampler& other) = delete;
		public:

			enum class addressMode {
				CLAMP,
				REPEAT,
				MIRROR,
			};

			addressMode AddressMode = addressMode::CLAMP;
			
			Sampler(const size_t& width, const size_t& height);

			virtual ~Sampler();

			virtual float Sample(const size_t& x, const size_t& y) = 0;

		protected:
			
			std::unique_ptr<Sampler> prev = std::unique_ptr<Sampler>(nullptr);

		};

		class HeightmapSampler : public Sampler {
		public:

			HeightmapSampler(const char* filename, const size_t& width, const size_t& height);

			~HeightmapSampler() = default;

			virtual float Sample(const size_t& x, const size_t& y) override;

		protected:

			gli::texture2d heightmap;
		};

	}

}

#endif // !VULPES_TERRAIN_SAMPLER_H
