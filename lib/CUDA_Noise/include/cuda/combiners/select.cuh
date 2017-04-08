#ifndef SELECT_CUH
#define SELECT_CUH
#include "../common/CUDA_Include.h"

void SelectLauncher(float* out, float* select_item, float* subject0, float* subject1, int width, int height, float upper_bound, float lower_bound, float falloff);

#endif // !SELECT_CUH