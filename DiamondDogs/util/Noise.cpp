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
static const float F2 = 0.366025403f; // F2 = 0.5*(sqrt(3.0)-1.0)
static const float G2 = 0.211324865f; // G2 = (3.0-Math.sqrt(3.0))/6.0

// Constants for 3D simplex noise
static const float F3 = .333333333f;
static const float G3 = .166666667f;

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

float NoiseGenerator::simplex(float x, float y, float* dx, float* dy) {
	float n0, n1, n2; /* Noise contributions from the three simplex corners */
	float gx0, gy0, gx1, gy1, gx2, gy2; /* Gradients at simplex corners */
	float t0, t1, t2, x1, x2, y1, y2;
	float t20, t40, t21, t41, t22, t42;
	float temp0, temp1, temp2, noise;

	/* Skew the input space to determine which simplex cell we're in */
	float s = (x + y) * F2; /* Hairy factor for 2D */
	float xs = x + s;
	float ys = y + s;
	int ii, i = fastfloor(xs);
	int jj, j = fastfloor(ys);

	float t = (float)(i + j) * G2;
	float X0 = i - t; /* Unskew the cell origin back to (x,y) space */
	float Y0 = j - t;
	float x0 = x - X0; /* The x,y distances from the cell origin */
	float y0 = y - Y0;

	/* For the 2D case, the simplex shape is an equilateral triangle.
	* Determine which simplex we are in. */
	int i1, j1; /* Offsets for second (middle) corner of simplex in (i,j) coords */
	if (x0 > y0) { i1 = 1; j1 = 0; } /* lower triangle, XY order: (0,0)->(1,0)->(1,1) */
	else { i1 = 0; j1 = 1; }      /* upper triangle, YX order: (0,0)->(0,1)->(1,1) */

								  /* A step of (1,0) in (i,j) means a step of (1-c,-c) in (x,y), and
								  * a step of (0,1) in (i,j) means a step of (-c,1-c) in (x,y), where
								  * c = (3-sqrt(3))/6   */
	x1 = x0 - i1 + G2; /* Offsets for middle corner in (x,y) unskewed coords */
	y1 = y0 - j1 + G2;
	x2 = x0 - 1.0f + 2.0f * G2; /* Offsets for last corner in (x,y) unskewed coords */
	y2 = y0 - 1.0f + 2.0f * G2;

	/* Wrap the integer indices at 256, to avoid indexing perm[] out of bounds */
	ii = i % 256;
	jj = j % 256;

	/* Calculate the contribution from the three corners */
	t0 = 0.5f - x0 * x0 - y0 * y0;
	if (t0 < 0.0f) t40 = t20 = t0 = n0 = gx0 = gy0 = 0.0f; /* No influence */
	else {
		sGrad(hash[ii + hash[jj]], gx0, gy0);
		t20 = t0 * t0;
		t40 = t20 * t20;
		n0 = t40 * (gx0 * x0 + gy0 * y0);
	}

	t1 = 0.5f - x1 * x1 - y1 * y1;
	if (t1 < 0.0f) t21 = t41 = t1 = n1 = gx1 = gy1 = 0.0f; /* No influence */
	else {
		sGrad(hash[ii + i1 + hash[jj + j1]], gx1, gy1);
		t21 = t1 * t1;
		t41 = t21 * t21;
		n1 = t41 * (gx1 * x1 + gy1 * y1);
	}

	t2 = 0.5f - x2 * x2 - y2 * y2;
	if (t2 < 0.0f) t42 = t22 = t2 = n2 = gx2 = gy2 = 0.0f; /* No influence */
	else {
		sGrad(hash[ii + 1 + hash[jj + 1]], gx2, gy2);
		t22 = t2 * t2;
		t42 = t22 * t22;
		n2 = t42 * (gx2 * x2 + gy2 * y2);
	}

	/* Add contributions from each corner to get the final noise value.
	* The result is scaled to return values in the interval [-1,1]. */
	noise = 40.0f * (n0 + n1 + n2);

	/* Compute derivative, if requested by supplying non-null pointers
	* for the last two arguments */
	if ((NULL != dx) && (NULL != dy))
	{
		/*  A straight, unoptimised calculation would be like:
		*    *dnoise_dx = -8.0f * t20 * t0 * x0 * ( gx0 * x0 + gy0 * y0 ) + t40 * gx0;
		*    *dnoise_dy = -8.0f * t20 * t0 * y0 * ( gx0 * x0 + gy0 * y0 ) + t40 * gy0;
		*    *dnoise_dx += -8.0f * t21 * t1 * x1 * ( gx1 * x1 + gy1 * y1 ) + t41 * gx1;
		*    *dnoise_dy += -8.0f * t21 * t1 * y1 * ( gx1 * x1 + gy1 * y1 ) + t41 * gy1;
		*    *dnoise_dx += -8.0f * t22 * t2 * x2 * ( gx2 * x2 + gy2 * y2 ) + t42 * gx2;
		*    *dnoise_dy += -8.0f * t22 * t2 * y2 * ( gx2 * x2 + gy2 * y2 ) + t42 * gy2;
		*/
		temp0 = t20 * t0 * (gx0* x0 + gy0 * y0);
		*dx = temp0 * x0;
		*dy = temp0 * y0;
		temp1 = t21 * t1 * (gx1 * x1 + gy1 * y1);
		*dx += temp1 * x1;
		*dy += temp1 * y1;
		temp2 = t22 * t2 * (gx2* x2 + gy2 * y2);
		*dx += temp2 * x2;
		*dy += temp2 * y2;
		*dx *= -8.0f;
		*dy *= -8.0f;
		*dx += t40 * gx0 + t41 * gx1 + t42 * gx2;
		*dy += t40 * gy0 + t41 * gy1 + t42 * gy2;
		*dx *= 40.0f; /* Scale derivative to match the noise scaling */
		*dy *= 40.0f;
	}
	return noise;
}

