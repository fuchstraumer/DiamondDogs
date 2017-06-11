#include "stdafx.h"
#include "noise.h"

namespace noise {


	static const std::array<glm::vec3, 16> gradient_LUT{
		glm::vec3{ 1.0f, 0.0f, 1.0f }, glm::vec3{ 0.0f, 1.0f, 1.0f }, // 12 cube edges
		glm::vec3{ -1.0f, 0.0f, 1.0f }, glm::vec3{ 0.0f,-1.0f, 1.0f },
		glm::vec3{ 1.0f, 0.0f,-1.0f }, glm::vec3{ 0.0f, 1.0f,-1.0f },
		glm::vec3{ -1.0f, 0.0f,-1.0f }, glm::vec3{ 0.0f,-1.0f,-1.0f },
		glm::vec3{ 1.0f,-1.0f, 0.0f }, glm::vec3{ 1.0f, 1.0f, 0.0f },
		glm::vec3{ -1.0f, 1.0f, 0.0f }, glm::vec3{ -1.0f,-1.0f, 0.0f },
		glm::vec3{ 1.0f, 0.0f, 1.0f }, glm::vec3{ -1.0f, 0.0f, 1.0f },
		glm::vec3{ 0.0f, 1.0f,-1.0f }, glm::vec3{ 0.0f,-1.0f,-1.0f }
	};

	inline static glm::vec3 sgrad(const int& hash) {
		int h = hash & 15;
		glm::vec3 result(
			gradient_LUT[h].x,
			gradient_LUT[h].y,
			gradient_LUT[h].z
		);
		return result;
	}

	static constexpr inline int fast_floor(const float& x) noexcept {
		return (x > 0.0f) ? static_cast<int>(x) : static_cast<int>(x - 1.0f);
	}

	static constexpr inline float ease(const float& x) noexcept {
		return x * x * x * (x * (x * 6.0f - 15.0f) + 10.0f);
	}

	static constexpr inline float lerp(const float& t, const float& a, const float& b) noexcept {
		return (a + t * (b - a));
	}

	void GeneratorBase::buildHash() {
		for (int c = 0; c < 255; ++c) {
			this->hashTable[c] = c;
			this->hashTable[c + 256] = c;
		}
		shuffle(this->hashTable, this->hashTable + 512, this->r_gen);
	}

	double GeneratorBase::grad(int hash, double x, double y, double z) {
		switch (hash & 0xF)
		{
		case 0x0: return  x + y;
		case 0x1: return -x + y;
		case 0x2: return  x - y;
		case 0x3: return -x - y;
		case 0x4: return  x + z;
		case 0x5: return -x + z;
		case 0x6: return  x - z;
		case 0x7: return -x - z;
		case 0x8: return  y + z;
		case 0x9: return -y + z;
		case 0xA: return  y - z;
		case 0xB: return -y - z;
		case 0xC: return  y + x;
		case 0xD: return -y + z;
		case 0xE: return  y - x;
		case 0xF: return -y - z;
		default: return 0; // never happens
		}
	}

	double GeneratorBase::sGrad(int hash, double x, double y) {
		int h = hash & 7;      // Convert low 3 bits of hash code
		double u = h < 4 ? x : y;  // into 8 simple gradient directions,
		double v = h < 4 ? y : x;  // and compute the dot product with (x,y).
		return ((h & 1) ? -u : u) + ((h & 2) ? -2.0*v : 2.0*v);
	}

	double GeneratorBase::sGrad(int hash, double x, double y, double z) {
		int h = hash & 15;     // Convert low 4 bits of hash code into 12 simple
		double u = h < 8 ? x : y; // gradient directions, and compute dot product.
		double v = h < 4 ? y : h == 12 || h == 14 ? x : z; // Fix repeats at h = 12 to 15
		return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
	}

