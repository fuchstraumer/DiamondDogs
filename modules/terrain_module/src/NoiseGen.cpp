/* sdnoise1234, Simplex noise with true analytic
 * derivative in 1D to 4D.
 *
 * Copyright © 2003-2012, Stefan Gustavson
 *
 * Contact: stefan.gustavson@gmail.com
 *
 * This library is public domain software, released by the author
 * into the public domain in February 2011. You may do anything
 * you like with it. You may even remove all attributions,
 * but of course I'd appreciate it if you kept my name somewhere.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
#include "NoiseGen.hpp" /* We strictly don't need this, but play nice. */
#include <numeric>
#include <algorithm>
#include <immintrin.h>
#include <smmintrin.h>

inline float FastFloor(float x)
{
    return (x > 0.0f) ? static_cast<int>(x) : static_cast<int>(x) - 1;
}
/*
 * Gradient tables. These could be programmed the Ken Perlin way with
 * some clever bit-twiddling, but this is more clear, and not really slower.
 */
constexpr static float grad2lut[8][2] = {
  { -1.0f, -1.0f }, { 1.0f, 0.0f } , { -1.0f, 0.0f } , { 1.0f, 1.0f } ,
  { -1.0f, 1.0f } , { 0.0f, -1.0f } , { 0.0f, 1.0f } , { 1.0f, -1.0f }
};

/*
 * Gradient directions for 3D.
 * These vectors are based on the midpoints of the 12 edges of a cube.
 * A larger array of random unit length vectors would also do the job,
 * but these 12 (including 4 repeats to make the array length a power
 * of two) work better. They are not random, they are carefully chosen
 * to represent a small, isotropic set of directions.
 */

constexpr static float grad3lut[16][3] = {
  { 1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 1.0f }, // 12 cube edges
  { -1.0f, 0.0f, 1.0f }, { 0.0f, -1.0f, 1.0f },
  { 1.0f, 0.0f, -1.0f }, { 0.0f, 1.0f, -1.0f },
  { -1.0f, 0.0f, -1.0f }, { 0.0f, -1.0f, -1.0f },
  { 1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f },
  { -1.0f, 1.0f, 0.0f }, { -1.0f, -1.0f, 0.0f },
  { 1.0f, 0.0f, 1.0f }, { -1.0f, 0.0f, 1.0f }, // 4 repeats to make 16
  { 0.0f, 1.0f, -1.0f }, { 0.0f, -1.0f, -1.0f }
};

constexpr static float grad4lut[32][4] = {
  { 0.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f, 1.0f, -1.0f }, { 0.0f, 1.0f, -1.0f, 1.0f }, { 0.0f, 1.0f, -1.0f, -1.0f }, // 32 tesseract edges
  { 0.0f, -1.0f, 1.0f, 1.0f }, { 0.0f, -1.0f, 1.0f, -1.0f }, { 0.0f, -1.0f, -1.0f, 1.0f }, { 0.0f, -1.0f, -1.0f, -1.0f },
  { 1.0f, 0.0f, 1.0f, 1.0f }, { 1.0f, 0.0f, 1.0f, -1.0f }, { 1.0f, 0.0f, -1.0f, 1.0f }, { 1.0f, 0.0f, -1.0f, -1.0f },
  { -1.0f, 0.0f, 1.0f, 1.0f }, { -1.0f, 0.0f, 1.0f, -1.0f }, { -1.0f, 0.0f, -1.0f, 1.0f }, { -1.0f, 0.0f, -1.0f, -1.0f },
  { 1.0f, 1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f, 0.0f, -1.0f }, { 1.0f, -1.0f, 0.0f, 1.0f }, { 1.0f, -1.0f, 0.0f, -1.0f },
  { -1.0f, 1.0f, 0.0f, 1.0f }, { -1.0f, 1.0f, 0.0f, -1.0f }, { -1.0f, -1.0f, 0.0f, 1.0f }, { -1.0f, -1.0f, 0.0f, -1.0f },
  { 1.0f, 1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, -1.0f, 0.0f }, { 1.0f, -1.0f, 1.0f, 0.0f }, { 1.0f, -1.0f, -1.0f, 0.0f },
  { -1.0f, 1.0f, 1.0f, 0.0f }, { -1.0f, 1.0f, -1.0f, 0.0f }, { -1.0f, -1.0f, 1.0f, 0.0f }, { -1.0f, -1.0f, -1.0f, 0.0f }
};

  // A lookup table to traverse the simplex around a given point in 4D.
  // Details can be found where this table is used, in the 4D noise method.
  /* TODO: This should not be required, backport it from Bill's GLSL code! */