float NoiseGenerator::simplex(glm::vec2 & pos, glm::vec2 * deriv){
	return simplex(pos.x, pos.y, &deriv->x, &deriv->y);
}

float NoiseGenerator::simplex(float x, float y, float z, float * dx, float * dy, float * dz){
	float n0, n1, n2, n3; /* Noise contributions from the four simplex corners */
	float noise;          /* Return value */
	float gx0, gy0, gz0, gx1, gy1, gz1; /* Gradients at simplex corners */
	float gx2, gy2, gz2, gx3, gy3, gz3;
	float x1, y1, z1, x2, y2, z2, x3, y3, z3;
	float t0, t1, t2, t3, t20, t40, t21, t41, t22, t42, t23, t43;
	float temp0, temp1, temp2, temp3;

	/* Skew the input space to determine which simplex cell we're in */
	float s = (x + y + z)*F3; /* Very nice and simple skew factor for 3D */
	float xs = x + s;
	float ys = y + s;
	float zs = z + s;
	int ii, i = fastfloor(xs);
	int jj, j = fastfloor(ys);
	int kk, k = fastfloor(zs);

	float t = (float)(i + j + k)*G3;
	float X0 = i - t; /* Unskew the cell origin back to (x,y,z) space */
	float Y0 = j - t;
	float Z0 = k - t;
	float x0 = x - X0; /* The x,y,z distances from the cell origin */
	float y0 = y - Y0;
	float z0 = z - Z0;

	/* For the 3D case, the simplex shape is a slightly irregular tetrahedron.
	* Determine which simplex we are in. */
	int i1, j1, k1; /* Offsets for second corner of simplex in (i,j,k) coords */
	int i2, j2, k2; /* Offsets for third corner of simplex in (i,j,k) coords */

					/* TODO: This code would benefit from a backport from the GLSL version! */
	if (x0 >= y0) {
		if (y0 >= z0)
		{
			i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 1; k2 = 0;
		} /* X Y Z order */
		else if (x0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 0; k2 = 1; } /* X Z Y order */
		else { i1 = 0; j1 = 0; k1 = 1; i2 = 1; j2 = 0; k2 = 1; } /* Z X Y order */
	}
	else { // x0<y0
		if (y0<z0) { i1 = 0; j1 = 0; k1 = 1; i2 = 0; j2 = 1; k2 = 1; } /* Z Y X order */
		else if (x0<z0) { i1 = 0; j1 = 1; k1 = 0; i2 = 0; j2 = 1; k2 = 1; } /* Y Z X order */
		else { i1 = 0; j1 = 1; k1 = 0; i2 = 1; j2 = 1; k2 = 0; } /* Y X Z order */
	}

	/* A step of (1,0,0) in (i,j,k) means a step of (1-c,-c,-c) in (x,y,z),
	* a step of (0,1,0) in (i,j,k) means a step of (-c,1-c,-c) in (x,y,z), and
	* a step of (0,0,1) in (i,j,k) means a step of (-c,-c,1-c) in (x,y,z), where
	* c = 1/6.   */

	x1 = x0 - i1 + G3; /* Offsets for second corner in (x,y,z) coords */
	y1 = y0 - j1 + G3;
	z1 = z0 - k1 + G3;
	x2 = x0 - i2 + 2.0f * G3; /* Offsets for third corner in (x,y,z) coords */
	y2 = y0 - j2 + 2.0f * G3;
	z2 = z0 - k2 + 2.0f * G3;
	x3 = x0 - 1.0f + 3.0f * G3; /* Offsets for last corner in (x,y,z) coords */
	y3 = y0 - 1.0f + 3.0f * G3;
	z3 = z0 - 1.0f + 3.0f * G3;

	/* Wrap the integer indices at 256, to avoid indexing perm[] out of bounds */
	ii = i % 256;
	jj = j % 256;
	kk = k % 256;

	/* Calculate the contribution from the four corners */
	t0 = 0.6f - x0*x0 - y0*y0 - z0*z0;
	if (t0 < 0.0f) n0 = t0 = t20 = t40 = gx0 = gy0 = gz0 = 0.0f;
	else {
		sGrad(hash[ii + hash[jj + hash[kk]]], gx0, gy0, gz0);
		t20 = t0 * t0;
		t40 = t20 * t20;
		n0 = t40 * (gx0 * x0 + gy0 * y0 + gz0 * z0);
	}

	t1 = 0.6f - x1*x1 - y1*y1 - z1*z1;
	if (t1 < 0.0f) n1 = t1 = t21 = t41 = gx1 = gy1 = gz1 = 0.0f;
	else {
		sGrad(hash[ii + i1 + hash[jj + j1 + hash[kk + k1]]], gx1, gy1, gz1);
		t21 = t1 * t1;
		t41 = t21 * t21;
		n1 = t41 * (gx1 * x1 + gy1 * y1 + gz1 * z1);
	}

	t2 = 0.6f - x2*x2 - y2*y2 - z2*z2;
	if (t2 < 0.0f) n2 = t2 = t22 = t42 = gx2 = gy2 = gz2 = 0.0f;
	else {
		sGrad(hash[ii + i2 + hash[jj + j2 + hash[kk + k2]]], gx2, gy2, gz2);
		t22 = t2 * t2;
		t42 = t22 * t22;
		n2 = t42 * (gx2 * x2 + gy2 * y2 + gz2 * z2);
	}

	t3 = 0.6f - x3*x3 - y3*y3 - z3*z3;
	if (t3 < 0.0f) n3 = t3 = t23 = t43 = gx3 = gy3 = gz3 = 0.0f;
	else {
		sGrad(hash[ii + 1 + hash[jj + 1 + hash[kk + 1]]], gx3, gy3, gz3);
		t23 = t3 * t3;
		t43 = t23 * t23;
		n3 = t43 * (gx3 * x3 + gy3 * y3 + gz3 * z3);
	}

	/*  Add contributions from each corner to get the final noise value.
	* The result is scaled to return values in the range [-1,1] */
	noise = 28.0f * (n0 + n1 + n2 + n3);

	/* Compute derivative, if requested by supplying non-null pointers
	* for the last three arguments */
	if ((NULL != dx) && (NULL != dy) && (NULL != dz))
	{
		/*  A straight, unoptimised calculation would be like:
		*     *dnoise_dx = -8.0f * t20 * t0 * x0 * dot(gx0, gy0, gz0, x0, y0, z0) + t40 * gx0;
		*    *dnoise_dy = -8.0f * t20 * t0 * y0 * dot(gx0, gy0, gz0, x0, y0, z0) + t40 * gy0;
		*    *dnoise_dz = -8.0f * t20 * t0 * z0 * dot(gx0, gy0, gz0, x0, y0, z0) + t40 * gz0;
		*    *dnoise_dx += -8.0f * t21 * t1 * x1 * dot(gx1, gy1, gz1, x1, y1, z1) + t41 * gx1;
		*    *dnoise_dy += -8.0f * t21 * t1 * y1 * dot(gx1, gy1, gz1, x1, y1, z1) + t41 * gy1;
		*    *dnoise_dz += -8.0f * t21 * t1 * z1 * dot(gx1, gy1, gz1, x1, y1, z1) + t41 * gz1;
		*    *dnoise_dx += -8.0f * t22 * t2 * x2 * dot(gx2, gy2, gz2, x2, y2, z2) + t42 * gx2;
		*    *dnoise_dy += -8.0f * t22 * t2 * y2 * dot(gx2, gy2, gz2, x2, y2, z2) + t42 * gy2;
		*    *dnoise_dz += -8.0f * t22 * t2 * z2 * dot(gx2, gy2, gz2, x2, y2, z2) + t42 * gz2;
		*    *dnoise_dx += -8.0f * t23 * t3 * x3 * dot(gx3, gy3, gz3, x3, y3, z3) + t43 * gx3;
		*    *dnoise_dy += -8.0f * t23 * t3 * y3 * dot(gx3, gy3, gz3, x3, y3, z3) + t43 * gy3;
		*    *dnoise_dz += -8.0f * t23 * t3 * z3 * dot(gx3, gy3, gz3, x3, y3, z3) + t43 * gz3;
		*/
		temp0 = t20 * t0 * (gx0 * x0 + gy0 * y0 + gz0 * z0);
		*dx = temp0 * x0;
		*dy = temp0 * y0;
		*dz = temp0 * z0;
		temp1 = t21 * t1 * (gx1 * x1 + gy1 * y1 + gz1 * z1);
		*dx += temp1 * x1;
		*dy += temp1 * y1;
		*dz += temp1 * z1;
		temp2 = t22 * t2 * (gx2 * x2 + gy2 * y2 + gz2 * z2);
		*dx += temp2 * x2;
		*dy += temp2 * y2;
		*dz += temp2 * z2;
		temp3 = t23 * t3 * (gx3 * x3 + gy3 * y3 + gz3 * z3);
		*dx += temp3 * x3;
		*dy += temp3 * y3;
		*dz += temp3 * z3;
		*dx *= -8.0f;
		*dy *= -8.0f;
		*dz *= -8.0f;
		*dx += t40 * gx0 + t41 * gx1 + t42 * gx2 + t43 * gx3;
		*dy += t40 * gy0 + t41 * gy1 + t42 * gy2 + t43 * gy3;
		*dz += t40 * gz0 + t41 * gz1 + t42 * gz2 + t43 * gz3;
		*dx *= 28.0f; /* Scale derivative to match the noise scaling */
		*dy *= 28.0f;
		*dz *= 28.0f;
	}
	return noise;
}

