#include "../stdafx.h"
#include "Noise.h"

// Faster flooring function than std::floor()
__inline int fastfloor(double x) {
	return static_cast<int>(x>0 ? static_cast<int>(x) : static_cast<int>(x) - 1);
}

// Static data - LUT's and the like

/*
* Gradient tables. These could be programmed the Ken Perlin way with
* some clever bit-twiddling, but this is more clear, and not really slower.
*/

static float grad2lut[8][2] = {
	{ -1.0f, -1.0f },{ 1.0f, 0.0f } ,{ -1.0f, 0.0f } ,{ 1.0f, 1.0f } ,
	{ -1.0f, 1.0f } ,{ 0.0f, -1.0f } ,{ 0.0f, 1.0f } ,{ 1.0f, -1.0f }
};

/*
* Gradient directions for 3D.
* These vectors are based on the midpoints of the 12 edges of a cube.
* A larger array of random unit length vectors would also do the job,
* but these 12 (including 4 repeats to make the array length a power
* of two) work better. They are not random, they are carefully chosen
* to represent a small, isotropic set of directions.
*/

static float grad3lut[16][3] = {
	{ 1.0f, 0.0f, 1.0f },{ 0.0f, 1.0f, 1.0f }, // 12 cube edges
	{ -1.0f, 0.0f, 1.0f },{ 0.0f, -1.0f, 1.0f },
	{ 1.0f, 0.0f, -1.0f },{ 0.0f, 1.0f, -1.0f },
	{ -1.0f, 0.0f, -1.0f },{ 0.0f, -1.0f, -1.0f },
	{ 1.0f, -1.0f, 0.0f },{ 1.0f, 1.0f, 0.0f },
	{ -1.0f, 1.0f, 0.0f },{ -1.0f, -1.0f, 0.0f },
	{ 1.0f, 0.0f, 1.0f },{ -1.0f, 0.0f, 1.0f }, // 4 repeats to make 16
	{ 0.0f, 1.0f, -1.0f },{ 0.0f, -1.0f, -1.0f }
};

/*
* Gradient directions for 4D
* I don't even think i could understand this...
*/

static float grad4lut[32][4] = {
	{ 0.0f, 1.0f, 1.0f, 1.0f },{ 0.0f, 1.0f, 1.0f, -1.0f },{ 0.0f, 1.0f, -1.0f, 1.0f },{ 0.0f, 1.0f, -1.0f, -1.0f }, // 32 tesseract edges
	{ 0.0f, -1.0f, 1.0f, 1.0f },{ 0.0f, -1.0f, 1.0f, -1.0f },{ 0.0f, -1.0f, -1.0f, 1.0f },{ 0.0f, -1.0f, -1.0f, -1.0f },
	{ 1.0f, 0.0f, 1.0f, 1.0f },{ 1.0f, 0.0f, 1.0f, -1.0f },{ 1.0f, 0.0f, -1.0f, 1.0f },{ 1.0f, 0.0f, -1.0f, -1.0f },
	{ -1.0f, 0.0f, 1.0f, 1.0f },{ -1.0f, 0.0f, 1.0f, -1.0f },{ -1.0f, 0.0f, -1.0f, 1.0f },{ -1.0f, 0.0f, -1.0f, -1.0f },
	{ 1.0f, 1.0f, 0.0f, 1.0f },{ 1.0f, 1.0f, 0.0f, -1.0f },{ 1.0f, -1.0f, 0.0f, 1.0f },{ 1.0f, -1.0f, 0.0f, -1.0f },
	{ -1.0f, 1.0f, 0.0f, 1.0f },{ -1.0f, 1.0f, 0.0f, -1.0f },{ -1.0f, -1.0f, 0.0f, 1.0f },{ -1.0f, -1.0f, 0.0f, -1.0f },
	{ 1.0f, 1.0f, 1.0f, 0.0f },{ 1.0f, 1.0f, -1.0f, 0.0f },{ 1.0f, -1.0f, 1.0f, 0.0f },{ 1.0f, -1.0f, -1.0f, 0.0f },
	{ -1.0f, 1.0f, 1.0f, 0.0f },{ -1.0f, 1.0f, -1.0f, 0.0f },{ -1.0f, -1.0f, 1.0f, 0.0f },{ -1.0f, -1.0f, -1.0f, 0.0f }
};

