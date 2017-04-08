#pragma once
#ifndef CNOISE_SPHERE_H
#define CNOISE_SPHERE_H

#include "../common/CUDA_Include.h"
#include "../modules/Base.h"

namespace cnoise {

	namespace models {

		/*
		
			class - Sphere

			Builds the collection of data points to evaluate the noise functions at to 
			get the noise data required for a sphere. Specify lattitudional/longitudional 
			extents, radius, and sample points per meter before calling constructor.
		
		*/
	
		class Sphere {
		public:

			// Constructs sphere object with specified bounds, where "width" and "height" specifiy the number of points in the grid of this sphere (affecting number of samples)
			Sphere(size_t width, size_t height, float east_lon_bound = 0.0f, float west_lon_bound = 0.0f, float south_latt_bound = 0.0f, float north_latt_bound = 0.0f);

			Sphere(Module3D* source, float east_lon_bound = 180.0f, float west_lon_bound = -180.0f, float south_latt_bound = -90.0f, float north_latt_bound = 90.0f);
			
			~Sphere();

			void SaveToPNG(const char * filename) const;

			// Builds data, retrieving noise values from "source" at each location in "coords".
			void Build();

			void SetSourceModule(Module3D* src);

			void SetEasternBound(float _east);

			void SetWesternBound(float _west);

			void SetSouthernBound(float _south);

			void SetNorthernBound(float _north);

			float GetEasternBound() const;

			float GetWesternBound() const;

			float GetSouthernBound() const;

			float GetNorthernBound() const;

		private:

			// Coordinate boundaries. 
			// Southern and northern coordinates cannot exceed -90 and 90, respectively.
			// In turn, east and west coords cannot be greater than -180 and 180.
			float east, west, south, north;

			// Module to send coords to.
			Module3D* source;

			GeoCoord* points;

			// Dimensions of grid we map to
			int2 dimensions;
		};

	}

}

#endif // !SPHERE_H