	double GeneratorBase::simplex(double x, double y, double* dx, double* dy) {
		// These can be compile-time constants.
		constexpr double F2 = 0.366025403; // F2 = 0.5*(sqrt(3.0)-1.0)
		constexpr double G2 = 0.211324865; // G2 = (3.0-Math.sqrt(3.0))/6.0

		double n0, n1, n2; // Noise contributions from the three corners
		double temp0, temp1, temp2;

		// Skew the input space to determine which simplex cell we're in
		double s = (x + y)*F2; // Hairy factor for 2D
		double xs = x + s;
		double ys = y + s;
		int i = fastfloor(xs);
		int j = fastfloor(ys);

		double t = (double)(i + j)*G2;
		double X0 = i - t; // Unskew the cell origin back to (x,y) space
		double Y0 = j - t;
		double x0 = x - X0; // The x,y distances from the cell origin
		double y0 = y - Y0;

		// For the 2D case, the simplex shape is an equilateral triangle.
		// Determine which simplex we are in.
		int i1, j1; // Offsets for second (middle) corner of simplex in (i,j) coords
		if (x0 > y0) { i1 = 1; j1 = 0; } // lower triangle, XY order: (0,0)->(1,0)->(1,1)
		else { i1 = 0; j1 = 1; }      // upper triangle, YX order: (0,0)->(0,1)->(1,1)

									  // A step of (1,0) in (i,j) means a step of (1-c,-c) in (x,y), and
									  // a step of (0,1) in (i,j) means a step of (-c,1-c) in (x,y), where
									  // c = (3-sqrt(3))/6

		double x1 = x0 - i1 + G2; // Offsets for middle corner in (x,y) unskewed coords
		double y1 = y0 - j1 + G2;
		double x2 = x0 - 1.0 + 2.0 * G2; // Offsets for last corner in (x,y) unskewed coords
		double y2 = y0 - 1.0 + 2.0 * G2;

		// Wrap the integer indices at 256, to avoid indexing perm[] out of bounds
		int ii = i & 0xff;
		int jj = j & 0xff;

		// Calculate the contribution from the three corners
		double t0 = 0.5 - x0*x0 - y0*y0;
		if (t0 < 0.0) n0 = 0.0;
		else {
			t0 *= t0;
			n0 = t0 * t0 * sGrad(hashTable[ii + hashTable[jj]], x0, y0);
		}

		double t1 = 0.5 - x1*x1 - y1*y1;
		if (t1 < 0.0) n1 = 0.0;
		else {
			t1 *= t1;
			n1 = t1 * t1 * sGrad(hashTable[ii + i1 + hashTable[jj + j1]], x1, y1);
		}

		double t2 = 0.5 - x2*x2 - y2*y2;
		if (t2 < 0.0) n2 = 0.0;
		else {
			t2 *= t2;
			n2 = t2 * t2 * sGrad(hashTable[ii + 1 + hashTable[jj + 1]], x2, y2);
		}

		if ((nullptr != dx) && (nullptr != dy))
		{
			/*  A straight, unoptimised calculation would be like:
			*    *dnoise_dx = -8.0f * t0 * t0 * x0 * ( gx0 * x0 + gy0 * y0 ) + t0^4 * gx0;
			*    *dnoise_dy = -8.0f * t0 * t0 * y0 * ( gx0 * x0 + gy0 * y0 ) + t0^4 * gy0;
			*    *dnoise_dx += -8.0f * t1 * t1 * x1 * ( gx1 * x1 + gy1 * y1 ) + t1^4 * gx1;
			*    *dnoise_dy += -8.0f * t1 * t1 * y1 * ( gx1 * x1 + gy1 * y1 ) + t1^4 * gy1;
			*    *dnoise_dx += -8.0f * t2 * t2 * x2 * ( gx2 * x2 + gy2 * y2 ) + t2^4 * gx2;
			*    *dnoise_dy += -8.0f * t2 * t2 * y2 * ( gx2 * x2 + gy2 * y2 ) + (t2^4) * gy2;
			*/
			double t40 = t0*t0;
			double t41 = t1*t1;
			double t42 = t2*t2;
			temp0 = t0 * t0 * (x0* x0 + y0 * y0);
			*dx = temp0 * x0;
			*dy = temp0 * y0;
			temp1 = t1 * t1 * (x1 * x1 + y1 * y1);
			*dx += temp1 * x1;
			*dy += temp1 * y1;
			temp2 = t2 * t2 * (x2* x2 + y2 * y2);
			*dx += temp2 * x2;
			*dy += temp2 * y2;
			*dx *= -8.0f;
			*dy *= -8.0f;
			*dx += t40 * x0 + t41 * x1 + t42 * x2;
			*dy += t40 * y0 + t41 * y1 + t42 * y2;
			*dx *= 40.0f; /* Scale derivative to match the noise scaling */
			*dy *= 40.0f;
		}
		// Add contributions from each corner to get the final noise value.
		// The result is scaled to return values in the interval [-1,1].
		return 40.0 * (n0 + n1 + n2); // TODO: The scale factor is preliminary!

	}