// A lookup table to traverse the simplex around a given point in 4D.
// Details can be found where this table is used, in the 4D noise method.
/* TODO: This should not be required, backport it from Bill's GLSL code! */

static unsigned char simplex[64][4] = {
	{ 0,1,2,3 },{ 0,1,3,2 },{ 0,0,0,0 },{ 0,2,3,1 },{ 0,0,0,0 },{ 0,0,0,0 },{ 0,0,0,0 },{ 1,2,3,0 },
	{ 0,2,1,3 },{ 0,0,0,0 },{ 0,3,1,2 },{ 0,3,2,1 },{ 0,0,0,0 },{ 0,0,0,0 },{ 0,0,0,0 },{ 1,3,2,0 },
	{ 0,0,0,0 },{ 0,0,0,0 },{ 0,0,0,0 },{ 0,0,0,0 },{ 0,0,0,0 },{ 0,0,0,0 },{ 0,0,0,0 },{ 0,0,0,0 },
	{ 1,2,0,3 },{ 0,0,0,0 },{ 1,3,0,2 },{ 0,0,0,0 },{ 0,0,0,0 },{ 0,0,0,0 },{ 2,3,0,1 },{ 2,3,1,0 },
	{ 1,0,2,3 },{ 1,0,3,2 },{ 0,0,0,0 },{ 0,0,0,0 },{ 0,0,0,0 },{ 2,0,3,1 },{ 0,0,0,0 },{ 2,1,3,0 },
	{ 0,0,0,0 },{ 0,0,0,0 },{ 0,0,0,0 },{ 0,0,0,0 },{ 0,0,0,0 },{ 0,0,0,0 },{ 0,0,0,0 },{ 0,0,0,0 },
	{ 2,0,1,3 },{ 0,0,0,0 },{ 0,0,0,0 },{ 0,0,0,0 },{ 3,0,1,2 },{ 3,0,2,1 },{ 0,0,0,0 },{ 3,1,2,0 },
	{ 2,1,0,3 },{ 0,0,0,0 },{ 0,0,0,0 },{ 0,0,0,0 },{ 3,1,0,2 },{ 0,0,0,0 },{ 3,2,0,1 },{ 3,2,1,0 } };

// Constants for 2D simplex noise
static const double F2 = 0.366025403; // F2 = 0.5*(sqrt(3.0)-1.0)
static const double G2 = 0.211324865; // G2 = (3.0-Math.sqrt(3.0))/6.0

// End static data, begin static methods

// Methods for getting gradient vector in various dimensions

__inline static void sGrad(const unsigned char hash, float& x, float& y) {
	int h = hash & 7;
	x = grad2lut[h][0];
	y = grad2lut[h][1];
	return;
}

__inline static void sGrad(const unsigned char hash, float& x, float& y, float& z) {
	int h = hash & 15;
	x = grad3lut[h][0];
	y = grad3lut[h][1];
	z = grad3lut[h][2];
	return;
}

__inline static void sGrad(const unsigned char hash, float& x, float& y, float& z, float& w) {
	int h = hash & 31;
	x = grad4lut[h][0];
	y = grad4lut[h][1];
	z = grad4lut[h][2];
	w = grad4lut[h][3];
	return;
}

// Simplex noise gen for 2D with analytical derivatives

double NoiseGenerator::simplex(double x, double y, double* dx, double* dy) {
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
	if (x0>y0) { i1 = 1; j1 = 0; } // lower triangle, XY order: (0,0)->(1,0)->(1,1)
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
		//n0 = t0 * t0 * sGrad(hashTable[ii + hashTable[jj]], static_cast<float>(x0), static_cast<float>(y0));
	}

	double t1 = 0.5 - x1*x1 - y1*y1;
	if (t1 < 0.0) n1 = 0.0;
	else {
		t1 *= t1;
		//n1 = t1 * t1 * sGrad(hashTable[ii + i1 + hashTable[jj + j1]], x1, y1);
	}

	double t2 = 0.5 - x2*x2 - y2*y2;
	if (t2 < 0.0) n2 = 0.0;
	else {
		t2 *= t2;
		//n2 = t2 * t2 * sGrad(hashTable[ii + 1 + hashTable[jj + 1]], x2, y2);
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


double NoiseGenerator::PerlinBillow(int x, int y, int z, double frequency, int octaves, float lacunarity, float gain)
{
	return 0.0;
}
