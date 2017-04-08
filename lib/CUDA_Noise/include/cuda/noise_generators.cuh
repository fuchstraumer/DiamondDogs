#ifndef NOISE_GENERATORS_CUH
#define NOISE_GENERATORS_CUH
#include "../common/CUDA_Include.h"
#include "cutil_math.cuh"
/*
	
	NOISE_GENERATORS_CUH

	Defines the base noise generators that are then called from the more
	advanced generator functions: there's generally no reason to call 
	these values alone, or to use them in a kernel by themselves, as raw
	noise values are pretty boring (thus why we use the other "fractal" 
	kernels in the "generators" folder").

	Uses hash functions from accidental noise, to avoid needing obnoxious
	constant-memory LUTs.

	Derivatives are left as pointers defaulted to nullptr to make it easier
	to skip past calculating these unless we absolutely need to.

*/


__device__ float perlin2d(const float px, const float py, const int seed, float2* deriv = nullptr);

__device__ float perlin3d(const float px, const float py, const float pz, const int seed, float3* deriv = nullptr);

__device__ float simplex2d(const float px, const float py, const int seed, float2* deriv = nullptr);

__device__ float simplex3d(const float px, const float py, const float pz, const int seed, float3* deriv = nullptr);

#endif // !NOISE_GENERATORS_CUH