constexpr static unsigned char simplex[64][4] = {
  {0,1,2,3},{0,1,3,2},{0,0,0,0},{0,2,3,1},{0,0,0,0},{0,0,0,0},{0,0,0,0},{1,2,3,0},
  {0,2,1,3},{0,0,0,0},{0,3,1,2},{0,3,2,1},{0,0,0,0},{0,0,0,0},{0,0,0,0},{1,3,2,0},
  {0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},
  {1,2,0,3},{0,0,0,0},{1,3,0,2},{0,0,0,0},{0,0,0,0},{0,0,0,0},{2,3,0,1},{2,3,1,0},
  {1,0,2,3},{1,0,3,2},{0,0,0,0},{0,0,0,0},{0,0,0,0},{2,0,3,1},{0,0,0,0},{2,1,3,0},
  {0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},
  {2,0,1,3},{0,0,0,0},{0,0,0,0},{0,0,0,0},{3,0,1,2},{3,0,2,1},{0,0,0,0},{3,1,2,0},
  {2,1,0,3},{0,0,0,0},{0,0,0,0},{0,0,0,0},{3,1,0,2},{0,0,0,0},{3,2,0,1},{3,2,1,0}};

/* --------------------------------------------------------------------- */

static void grad3(int32_t hash, float& gx, float& gy, float& gz) {
    const int32_t h = hash & 15;
    gx = grad3lut[h][0];
    gy = grad3lut[h][1];
    gz = grad3lut[h][2];
}

NoiseGen::NoiseGen() : randomDvc(), rng(randomDvc()) {
	std::iota(permutationArray.begin(), permutationArray.begin() + 256, 0);
	std::iota(permutationArray.begin() + 256, permutationArray.end(), 0);
	std::shuffle(permutationArray.begin(), permutationArray.end(), rng);
}

void NoiseGen::Seed(const size_t seed) noexcept
{
    rng.seed(seed);
}

NoiseGen& NoiseGen::Get()
{
    static NoiseGen generator;
    return generator;
}

inline __m128 horizontalSum(__m128 v)
{
    __m128 shuffled = _mm_movehdup_ps(v);
    __m128 sums = _mm_add_ps(v, shuffled);
    shuffled = _mm_movehl_ps(shuffled, sums);
    return _mm_add_ss(sums, shuffled);
}

