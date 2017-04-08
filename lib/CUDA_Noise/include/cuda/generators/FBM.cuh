#ifndef FBM_CUH
#define FBM_CUH
#include "../common/CUDA_Include.h"
#include "../noise_generators.cuh"

__device__ float FBM2d_Simplex(float2 point, const float freq, const float lacun, const float persist, const int init_seed, const int octaves);

__device__ float FBM2d(float2 point, const float freq, const float lacun, const float persist, const int init_seed, const int octaves);

void FBM_Launcher(float* out, const int width, const int height, const cnoise::noise_t noise_type, const float2 origin, const float freq, const float lacun, const float persist, const int seed, const int octaves);

void FBM_Launcher3D(cnoise::Point* coords, const int width, const int height, const float freq, const float lacun, const float persist, const int seed, const int octaves);

#endif // !FBM_CUH