float NoiseGenerator::simplex(glm::vec3 & pos, glm::vec3 * deriv){
	return simplex(pos.x, pos.y, pos.z, &deriv->x, &deriv->y, &deriv->z);
}

float NoiseGenerator::SimplexFBM(int x, int y, float freq, int octaves, float lac, float gain){
	float sum = 0;
	float amplitude = 1.0;
	glm::vec2 f; f.x = x * freq;
	f.y = y * freq;
	for (int i = 0; i < octaves; ++i) {
		float n = simplex(f.x, f.y, nullptr, nullptr);
		sum += n*amplitude;
		freq *= lac;
		amplitude *= gain;
	}
	float temp = (sum / amplitude);
	return temp;
}

float NoiseGenerator::SimplexBillow(int x, int y, float freq, int octaves, float lac, float gain){
	float sum = 0;
	float amplitude = 1.0;
	glm::vec2 f;
	f.x = x * freq; f.y = y * freq;
	for (int i = 0; i < octaves; ++i) {
		float n = abs(simplex(f.x, f.y, nullptr, nullptr));
		sum += n*amplitude;
		freq *= lac;
		amplitude *= gain;
	}
	float temp = (sum / amplitude);
	return temp;
}

float NoiseGenerator::SimplexRidged(int x, int y, float freq, int octaves, float lac, float gain){
	float sum = 0;
	float amplitude = 1.0f;
	glm::vec2 f;
	f.x = x * freq;
	f.y = y * freq;
	for (int i = 0; i < octaves; ++i) {
		float n = 1.0f - abs(simplex(x*freq, y*freq, nullptr, nullptr));
		sum += n*amplitude;
		freq *= lac;
		amplitude *= gain;
	}
	float temp = (sum / amplitude);
	return temp;
}

void NoiseGenerator::buildHash(){
	for (int c = 0; c < 255; ++c) {
		this->hash[c] = c;
		this->hash[c + 256] = c;
	}
	std::shuffle(hash, hash + 512, RGen);
}
