#ifndef NORMALIZE_CUH
#define NORMALIZE_CUH
#include "../common/CUDA_Include.h"
#include <thrust/device_vector.h>
#include <thrust/reduce.h>

void NormalizeLauncher(float* input, float* output, const int width, const int height);

#endif // !NORMALIZE_CUH