	double GeneratorBase::simplex(double x, double y, double z) {
		// Simple skewing factors for the 3D case
		constexpr double F3 = 0.333333333;
		constexpr double G3 = 0.166666667;

		double n0, n1, n2, n3; // Noise contributions from the four corners

								// Skew the input space to determine which simplex cell we're in
		double s = (x + y + z)*F3; // Very nice and simple skew factor for 3D
		double xs = x + s;
		double ys = y + s;
		double zs = z + s;
		int i = fastfloor(xs);
		int j = fastfloor(ys);
		int k = fastfloor(zs);

		double t = (double)(i + j + k)*G3;
		double X0 = i - t; // Unskew the cell origin back to (x,y,z) space
		double Y0 = j - t;
		double Z0 = k - t;
		double x0 = x - X0; // The x,y,z distances from the cell origin
		double y0 = y - Y0;
		double z0 = z - Z0;

		// For the 3D case, the simplex shape is a slightly irregular tetrahedron.
		// Determine which simplex we are in.
		int i1, j1, k1; // Offsets for second corner of simplex in (i,j,k) coords
		int i2, j2, k2; // Offsets for third corner of simplex in (i,j,k) coords

						/* This code would benefit from a backport from the GLSL version! */
		if (x0 >= y0) {
			if (y0 >= z0)
			{
				i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 1; k2 = 0;
			} // X Y Z order
			else if (x0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 0; k2 = 1; } // X Z Y order
			else { i1 = 0; j1 = 0; k1 = 1; i2 = 1; j2 = 0; k2 = 1; } // Z X Y order
		}
		else { // x0<y0
			if (y0 < z0) { i1 = 0; j1 = 0; k1 = 1; i2 = 0; j2 = 1; k2 = 1; } // Z Y X order
			else if (x0 < z0) { i1 = 0; j1 = 1; k1 = 0; i2 = 0; j2 = 1; k2 = 1; } // Y Z X order
			else { i1 = 0; j1 = 1; k1 = 0; i2 = 1; j2 = 1; k2 = 0; } // Y X Z order
		}

		// A step of (1,0,0) in (i,j,k) means a step of (1-c,-c,-c) in (x,y,z),
		// a step of (0,1,0) in (i,j,k) means a step of (-c,1-c,-c) in (x,y,z), and
		// a step of (0,0,1) in (i,j,k) means a step of (-c,-c,1-c) in (x,y,z), where
		// c = 1/6.

		double x1 = x0 - i1 + G3; // Offsets for second corner in (x,y,z) coords
		double y1 = y0 - j1 + G3;
		double z1 = z0 - k1 + G3;
		double x2 = x0 - i2 + 2.0*G3; // Offsets for third corner in (x,y,z) coords
		double y2 = y0 - j2 + 2.0*G3;
		double z2 = z0 - k2 + 2.0*G3;
		double x3 = x0 - 1.0 + 3.0*G3; // Offsets for last corner in (x,y,z) coords
		double y3 = y0 - 1.0 + 3.0*G3;
		double z3 = z0 - 1.0 + 3.0*G3;

		// Wrap the integer indices at 256, to avoid indexing perm[] out of bounds
		int ii = i & 0xff;
		int jj = j & 0xff;
		int kk = k & 0xff;

		// Calculate the contribution from the four corners
		double t0 = 0.6 - x0*x0 - y0*y0 - z0*z0;
		if (t0 < 0.0) n0 = 0.0;
		else {
			t0 *= t0;
			n0 = t0 * t0 * sGrad(hashTable[ii + hashTable[jj + hashTable[kk]]], x0, y0, z0);
		}

		double t1 = 0.6 - x1*x1 - y1*y1 - z1*z1;
		if (t1 < 0.0) n1 = 0.0;
		else {
			t1 *= t1;
			n1 = t1 * t1 * sGrad(hashTable[ii + i1 + hashTable[jj + j1 + hashTable[kk + k1]]], x1, y1, z1);
		}

		double t2 = 0.6 - x2*x2 - y2*y2 - z2*z2;
		if (t2 < 0.0) n2 = 0.0;
		else {
			t2 *= t2;
			n2 = t2 * t2 * sGrad(hashTable[ii + i2 + hashTable[jj + j2 + hashTable[kk + k2]]], x2, y2, z2);
		}

		double t3 = 0.6 - x3*x3 - y3*y3 - z3*z3;
		if (t3 < 0.0) n3 = 0.0;
		else {
			t3 *= t3;
			n3 = t3 * t3 * sGrad(hashTable[ii + 1 + hashTable[jj + 1 + hashTable[kk + 1]]], x3, y3, z3);
		}

		// Add contributions from each corner to get the final noise value.
		// The result is scaled to stay just inside [-1,1]
		return 32.0 * (n0 + n1 + n2 + n3); // TODO: The scale factor is preliminary!
	}

