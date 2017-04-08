#pragma once
#ifndef CNOISE_PLANE_H
#define CNOISE_PLANE_H
#include "../common/CUDA_Include.h"
#include "../modules/Base.h"
namespace cnoise {


	namespace models {

		class Plane {
		public:

			Plane(int width, int height, float min_x, float min_y, float max_x, float max_y, Module3D* src = nullptr);

			Plane(int width, int height, float2 min, float2 max, Module3D* src = nullptr);

			Plane(float min_x, float min_y, float max_x, float max_y, Module3D* src = nullptr);

			Plane(float2 min, float2 max, Module3D* src = nullptr);

			std::vector<Point> GetPoints() const;

			std::vector<float> GetPointValues() const;

			void SaveToPNG(const char* filename) const;

			const Module3D* GetSourceModule() const;

			Module3D* GetSourceModule();

			void SetBounds(float2 _min, float2 _max);

		private:

			void buildPts();

			Module3D* source;

			Point* points;

			float2 max, min;

		};

	}

}

#endif // !NOISE_PLANE_H
