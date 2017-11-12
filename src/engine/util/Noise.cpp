#include "stdafx.h"
#include "Noise.hpp"


namespace vulpes {

	constexpr static float grad3lut[16][3] = {
		{ 1.0f, 0.0f, 1.0f },{ 0.0f, 1.0f, 1.0f }, // 12 cube edges
		{ -1.0f, 0.0f, 1.0f },{ 0.0f, -1.0f, 1.0f },
		{ 1.0f, 0.0f, -1.0f },{ 0.0f, 1.0f, -1.0f },
		{ -1.0f, 0.0f, -1.0f },{ 0.0f, -1.0f, -1.0f },
		{ 1.0f, -1.0f, 0.0f },{ 1.0f, 1.0f, 0.0f },
		{ -1.0f, 1.0f, 0.0f },{ -1.0f, -1.0f, 0.0f },
		{ 1.0f, 0.0f, 1.0f },{ -1.0f, 0.0f, 1.0f }, // 4 repeats to make 16
		{ 0.0f, 1.0f, -1.0f },{ 0.0f, -1.0f, -1.0f }
	};

	constexpr static int16_t GRADIENT_2D_LUT[256][2]{
		{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },
		{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },
		{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },
		{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },
		{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },
		{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },
		{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },
		{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },
		{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },
		{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },
		{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },
		{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },
		{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },
		{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },
		{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },
		{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },
		{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },
		{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },
		{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },
		{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },
		{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },
		{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },
		{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },
		{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },
		{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },
		{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },
		{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },
		{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },
		{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },
		{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },
		{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },
		{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },{ 0,1 },{ 0,-1 },{ 1,0 },{ -1,0 },
	};

	constexpr static float SIMPLEX_F3 = 0.33333333f;
	constexpr static float SIMPLEX_G3 = 0.16666667f;

	constexpr static float SIMPLEX_F2 = 0.366035403f;
	constexpr static float SIMPLEX_G2 = 0.211324865f;

	constexpr static int32_t FNV_32_A_BUF(void* buff, const int32_t& len) {
		int32_t hval = FNV_32_INIT;
		int32_t *bp = reinterpret_cast<int32_t*>(buff);
		int32_t *be = bp + len;
		while (bp < be) {
			hval ^= *bp++;
			hval *= FNV_32_PRIME;
		}
		return hval;
	}

	constexpr static uint8_t XOR_FOLD_HASH(const int32_t& hash) {
		return static_cast<uint8_t>((hash >> 8) ^ (hash & FNV_MASK_8));
	}

	constexpr static int32_t hash2D(const glm::ivec2& pos, const int& seed) {
		int32_t val[3]{ static_cast<int32_t>(pos.x), static_cast<int32_t>(pos.y), static_cast<int32_t>(seed) };
		return XOR_FOLD_HASH(FNV_32_A_BUF(val, 3));
	}

	constexpr static int32_t hash2D_float(const glm::vec2& pos, const int& seed) {
		int32_t val[3]{ static_cast<int32_t>(pos.x), static_cast<int32_t>(pos.y), seed };
		return XOR_FOLD_HASH(FNV_32_A_BUF(val, 3));
	}

	constexpr static uint8_t hash3D(const glm::ivec3& pos, const int& seed) {
		int32_t val[4]{ static_cast<int32_t>(pos.x), static_cast<int32_t>(pos.y), static_cast<int32_t>(pos.z), static_cast<int32_t>(seed) };
		return XOR_FOLD_HASH(FNV_32_A_BUF(val, 4));
	}

	constexpr static int32_t hash3D_float(const glm::vec3& pos, const int& seed) {
		int32_t val[4]{ static_cast<int32_t>(pos.x), static_cast<int32_t>(pos.y), static_cast<int32_t>(pos.z), static_cast<int32_t>(seed) };
		return XOR_FOLD_HASH(FNV_32_A_BUF(val, 4));
	}

	static void grad3(int hash, glm::vec3& grad) {
		int h = hash & 15;
		grad.x = grad3lut[h][0];
		grad.y = grad3lut[h][1];
		grad.z = grad3lut[h][2];
		return;
	}

	float SNoise::FBM(const glm::vec3 & pos, const int32_t& seed, const float & freq, const size_t & octaves, const float & lacun, const float & persistance) {
		glm::vec3 npos = pos * freq;
		float sum = 0.0f, amplitude = 1.0f;
		NoiseGen ng;
		for (size_t i = 0; i < octaves; ++i) {
			sum += ng.sdnoise3(npos.x, npos.y, npos.z, nullptr) * amplitude;
			npos *= lacun;
			amplitude *= persistance;
		}

		return sum;
	}