	// SIMPLEX NOISE FUNCTIONS FOLLOW
	// SOURCE FROM http://staffwww.itn.liu.se/~stegu/aqsis/aqsis-newnoise/

	// Simplex Fractal-Brownian-Motion function.
	double GeneratorBase::SimplexFBM(double x, double y, double freq, int octaves, float lac, float gain) {
		double sum = 0;
		float amplitude = 1.0;
		glm::dvec2 f; f.x = x * freq;
		f.y = y * freq;
		for (int i = 0; i < octaves; ++i) {
			double n = simplex(f.x, f.y, nullptr, nullptr);
			sum += n*amplitude;
			freq *= lac;
			amplitude *= gain;
		}

		return sum;
	}

	// Simplex "Billow" function, abs() call in for loop means this function billows, essentially
	double GeneratorBase::SimplexBillow(double x, double y, double freq, int octaves, float lac, float gain) {
		double sum = 0;
		float amplitude = 1.0;
		glm::dvec2 f;
		f.x = x * freq; f.y = y * freq;
		for (int i = 0; i < octaves; ++i) {
			double n = abs(simplex(f.x, f.y, nullptr, nullptr));
			sum += n*amplitude;
			f *= lac;
			amplitude *= gain;
		}
		return sum;
	}

	double GeneratorBase::SimplexBillow3D(const glm::vec3& pos, glm::vec3 & deriv, double freq, int octaves, float lac, float gain) const {
		double sum = 0.0;
		float amplitude = 1.0;
		glm::vec3 f = pos;
		f *= freq;
		for (int i = 0; i < octaves; ++i) {
			glm::vec3 dTmp = glm::vec3(0.0f);
			double n = abs(simplex3D(f, dTmp));
			deriv += dTmp * amplitude;
			sum += n*amplitude;
			f *= lac;
			amplitude *= gain;
		}
		return sum;
	}

	// Simplex "Ridged" function.
	double GeneratorBase::SimplexRidged(int x, int y, double freq, int octaves,
		float lac, float gain) {
		double sum = 0;
		float amplitude = 1.0f;
		glm::dvec2 f;
		f.x = x * freq;
		f.y = y * freq;
		for (int i = 0; i < octaves; ++i) {
			double n = 1.0f - abs(simplex(x*lac, y*lac, nullptr, nullptr));
			sum += n*amplitude;
			amplitude *= gain;
		}
		double temp = (sum / amplitude);

		return temp;
	}