/*
float NoiseGen::SimplexNoiseWithDerivative3D(float x, float y, float z, float* deriv) const
{
    constexpr float F3 = 0.3333333333f;
    // we make sure to set 0.0 for the fourth because it will let us zero out key quantities before use.
    const __m128 registerF3 = _mm_set_ps(F3, F3, F3, 0.0f);
    constexpr float G3 = 0.1666666667f;
    const __m128 registerG3 = _mm_set_ps(G3, G3, G3, 0.0f);
    const __m128 zeroOutMask = _mm_set_ps(1.0f, 1.0f, 1.0f, 0.0f);
    // stuff it into our vectors
    const float zeroPad = 0.0f;

    __m128 xyzw = _mm_set_ps(x, y, z, zeroPad);
    __m128 xyzSum = horizontalSum(xyzw);
    __m128 s = _mm_mul_ps(xyzSum, registerF3);
    __m128 xyzwS = _mm_add_ps(xyzw, s);

    __m128 ijk = _mm_round_ps(xyzwS, _MM_FROUND_FLOOR);
    ijk = _mm_mul_ps(ijk, zeroOutMask);
    __m128 tVec = horizontalSum(ijk);
    tVec = _mm_mul_ps(tVec, registerG3);
    __m128 _XYZW = _mm_sub_ps(ijk, tVec);
    __m128 x0y0z0 = _mm_sub_ps(xyzw, _XYZW);

    float x0, y0, z0;
    // aligned for a faster store
    alignas(16) float comparisonBuffers[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
    _mm_store_ps(comparisonBuffers, x0y0z0);

    __m128 i1j1k1;
    __m128 i2j2k2;

    if (comparisonBuffers[0] >= comparisonBuffers[1])
    {
        if (comparisonBuffers[1] >= comparisonBuffers[2])
        {
            i1j1k1 = _mm_set_ps(1.0f, 0.0f, 0.0f, 0.0f);
            i2j2k2 = _mm_set_ps(1.0f, 1.0f, 0.0f, 0.0f);
        }
        else if (comparisonBuffers[0] >= comparisonBuffers[2])
        {
            i1j1k1 = _mm_set_ps(1.0f, 0.0f, 0.0f, 0.0f);
            i2j2k2 = _mm_set_ps(1.0f, 0.0f, 1.0f, 0.0f);
        }
        else
        {
            i1j1k1 = _mm_set_ps(0.0f, 0.0f, 1.0f, 0.0f);
            i2j2k2 = _mm_set_ps(1.0f, 0.0f, 1.0f, 0.0f);
        }
    }
    else
    {
        if (comparisonBuffers[1] < comparisonBuffers[2])
        {
            i1j1k1 = _mm_set_ps(0.0f, 0.0f, 1.0f, 0.0f);
            i2j2k2 = _mm_set_ps(0.0f, 1.0f, 1.0f, 0.0f);
        }
        else if (comparisonBuffers[0] < comparisonBuffers[2])
        {
            i1j1k1 = _mm_set_ps(0.0f, 1.0f, 0.0f, 0.0f);
            i2j2k2 = _mm_set_ps(0.0f, 1.0f, 1.0f, 0.0f);
        }
        else
        {
            i1j1k1 = _mm_set_ps(0.0f, 1.0f, 0.0f, 0.0f);
            i2j2k2 = _mm_set_ps(1.0f, 1.0f, 0.0f, 0.0f);
        }
    }

    __m128 x1y1z1 = _mm_sub_ps(x0y0z0, _mm_add_ps(i1j1k1, registerG3));
    __m128 x2y2z2 = _mm_sub_ps(x0y0z0, _mm_add_ps(i1j1k1, _mm_mul_ps(_mm_set1_ps(2.0f), registerG3)));
    __m128 x3y3z3 = _mm_sub_ps(x0y0z0, _mm_add_ps(_mm_set1_ps(-1.0f), _mm_mul_ps(_mm_set1_ps(3.0f), registerG3)));

    alignas(16) float indicesManual[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
    _mm_store_ps(indicesManual, ijk);

    const int32_t ii = std::abs(static_cast<int32_t>(indicesManual[0]) % 256);
    const int32_t jj = std::abs(static_cast<int32_t>(indicesManual[1]) % 256);
    const int32_t kk = std::abs(static_cast<int32_t>(indicesManual[2]) % 256);

    alignas(16) float coordsBuffer[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
    _mm_store_ps(coordsBuffer, x0y0z0);
    const float x0 = coordsBuffer[0];
    const float y1 = coordsBuffer[1];
    const float z1 = coordsBuffer[2];
    _mm_store_ps(coordsBuffer, x1y1z1);
    const float x1 = coordsBuffer[0];
    const float y1 = coordsBuffer[1];
    const float z1 = coordsBuffer[2];
    _mm_store_ps(coordsBuffer, x2y2z2);
    const float x2 = coordsBuffer[0];
    const float y2 = coordsBuffer[1];
    const float z2 = coordsBuffer[2];
    _mm_store_ps(coordsBuffer, x3y3z3);
    const float x3 = coordsBuffer[0];
    const float y3 = coordsBuffer[1];
    const float z3 = coordsBuffer[2];

    float n0, t0, tSq0, tExp0, gx0, gy0, gz0;
    _mm_mul_ps(x0y0z0, x0y0z0);
    _mm_store_ps(coordsBuffer, x0y0z0);
    t0 = 0.6f - coordsBuffer[0] - coordsBuffer[1] - coordsBuffer[2];
    if (t0 < 0.0f)
    {
        n0 = t0 = tSq0 = tExp0 = gx0 = gy0 = gz0 = 0.0f;
    }
    else
    {
        const int32_t idx = permutationArray[ii + permutationArray[jj + permutationArray[kk]]];
        tSq0 = t0 * t0;
        tExp0 = tSq0 * tSq0;
        grad3(idx, gx0, gy0, gz0);
        n0 = tExp0 * (gx0 * x0 + gy0 * y0 + gz0 + z0);
    }

    float n1, t1, tSq1, tExp1, gx1, gy1, gz1;
    _mm_mul_ps(x1y1z1, x1y1z1);
    _mm_store_ps(coordsBuffer, x1y1z1);
    float t1 = 0.6f - coordsBuffer[0] - coordsBuffer[1] - coordsBuffer[2];
    if (t1 < 0.0f)
    {
        n1 = t1 = tSq1 = tExp1 = gx1 = gy1 = gz1 = 0.0f;
    }
    else
    {
        //const int32_t idx = 
    }

    // we square everything before storing it to the buffer for the linear op
    _mm_mul_ps(x2y2z2, x2y2z2);
    _mm_store_ps(coordsBuffer, x2y2z2);
    float t2 = 0.6f - coordsBuffer[0] - coordsBuffer[1] - coordsBuffer[2];
    _mm_mul_ps(x3y3z3, x3y3z3);
    _mm_store_ps(coordsBuffer, x3y3z3);
    float t3 = 0.6f - coordsBuffer[0] - coordsBuffer[1] - coordsBuffer[2];


    return 0.0f;
}
*/

