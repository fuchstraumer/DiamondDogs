#ifndef TURBULENCE_CUH
#define TURBULENCE_CUH
#include "../common/CUDA_Include.h"
#include "../generators/FBM.cuh"

void TurbulenceLauncher(float* out, const float* input, const int width, const int height, const cnoise::noise_t noise_type, const int roughness, const int seed, const float strength, const float freq);

void TurbulenceLauncher3D(cnoise::Point* output, const cnoise::Point* input, const int width, const int height, const int roughness, const int seed, const float strength, const float freq);

#endif // !TURBULENCE_CUH