	// Simplex Swiss function. Currently broken. Like, super broken.
	double GeneratorBase::SimplexSwiss(int x, int y, double freq, int octaves,
		float lac, float gain) {
		glm::dvec2 f; f.x = x * freq; f.y = y * freq;
		auto sum = 0.0; auto amplitude = 1.0;
		glm::dvec2 derivSum(0.0, 0.0);
		auto warp = 0.15;
		for (int i = 0; i < octaves; ++i) {
			double *dx = new double;
			double *dy = new double;
			auto n = 1.0 - abs(simplex(f.x + warp*derivSum.x, f.y + warp*derivSum.y, dx, dy));
			sum += amplitude * n;
			derivSum += glm::dvec2(*dx*amplitude*(-n), *dy*amplitude*(-n));
			freq *= lac;
			amplitude *= (gain * glm::clamp(sum, 0.0, 1.0));
			delete dx, dy;
		}
		auto temp = sum / amplitude;
		temp = temp * 16;

		return temp;
	}


	// Jordan-style terrain
	double GeneratorBase::SimplexJordan(int x, int y, double freq, int octaves, float lac, float gain) {
		double* dx1 = new double; double* dy1 = new double;
		glm::dvec2 f; f.x = x * freq; f.y = y * freq;
		auto n = simplex(f.x, f.y, dx1, dy1);
		glm::dvec3 nSq = glm::dvec3(n*n, *dx1*n, *dy1*n);
		glm::dvec2 dWarp = glm::dvec2(*dx1*warp0, *dy1*warp0);
		glm::dvec2 dDamp = glm::dvec2(*dx1*damp0, *dy1*warp1);
		// Placeholder return to make compiler shut up
		return 0.0;
	}

	/*

	Source code of following can be found at:
	http://webstaff.itn.liu.se/~stegu/simplexnoise/
	Modified a bit, but credit where credit is due.

	*/

