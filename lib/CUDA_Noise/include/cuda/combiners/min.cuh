#ifndef MIN_CUH
#define MIN_CUH
#include "../common/CUDA_Include.h"

void MinLauncher(float *output, const float* in0, const float* in1, const int width, const int height);

void MinLauncher3D(cnoise::Point* output, const cnoise::Point* in0, const cnoise::Point* in1, const int width, const int height);

#endif // !MIN_CUH
