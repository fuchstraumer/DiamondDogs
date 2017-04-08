#ifndef DECARPIENTIER_SWISS_CUH
#define DECARPIENTIER_SWISS_CUH
#include "../common/CUDA_Include.h"
#include "../noise_generators.cuh"

void DecarpientierSwissLauncher(float* out, int width, int height, cnoise::noise_t noise_type, float2 origin, float freq, float lacun, float persist, int seed, int octaves);

void DecarpientierSwissLauncher3D(cnoise::Point* coords, int width, int height, float freq, float lacun, float persist, int seed, int octaves);

#endif // !DECARPIENTIER_SWISS_CUH
