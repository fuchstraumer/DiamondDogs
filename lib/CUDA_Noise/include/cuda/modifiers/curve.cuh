#ifndef CURVE_CUH
#define CURVE_CUH
#include "../common/CUDA_Include.h"

void CurveLauncher(float* output, float* input, const int width, const int height, std::vector<cnoise::ControlPoint>& control_points);

void CurveLauncher3D(cnoise::Point* output, const cnoise::Point* input, const int width, const int height, std::vector<cnoise::ControlPoint>& control_pts);

#endif // !CURVE_CUH
