#ifndef CLAMP_CUH
#define CLAMP_CUH
#include "../common/CUDA_Include.h"

void ClampLauncher(float* output, float* input, const int width, const int height, const float low_val, const float high_val);

void ClampLauncher3D(cnoise::Point* output, const cnoise::Point* input, const int width, const int height, const float low_value, const float high_value);

#endif // !CLAMP_CUH
