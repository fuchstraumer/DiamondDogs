#include "stdafx.h"
#include "noise.h"

namespace noise {

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

}