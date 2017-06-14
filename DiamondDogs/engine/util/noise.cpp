#include "stdafx.h"
#include "Noise.h"

namespace vulpes {

	constexpr static int16_t GRADIENT_3D_LUT[256][3] {
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 }, 
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }, { 1,0,0 }, { -1,0,0 },
		{ 0,0,1 }, { 0,0,-1 }, { 0,1,0 }, { 0,-1,0 }
	};

	constexpr static float SIMPLEX_F3 = 0.33333333f;
	constexpr static float SIMPLEX_G3 = 0.16666667f;

	constexpr static uint32_t FNV_32_A_BUF(void* buff, const uint32_t& len) {
		uint32_t hval = FNV_32_INIT;
		uint32_t *bp = reinterpret_cast<uint32_t*>(buff);
		uint32_t *be = bp + len;
		while (bp < be) {
			hval ^= *bp++;
			hval *= FNV_32_PRIME;
		}
		return hval;
	}

	constexpr static uint8_t XOR_FOLD_HASH(const uint32_t& hash) {
		return ((hash >> 8) ^ (hash & FNV_MASK_8));
	}

	constexpr static uint8_t hash3D(const glm::ivec3& pos, const int& seed) {
		uint32_t val[4]{ pos.x, pos.y, pos.z, seed };
		return XOR_FOLD_HASH(FNV_32_A_BUF(val, 4));
	}

	constexpr static uint32_t hash3D_float(const glm::vec3& pos, const int& seed) {
		uint32_t val[4]{ static_cast<uint32_t>(pos.x), static_cast<uint32_t>(pos.y), static_cast<uint32_t>(pos.z), static_cast<uint32_t>(seed) };
		return XOR_FOLD_HASH(FNV_32_A_BUF(val, 4));
	}

	float SNoise::FBM(const glm::vec3 & pos, const int32_t& seed, const float & freq, const size_t & octaves, const float & lacun, const float & persistance) {
		glm::vec3 npos = pos * freq;
		float sum = 0.0f, amplitude = 1.0f;

		for (size_t i = 0; i < octaves; ++i) {
			int32_t oct_seed = (seed + i) & 0xfffffff;
			sum += SimplexBase(npos, glm::vec3(), oct_seed) * amplitude;
			npos *= lacun;
			amplitude *= persistance;
		}

		return sum;
	}

	float SNoise::FBM_Bounded(const glm::vec3 & pos, const int32_t & seed, const float & freq, const size_t & octaves, const float & lacun, const float & persistence, const float & min, const float & max) {
		return (FBM(pos, seed, freq, octaves, lacun, persistence) - min) / (max - min);
	}

	float SNoise::SimplexBase(const glm::vec3 & pos, glm::vec3 & norm, const int32_t& seed) {
		glm::vec3 s = pos + ((pos.x + pos.y + pos.z) * SIMPLEX_F3);
		glm::ivec3 i_s = glm::ivec3(floorf(s.x), floorf(s.y), floorf(s.z));

		glm::vec3 p0;
		p0.x = pos.x - (i_s.x - ((i_s.x + i_s.y + i_s.y) * SIMPLEX_G3));
		p0.y = pos.y - (i_s.y - ((i_s.x + i_s.y + i_s.y) * SIMPLEX_G3));
		p0.z = pos.z - (i_s.z - ((i_s.x + i_s.y + i_s.y) * SIMPLEX_G3));

		glm::ivec3 i1, i2;

		if (p0.x >= p0.y) {
			if (p0.y >= p0.z) {
				i1.x = 1;
				i1.y = 0;
				i1.z = 0;
				i2.x = 1;
				i2.y = 1;
				i2.z = 0;
			}
			else if (p0.x >= p0.z) {
				i1.x = 1;
				i1.y = 0;
				i1.z = 0;
				i2.x = 1;
				i2.y = 0;
				i2.z = 1;
			}
			else {
				i1.x = 0;
				i1.y = 0;
				i1.z = 1;
				i2.x = 1;
				i2.y = 0;
				i2.z = 1;
			}
		}
		// If p0.x < p0.y
		else {
			if (p0.y < p0.z) {
				i1.x = 0;
				i1.y = 0;
				i1.z = 1;
				i2.x = 0;
				i2.y = 1;
				i2.z = 1;
			}
			else if (p0.x < p0.z) {
				i1.x = 0;
				i1.y = 1;
				i1.z = 0;
				i2.x = 0;
				i2.y = 1;
				i2.z = 1;
			}
			else {
				i1.x = 0;
				i1.y = 1;
				i1.z = 0;
				i2.x = 1;
				i2.y = 1;
				i2.z = 0;
			}
		}

		glm::vec3 p1, p2, p3;
		p1 = p0 - glm::vec3(i1) + SIMPLEX_G3;
		p2 = p0 - glm::vec3(i2) + (2.0f * SIMPLEX_G3);
		p3 = p0 - 1.0f + (3.0f * SIMPLEX_G3);

		uint32_t h0, h1, h2, h3;
		h0 = hash3D(i_s, seed);
		h1 = hash3D(i_s + i1, seed);
		h2 = hash3D(i_s + i2, seed);
		h3 = hash3D(i_s + 1, seed);

		glm::vec3 g0, g1, g2, g3;
		g0 = glm::vec3(static_cast<float>(GRADIENT_3D_LUT[h0][0]), static_cast<float>(GRADIENT_3D_LUT[h0][1]), static_cast<float>(GRADIENT_3D_LUT[h0][2]));
		g1 = glm::vec3(static_cast<float>(GRADIENT_3D_LUT[h1][0]), static_cast<float>(GRADIENT_3D_LUT[h1][1]), static_cast<float>(GRADIENT_3D_LUT[h1][2]));
		g2 = glm::vec3(static_cast<float>(GRADIENT_3D_LUT[h2][0]), static_cast<float>(GRADIENT_3D_LUT[h2][1]), static_cast<float>(GRADIENT_3D_LUT[h2][2]));
		g2 = glm::vec3(static_cast<float>(GRADIENT_3D_LUT[h3][0]), static_cast<float>(GRADIENT_3D_LUT[h3][1]), static_cast<float>(GRADIENT_3D_LUT[h3][2]));

		float t0, t1, t2, t3;
		float t0_2, t1_2, t2_2, t3_2;
		float n0, n1, n2, n3;
		t0 = 0.60f - p0.x*p0.x - p0.y*p0.y - p0.z*p0.z;
		if (t0 < 0.0f) {
			t0 = t0_2 = n0 = 0.0f;
			g0 = glm::vec3(0.0f);
		}
		else {
			t0_2 = t0*t0;
			n0 = (t0_2 * t0_2) * (g0.x * p0.x + g0.y * p0.y + g0.z * p0.z);
		}

		t1 = 0.60f - p1.x*p1.x - p1.y*p1.y - p1.z*p1.z;
		if (t1 < 0.0f) {
			t1 = t1_2 = n1 = 0.0f;
			g1 = glm::vec3(0.0f);
		}
		else {
			t1_2 = t1 * t1;
			n1 = (t1_2 * t1_2) * (g1.x*p1.x + g1.y*p1.y + g1.z*p1.z);
		}

		t2 = 0.60f - p2.x*p2.x - p2.y*p2.y - p2.z*p2.z;
		if (t2 < 0.0f) {
			t2 = t2_2 = n2 = 0.0f;
			g2 = glm::vec3(0.0f);
		}
		else {
			t2_2 = t2 * t2;
			n2 = (t2_2 * t2_2) * (g2.x*p2.x + g2.y*p2.y + g2.z*p2.z);
		}

		t3 = 0.60f - p3.x*p3.x - p3.y*p3.y - p3.z*p3.z;
		if (t3 < 0.0f) {
			t3 = t3_2 = n3 = 0.0f;
			g3 = glm::vec3(0.0f);
		}
		else {
			t3_2 = t3 * t3;
			n3 = (t3_2 * t3_2) * (g3.x*p3.x + g3.y*p3.y + g3.z*p2.z);
		}

		float noise = 28.0f * (n0 + n1 + n2 + n3);

		{
			float tmp0, tmp1, tmp2, tmp3;
			tmp0 = t0_2 * t0 * (g0.x*p0.x + g0.y*p0.y + g0.z*p0.z);
			norm.x = tmp0 * p0.x;
			norm.y = tmp0 * p0.y;
			norm.z = tmp0 * p0.z;
			tmp1 = t1_2 * t1 * (g1.x*p1.x + g1.y*p1.y + g1.z*p1.z);
			norm.x += tmp1 * p1.x;
			norm.y += tmp1 * p1.y;
			norm.z += tmp1 * p1.z;
			tmp2 = t2_2 * t2 * (g2.x*p2.x + g2.y*p2.y + g2.z*p2.z);
			norm.x += tmp2 * p2.x;
			norm.y += tmp2 * p2.y;
			norm.z += tmp2 * p2.z;
			tmp3 = t3_2 * t3 * (g3.x*p3.x + g3.y*p3.y + g3.z*p3.z);
			norm.x += tmp3 * p3.x;
			norm.y += tmp3 * p3.y;
			norm.z += tmp3 * p3.z;
			norm *= -8.0f;
			g0 *= (t0_2 * t0_2);
			g1 *= (t1_2 * t1_2);
			g2 *= (t2_2 * t2_2);
			g3 *= (t3_2 * t3_2);
			norm += (g0 + g1 + g2 + g3);
			norm *= 28.0f;
		}
		
		return noise;
	}

}

