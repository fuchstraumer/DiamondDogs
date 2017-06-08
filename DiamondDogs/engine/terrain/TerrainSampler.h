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
			
			Sampler() = default;

			virtual ~Sampler() = default;

			virtual float Sample(const size_t& x, const size_t& y) = 0;

		};

		enum class ModType {
			ADD,
			SUBTRACT,
			MULTIPLY,
			DIVIDE,
			POW,
			LERP,
		};

		class Modifier : public Sampler {
		public:

			Modifier(ModType modifier_type, Sampler* left, Sampler* right);

			virtual float Sample(const size_t& x, const size_t& y);

			ModType ModifierType;

		private:
			
			std::unique_ptr<Sampler> leftOperand, rightOperand;
		};

		enum class addressMode {
			CLAMP,
			REPEAT,
			MIRROR,
		};

		
		class HeightmapSampler : public Sampler {
		public:

			HeightmapSampler(const char* filename, addressMode addr_mode);

			~HeightmapSampler() = default;

			virtual float Sample(const size_t& x, const size_t& y) override;

			addressMode AddressMode;

		protected:

			//std::shared_ptr<Heightmap> heightmap;

		};

	}

}

#endif // !VULPES_TERRAIN_SAMPLER_H
