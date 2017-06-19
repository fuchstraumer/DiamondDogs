#pragma once
#ifndef VULPES_NOISE_H
#define VULPES_NOISE_H

#include "stdafx.h"
#include <random>
namespace vulpes {

	constexpr static uint32_t FNV_32_PRIME = 0x01000193;
	constexpr static uint32_t FNV_32_INIT = 2166136261;
	constexpr static uint32_t FNV_MASK_8 = (1 << 8) - 1;

	class SNoise {
	public:

		static float FBM(const glm::vec3& pos, const int32_t& seed, const float& freq, const size_t& octaves, const float& lacun, const float& persistance);
		static float FBM_Bounded(const glm::vec3& pos, const int32_t& seed, const float& freq, const size_t& octaves, const float& lacun, const float& persistence, const float& min, const float& max);

		static float FBM(const glm::vec2& pos, const int32_t& seed, const float& freq, const size_t& octaves, const float& lacun, const float& persistance);
		static float FBM_Bounded(const glm::vec2& pos, const int32_t& seed, const float& freq, const size_t& octaves, const float& lacun, const float& persistence, const float& min, const float& max);

		static float DecarpientierSwiss(const glm::vec3& pos, const int32_t& seed, const float& freq, const size_t& octaves, const float& lacun, const float& persistance);
		static float DecarpientierSwiss_Bounded(const glm::vec3& pos, const int32_t& seed, const float& freq, const size_t& octaves, const float& lacun, const float& persistence, const float& min, const float& max);

		static float DecarpientierSwiss(const glm::vec2& pos, const int32_t& seed, const float& freq, const size_t& octaves, const float& lacun, const float& persistance);

		static float SimplexBase(const glm::vec3& pos, glm::vec3& norm, const int32_t& seed);

		static float SimplexBase(const glm::vec2 & pos, glm::vec2 & norm, const int32_t & seed);
		
	private:

		std::mt19937 rng;

	};

}

#endif // !VULPES_NOISE_H