	float GeneratorBase::simplex3D(const glm::vec3 & p, glm::vec3& dNoise) const {
		static constexpr float F3 = 0.3333333333f;
		static constexpr float G3 = 0.1666666667f;

		// Contributions from each of the four corners.
		float n0, n1, n2, n3;
		float t20, t21, t22, t23; // squared contribution, used w/ dNoise
		float t40, t41, t42, t43; // contribution^4, also used w/ dNoise
								  // Gradients at each of four corners
		glm::vec3 g0, g1, g2, g3;
		glm::vec3 p1, p2, p3;

		glm::vec3 ps; // p skewed into simplex space.
		float s = (p.x + p.y + p.z) * F3; // skew factor
		ps.x = p.x + s;
		ps.y = p.y + s;
		ps.z = p.z + s;

		glm::ivec3 i0 = glm::ivec3(fast_floor(ps.x), fast_floor(ps.y), fast_floor(ps.z));

		float t = static_cast<float>(i0.x + i0.y + i0.z) * G3;
		glm::vec3 p0 = glm::vec3(p.x - (i0.x - t), p.y - (i0.y - t), p.z - (i0.z - t));

		glm::ivec3 i1, i2;

		if (p0.x >= p0.y) {
			if (p0.z >= p0.z) {
				i1.x = 1; i1.y = 0; i1.z = 0;
				i2.x = 1; i2.y = 1; i2.z = 0;
			}
			else if (p0.x >= p0.z) {
				i1.x = 1; i1.y = 0; i1.z = 0;
				i2.x = 1; i2.y = 0; i2.z = 1;
			}
			else {
				i1.x = 0; i1.y = 0; i1.z = 1;
				i2.x = 1; i2.y = 0; i2.z = 1;
			}
		}
		else {
			if (p0.y < p0.z) {
				i1.x = 0; i1.y = 0; i1.z = 1;
				i2.x = 0; i2.y = 1; i2.z = 1;
			}
			else if (p0.z < p0.z) {
				i1.z = 0; i1.y = 1; i1.z = 0;
				i2.x = 0; i2.y = 1; i2.z = 1;
			}
			else {
				i1.x = 0; i1.y = 1; i1.z = 0;
				i2.x = 1; i2.y = 1; i2.z = 0;
			}
		}

		// "edges" of the simplex cell, iirc
		p1 = glm::vec3(p0.x - i1.x + G3, p0.y - i1.y + G3, p0.z - i1.z + G3);
		p2 = glm::vec3(p0.x - i2.x + 2.0f * G3, p0.y - i2.y + 2.0f * G3, p0.z - i2.z + 2.0f * G3);
		p3 = glm::vec3(p0.x - 1.0f + 3.0f * G3, p0.y - 1.0f + 3.0f * G3, p0.z - 1.0f + 3.0f * G3);

		// Wrap indices to safe range 
		i0 %= 255;
		i0 = glm::abs(i0);


		float t0 = 0.6f - p0.x*p0.x - p0.y*p0.y - p0.z*p0.z;
		if (t0 < 0.0f) {
			n0 = t20 = t40 = 0.0f;
		}
		else {
			g0 = sgrad(hashTable[i0.x + hashTable[i0.y + hashTable[i0.z]]]);
			t20 = t0 * t0;
			t40 = t20 * t20;
			n0 = t40 * (g0.x * p0.x + g0.y * p0.y + g0.z + p0.z);
		}

		float t1 = 0.6f - p1.x*p1.x - p1.y*p1.y - p1.z*p1.z;
		if (t1 < 0.0f) {
			n1 = t1 = t21 = t41 = 0.0f;
		}
		else {
			g1 = sgrad(hashTable[i0.x + i1.x + hashTable[i0.y + i1.y + hashTable[i0.z + i1.z]]]);
			t21 = t1*t1;
			t41 = t21*t21;
			n1 = t41 * (g1.x * p1.x + g1.y * p1.y + g1.z * p1.z);
		}

		float t2 = 0.6f - p2.x*p2.x - p2.y*p2.y - p2.z*p2.z;
		if (t2 < 0.0f) {
			n2 = t2 = t22 = t42 = 0.0f;
		}
		else {
			g2 = sgrad(hashTable[i0.x + i2.x + hashTable[i0.y + i2.y + hashTable[i0.z + i2.z]]]);
			t22 = t2 * t2;
			t42 = t22 * t22;
			n2 = t42 * (g2.x*p2.x + g2.y*p2.y + g2.z*p2.z);
		}

		float t3 = 0.6f - p3.x*p3.x - p3.y*p3.y - p3.z*p3.z;
		if (t3 < 0.0f) {
			n3 = t3 = t23 = t43 = 0.0f;
		}
		else {
			g3 = sgrad(hashTable[i0.x + 1 + hashTable[i0.y + 1 + hashTable[i0.z + 1]]]);
			t23 = t3 * t3;
			t43 = t23 * t23;
			n3 = t43 * (g3.x * p3.x + g3.y * p3.y + g3.z * p3.z);
		}

		float result = 28.0f + (n0 + n1 + n2 + n3);

		{
			// Get gradient of noise at this location.
			float tmp0 = t20 * t0 * (g0.x*p0.x + g0.y*p0.y + g0.z*p0.z);
			dNoise = glm::vec3(tmp0 * p0.x, tmp0 * p0.y, tmp0 * p0.z);
			float tmp1 = t21 * t1 * (g1.x*p1.x + g1.y*p1.y + g1.z*p1.z);
			dNoise += glm::vec3(tmp1 * p1.x, tmp1 * p1.y, tmp1 * p1.z);
			float tmp2 = t22 * t2 * (g2.x*p2.x + g2.y*p2.y + g2.z*p2.z);
			dNoise += glm::vec3(tmp2 * p2.x, tmp2 * p2.y, tmp2 * p2.z);
			float tmp3 = t23 * t3 * (g3.x*p3.x + g3.y*p3.y + g3.z*p3.z);
			dNoise += glm::vec3(tmp3 * p3.x, tmp3 * p3.y, tmp3 * p3.z);
			dNoise *= -8.0f;
			dNoise.x += (t40 * g0.x + t41 * g1.x + t42 * g2.x + t43 * g3.x);
			dNoise.y += (t40 * g0.y + t41 * g1.y + t42 * g2.y + t43 * g3.y);
			dNoise.z += (t40 * g0.z + t41 * g1.z + t42 * g2.z + t43 * g3.z);
			dNoise *= 28.0f;
		}
		return result;
	}

}