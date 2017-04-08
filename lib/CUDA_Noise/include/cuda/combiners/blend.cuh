#ifndef BLEND_CUH
#define BLEND_CUH
#include "../common/CUDA_Include.h"

void BlendLauncher(float * output, const float* in0, const float* in1, const float* weight, const int width, const int height);

void BlendLauncher3D(cnoise::Point* output, const cnoise::Point* input0, const cnoise::Point* input1, const cnoise::Point* control, const int width, const int height);

#endif // !BLEND_CUH
