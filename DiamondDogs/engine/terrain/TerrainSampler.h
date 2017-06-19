#pragma once
#ifndef VULPES_TERRAIN_SAMPLER_H

#include "stdafx.h"
#include "engine\util\noise.h"
#include "engine\util\FastNoise.h"

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
			virtual float Sample(const size_t& lod_level, const glm::vec3& pos, glm::vec3& normal) const = 0;
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


		class NoiseSampler : public Sampler {
		public:

			NoiseSampler(const FastNoise::NoiseType& noise_type, const FastNoise::FractalType& fractal_type, const float& freq, const size_t& octaves, const float& lacun, const float& gain, const size_t& seed = 6266);

			virtual float Sample(const size_t& lod_level, const glm::vec3& pos, glm::vec3& normal) const override;
			virtual float Sample(const size_t& x, const size_t& y) override { return 0.0f; }

		private:
			float freq, lacun, gain;
			size_t octaves;
			size_t seed;

		};

	}

}

#endif // !VULPES_TERRAIN_SAMPLER_H
