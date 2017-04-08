#ifndef RIDGED_MULTI_CUH
#define RIDGED_MULTI_CUH

#include "../common/CUDA_Include.h"
#include "../noise_generators.cuh"

void RidgedMultiLauncher(float* out, int width, int height, cnoise::noise_t noise_type, float2 origin, float freq, float lacun, float persist, int seed, int octaves);

void RidgedMultiLauncher3D(cnoise::Point* coords, const int width, const int height, const float freq, const float lacun, const float persist, const int seed, const int octaves);

#endif // !RIDGED_MULTI_CUH