/* Skewing factors for 3D simplex grid:
 * F3 = 1/3
 * G3 = 1/6 */
constexpr float F3 = 0.333333333f;
constexpr float G3 = 0.166666667f;

/** 3D simplex noise with derivatives.
 * If the last tthree arguments are not null, the analytic derivative
 * (the 3D gradient of the scalar noise field) is also calculated.
 */
float NoiseGen::SimplexNoiseWithDerivative3D(float x, float y, float z, float* deriv = nullptr) const
{
    float n0, n1, n2, n3; /* Noise contributions from the four simplex corners */
    float noise;          /* Return value */
    float gx0, gy0, gz0, gx1, gy1, gz1; /* Gradients at simplex corners */
    float gx2, gy2, gz2, gx3, gy3, gz3;
    float x1, y1, z1, x2, y2, z2, x3, y3, z3;
    float t0, t1, t2, t3, t20, t40, t21, t41, t22, t42, t23, t43;
    float temp0, temp1, temp2, temp3;

    /* Skew the input space to determine which simplex cell we're in */
    float s = (x+y+z)*F3; /* Very nice and simple skew factor for 3D */
    float xs = x+s;
    float ys = y+s;
    float zs = z+s;
    int ii, i = FastFloor(xs);
    int jj, j = FastFloor(ys);
    int kk, k = FastFloor(zs);

    float t = (float)(i+j+k)*G3; 
    float X0 = i-t; /* Unskew the cell origin back to (x,y,z) space */
    float Y0 = j-t;
    float Z0 = k-t;
    float x0 = x-X0; /* The x,y,z distances from the cell origin */
    float y0 = y-Y0;
    float z0 = z-Z0;

    /* For the 3D case, the simplex shape is a slightly irregular tetrahedron.
     * Determine which simplex we are in. */
    int i1, j1, k1; /* Offsets for second corner of simplex in (i,j,k) coords */
    int i2, j2, k2; /* Offsets for third corner of simplex in (i,j,k) coords */

    /* TODO: This code would benefit from a backport from the GLSL version! */
    if(x0>=y0) {
      if(y0>=z0)
        { i1=1; j1=0; k1=0; i2=1; j2=1; k2=0; } /* X Y Z order */
        else if(x0>=z0) { i1=1; j1=0; k1=0; i2=1; j2=0; k2=1; } /* X Z Y order */
        else { i1=0; j1=0; k1=1; i2=1; j2=0; k2=1; } /* Z X Y order */
      }
    else { // x0<y0
      if(y0<z0) { i1=0; j1=0; k1=1; i2=0; j2=1; k2=1; } /* Z Y X order */
      else if(x0<z0) { i1=0; j1=1; k1=0; i2=0; j2=1; k2=1; } /* Y Z X order */
      else { i1=0; j1=1; k1=0; i2=1; j2=1; k2=0; } /* Y X Z order */
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
    ii = abs(i % 256);
    jj = abs(j % 256);
    kk = abs(k % 256);

    /* Calculate the contribution from the four corners */
    t0 = 0.6f - x0*x0 - y0*y0 - z0*z0;
    if(t0 < 0.0f) n0 = t0 = t20 = t40 = gx0 = gy0 = gz0 = 0.0f;
    else {
      grad3( permutationArray[ii + permutationArray[jj + permutationArray[kk]]], gx0, gy0, gz0 );
      t20 = t0 * t0;
      t40 = t20 * t20;
      n0 = t40 * ( gx0 * x0 + gy0 * y0 + gz0 * z0 );
    }

    t1 = 0.6f - x1*x1 - y1*y1 - z1*z1;
    if(t1 < 0.0f) n1 = t1 = t21 = t41 = gx1 = gy1 = gz1 = 0.0f;
    else {
      grad3( permutationArray[ii + i1 + permutationArray[jj + j1 + permutationArray[kk + k1]]], gx1, gy1, gz1 );
      t21 = t1 * t1;
      t41 = t21 * t21;
      n1 = t41 * ( gx1 * x1 + gy1 * y1 + gz1 * z1 );
    }

    t2 = 0.6f - x2*x2 - y2*y2 - z2*z2;
    if(t2 < 0.0f) n2 = t2 = t22 = t42 = gx2 = gy2 = gz2 = 0.0f;
    else {
      grad3( permutationArray[ii + i2 + permutationArray[jj + j2 + permutationArray[kk + k2]]], gx2, gy2, gz2 );
      t22 = t2 * t2;
      t42 = t22 * t22;
      n2 = t42 * ( gx2 * x2 + gy2 * y2 + gz2 * z2 );
    }

    t3 = 0.6f - x3*x3 - y3*y3 - z3*z3;
    if(t3 < 0.0f) n3 = t3 = t23 = t43 = gx3 = gy3 = gz3 = 0.0f;
    else {
      grad3( permutationArray[ii + 1 + permutationArray[jj + 1 + permutationArray[kk + 1]]], gx3, gy3, gz3 );
      t23 = t3 * t3;
      t43 = t23 * t23;
      n3 = t43 * ( gx3 * x3 + gy3 * y3 + gz3 * z3 );
    }

    /*  Add contributions from each corner to get the final noise value.
     * The result is scaled to return values in the range [-1,1] */
    noise = 28.0f * (n0 + n1 + n2 + n3);

    /* Compute derivative, if requested by supplying non-null pointers
     * for the last three arguments */
    if (deriv != nullptr) {
	/*  A straight, unoptimised calculation would be like:
     *     *deriv->x = -8.0f * t20 * t0 * x0 * dot(gx0, gy0, gz0, x0, y0, z0) + t40 * gx0;
     *    *deriv->y = -8.0f * t20 * t0 * y0 * dot(gx0, gy0, gz0, x0, y0, z0) + t40 * gy0;
     *    *deriv->z = -8.0f * t20 * t0 * z0 * dot(gx0, gy0, gz0, x0, y0, z0) + t40 * gz0;
     *    *deriv->x += -8.0f * t21 * t1 * x1 * dot(gx1, gy1, gz1, x1, y1, z1) + t41 * gx1;
     *    *deriv->y += -8.0f * t21 * t1 * y1 * dot(gx1, gy1, gz1, x1, y1, z1) + t41 * gy1;
     *    *deriv->z += -8.0f * t21 * t1 * z1 * dot(gx1, gy1, gz1, x1, y1, z1) + t41 * gz1;
     *    *deriv->x += -8.0f * t22 * t2 * x2 * dot(gx2, gy2, gz2, x2, y2, z2) + t42 * gx2;
     *    *deriv->y += -8.0f * t22 * t2 * y2 * dot(gx2, gy2, gz2, x2, y2, z2) + t42 * gy2;
     *    *deriv->z += -8.0f * t22 * t2 * z2 * dot(gx2, gy2, gz2, x2, y2, z2) + t42 * gz2;
     *    *deriv->x += -8.0f * t23 * t3 * x3 * dot(gx3, gy3, gz3, x3, y3, z3) + t43 * gx3;
     *    *deriv->y += -8.0f * t23 * t3 * y3 * dot(gx3, gy3, gz3, x3, y3, z3) + t43 * gy3;
     *    *deriv->z += -8.0f * t23 * t3 * z3 * dot(gx3, gy3, gz3, x3, y3, z3) + t43 * gz3;
     */
        temp0 = t20 * t0 * ( gx0 * x0 + gy0 * y0 + gz0 * z0 );
        deriv[0] = temp0 * x0;
        deriv[1] = temp0 * y0;
        deriv[2] = temp0 * z0;
        temp1 = t21 * t1 * ( gx1 * x1 + gy1 * y1 + gz1 * z1 );
        deriv[0] += temp1 * x1;
        deriv[1] += temp1 * y1;
        deriv[2] += temp1 * z1;
        temp2 = t22 * t2 * ( gx2 * x2 + gy2 * y2 + gz2 * z2 );
        deriv[0] += temp2 * x2;
        deriv[1] += temp2 * y2;
        deriv[2] += temp2 * z2;
        temp3 = t23 * t3 * ( gx3 * x3 + gy3 * y3 + gz3 * z3 );
        deriv[0] += temp3 * x3;
        deriv[1] += temp3 * y3;
        deriv[2] += temp3 * z3;
        deriv[0] *= -8.0f;
        deriv[1] *= -8.0f;
        deriv[2] *= -8.0f;
        deriv[0] += t40 * gx0 + t41 * gx1 + t42 * gx2 + t43 * gx3;
        deriv[1] += t40 * gy0 + t41 * gy1 + t42 * gy2 + t43 * gy3;
        deriv[2] += t40 * gz0 + t41 * gz1 + t42 * gz2 + t43 * gz3;
        deriv[0] *= 28.0f; /* Scale derivative to match the noise scaling */
        deriv[1] *= 28.0f;
        deriv[2] *= 28.0f;
      }

    return noise;
}