	float SNoise::FBM_Bounded(const glm::vec3 & pos, const int32_t & seed, const float & freq, const size_t & octaves, const float & lacun, const float & persistence, const float & min, const float & max) {
		return (FBM(pos, seed, freq, octaves, lacun, persistence) - min) / (max - min);
	}

	float SNoise::FBM(const glm::vec2 & pos, const int32_t & seed, const float & freq, const size_t & octaves, const float & lacun, const float & persistance) {
		glm::vec2 npos = pos * freq;
		float sum = 0.0f, amplitude = 1.0f;
		NoiseGen ng;
		for (size_t i = 0; i < octaves; ++i) {
			sum += ng.sdnoise2(npos.x, npos.y, nullptr, nullptr) * amplitude;
			npos *= lacun;
			amplitude *= persistance;
		}

		return sum;
	}

	float SNoise::FBM_Bounded(const glm::vec2 & pos, const int32_t & seed, const float & freq, const size_t & octaves, const float & lacun, const float & persistence, const float & min, const float & max) {
		return (FBM(pos, seed, freq, octaves, lacun, persistence) - min) / (max - min);
	}

	float SNoise::Billow(const glm::vec3 & pos, const int32_t & seed, const float & freq, const size_t & octaves, const float & lacun, const float & persistance) {
		glm::vec3 npos = pos * freq;
		float sum = 0.0f, amplitude = 1.0f;
		NoiseGen ng;
		for (size_t i = 0; i < octaves; ++i) {
			sum += fabsf(ng.sdnoise3(npos.x, npos.y, npos.z, nullptr)) * amplitude;
			npos *= lacun;
			amplitude *= persistance;
		}
		return sum;
	}

	float SNoise::RidgedMulti(const glm::vec3 & pos, const int32_t & seed, const float & freq, const size_t & octaves, const float & lacun, const float & persistance) {
		glm::vec3 npos = pos * freq;
		float sum = 0.0f, amplitude = 1.0f;
		NoiseGen ng;
		for (size_t i = 0; i < octaves; ++i) {
			sum += 1.0f - fabsf(ng.sdnoise3(npos.x, npos.y, npos.z, nullptr)) * amplitude;
			npos *= lacun;
			amplitude *= persistance;
		}
		return sum;
	}

	float SNoise::DecarpientierSwiss(const glm::vec3 & pos, const int32_t & seed, const float & freq, const size_t & octaves, const float & lacun, const float & persistance) {
		float sum = 0.0f;
		float amplitude = 1.0f;
		float warp = 0.03f;

		glm::vec3 sp = pos * freq;
		glm::vec3 dSum = glm::vec3(0.0f);
		NoiseGen ng;

		for (size_t i = 0; i < octaves; ++i) {
			glm::vec3 deriv;
			float n = ng.sdnoise3(sp.x, sp.y, sp.z, &deriv);
			sum += (1.0f - fabsf(n)) * amplitude;
			dSum += amplitude * -n * deriv;
			sp *= lacun;
			sp += (warp * dSum);
			amplitude *= (glm::clamp(sum, 0.0f, 1.0f) * persistance);
		}

		return sum;
	}

	float SNoise::DecarpientierSwiss_Bounded(const glm::vec3 & pos, const int32_t & seed, const float & freq, const size_t & octaves, const float & lacun, const float & persistence, const float & min, const float & max) {
		return (DecarpientierSwiss(pos, seed, freq, octaves, lacun, persistence) - min) / (max - min);
	}

	float SNoise::DecarpientierSwiss(const glm::vec2 & pos, const int32_t & seed, const float & freq, const size_t & octaves, const float & lacun, const float & persistance) {
		float sum = 0.0f;
		float amplitude = 1.0f;
		float warp = 0.1f;

		glm::vec2 sp = pos * freq;
		glm::vec2 dSum = glm::vec2(0.0f);
		NoiseGen ng;

		for (size_t i = 0; i < octaves; ++i) {
			glm::vec2 deriv;
			float n = ng.sdnoise2(sp.x, sp.y, &deriv.x, &deriv.y);
			sum += (1.0f - fabsf(n)) * amplitude;
			dSum += amplitude * -n * deriv;
			sp *= lacun;
			sp += (warp * dSum);
			amplitude *= (glm::clamp(sum, 0.0f, 1.0f) * persistance);
		}

		return sum;
	}

}

