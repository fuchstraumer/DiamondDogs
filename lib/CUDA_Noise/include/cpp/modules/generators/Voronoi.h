#pragma once
#ifndef VORONOI_H
#define VORONOI_H
#include "../Base.h"


namespace cnoise {

	namespace generators {


		constexpr float DEFAULT_VORONOI_DISPLACEMENT = 1.0f;
		constexpr float DEFAULT_VORONOI_FREQUENCY = 1.0f;
		constexpr int DEFAULT_VORONOI_SEED = 0;

		class Voronoi : public Module {
		public:
			// Width + height specify output texture size.
			// Seed defines a value to seed the generator with
			// X & Y define the origin of the noise generator
			Voronoi(int width, int height, voronoi_distance_t cell_dist_func = voronoi_distance_t::EUCLIDEAN, voronoi_return_t return_t = voronoi_return_t::DISTANCE, float freq = DEFAULT_VORONOI_FREQUENCY, float displ = DEFAULT_VORONOI_DISPLACEMENT, int seed = DEFAULT_VORONOI_SEED);

			// Get source module count: must be 0, this is a generator and can't have preceding modules.
			virtual size_t GetSourceModuleCount() const override;

			// Launches the kernel and fills this object's surface object with the relevant data.
			virtual void Generate() override;

			// Origin of this noise generator. Keep the seed constant and change this for 
			// continuous "tileable" noise
			std::pair<float, float> Origin;

			voronoi_distance_t CellDistanceType;

			voronoi_return_t ReturnDataType;

			float Displacement, Frequency;
		};


	}

}

#endif // !VORONOI_H
