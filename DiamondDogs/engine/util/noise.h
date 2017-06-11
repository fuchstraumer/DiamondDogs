#ifndef NOISE_H
#define NOISE_H
#include "stdafx.h"
#include <random>
/*
	Class: Terrain Generator
	Primary terrain generation system for this library/engine.
	Sources:
	http://www.decarpentier.nl/scape-procedural-basics
	http://www.decarpentier.nl/scape-procedural-extensions
	http://www.iquilezles.org/www/articles/morenoise/morenoise.htm
	http://stackoverflow.com/questions/4297024/3d-perlin-noise-analytical-derivative
	https://github.com/Auburns/FastNoise/blob/master/FastNoise.cpp 
	http://webstaff.itn.liu.se/~stegu/simplexnoise/DSOnoises.html
*/

namespace noise {


	/*

	Default values for the fractal noise generators that build on the simplex
	baseline generator

	*/

	// Default values for FBM, constexpr = compile-time constant.
	constexpr double FBM_FREQ = 0.0002;
	constexpr int FBM_OCTAVES = 5;
	constexpr float FBM_LACUN = 2.0f;
	constexpr float FBM_GAIN = 1.5f;

	// Default values for Billow
	constexpr double BILLOW_FREQ = 0.0018;
	constexpr int BILLOW_OCTAVES = 4;
	constexpr float BILLOW_LACUN = 2.50f;
	constexpr float BILLOW_GAIN = 1.20f;

	// Default values for Ridged
	constexpr double RIDGED_FREQ = 0.00008;
	constexpr int RIDGED_OCTAVES = 6;
	constexpr float RIDGED_LACUN = 2.50f;
	constexpr float RIDGED_GAIN = 2.20f;

	// Default values for Swiss noise.
	constexpr double SWISS_FREQ = 0.008;
	constexpr double SWISS_OCTAVES = 7;
	constexpr float SWISS_LACUNARITY = 3.0f;
	constexpr float SWISS_GAIN = 1.5f;

	// Default values for Jordan noise.
	constexpr double JORDAN_FREQ = 0.008;
	constexpr int JORDAN_OCTAVES = 7;
	constexpr float JORDAN_LACUNARITY = 3.0f;
	constexpr float JORDAN_GAIN = 1.50f;
	// Variables specific to jordan.
	constexpr float warp0 = 0.4f;
	constexpr float warp1 = 0.35f;
	constexpr float damp0 = 1.0f;
	constexpr float damp1 = 0.8f;

	/*

	Methods used in various generators

	*/

	// Perlin noise easing curve
	static double fade(double f) {
		return f*f*f*(f*(f * 6 - 15) + 10);
	}
	// Derivative of the perlin noise easing curve
	static double fadeDeriv(double f) {
		return f*f*(f*(30 * f - 60) + 30);
	}

	// Basic linear interp
	static double lerp(double a, double b, double x) {
		return a + x * (b - a);
	}

	/*
	Enums
	*/

	// What are the types of noise we can use?
	enum class NoiseType {
		FBM,
		BILLOW,
		RIDGED_MULTI,
		DECARPIENTIER_SWISS,
		JORDAN,
		iQ,
	};

	// TODO: Could really use a voronoi generator.

	// What will the generator be used for?
	enum class GeneratorUse {
		HEIGHT,
		BIOME,
		TEMPERATURE,
		HUMIDITY,
	};

	class GeneratorBase {
	public:

		// Instantiates a terrain generator, setting the seed and building the hash table
		GeneratorBase(int seed) {
			this->Seed = seed;
			r_gen.seed(this->Seed);
			buildHash();
		}

		// Used to seed the random generator (below)
		int Seed;

		// Mersenne-twister used to shuffle the hash table on initialization of this object.
		std::mt19937 r_gen;

		//double SimplexFBM(int x, int y, int z, double frequency, int octaves, float lacunarity, float gain);
		double SimplexFBM(double x, double y, double freq = FBM_FREQ, int octaves = FBM_OCTAVES, float lac = FBM_LACUN, float gain = FBM_GAIN);

		//double SimplexBillow(int x, int y, int z, double frequency, int octaves, float lacunarity, float gain);
		double SimplexBillow(double x, double y, double freq = BILLOW_FREQ, int octaves = BILLOW_OCTAVES, float lac = BILLOW_LACUN, float gain = BILLOW_GAIN);
		double SimplexBillow3D(const glm::vec3& pos, glm::vec3& deriv, double freq = BILLOW_FREQ, int octaves = BILLOW_OCTAVES, float lac = BILLOW_LACUN, float gain = BILLOW_GAIN) const;

		//double SimplexRidged(int x, int y, int z, double freq, int octaves, float lac, float gain);;
		double SimplexRidged(int x, int y, double freq = RIDGED_FREQ, int octaves = RIDGED_OCTAVES, float lac = RIDGED_LACUN, float gain = RIDGED_GAIN);

		// "Swiss" noise using derivatives to simulate erosion and create better mountains
		double SimplexSwiss(int x, int y, double freq = SWISS_FREQ, int octaves = SWISS_OCTAVES, float lac = SWISS_LACUNARITY, float gain = SWISS_GAIN);

		// Jordan noise - not implemented yet.
		double SimplexJordan(int x, int y, double freq, int octaves, float lac, float gain);

		

	protected:

		// Hash table containing values to be used for gradient vectors
		// Leave as unsigned char - getting this to fit in the cache is optimal
		unsigned char hashTable[512];

		// Builds the hash table containing our 0-255 values used for gradient vectors
		// This approach is psuedo-random (on purpose) and faster than other methods
		// This is relatively expensive, so it should only be called once.
		void buildHash();

		// Faster flooring function than std::floor()
		inline int fastfloor(double x) {
			return x > 0 ? (int)x : (int)x - 1;
		}

		// Finds the dot product of x,y,z and the gradient vector "hash". 
		// Source: http://riven8192.blogspot.com/2010/08/calculate-perlinnoise-twice-as-fast.html //
		// Forcing inline to increase speed (I hope).
		double grad(int hash, double x, double y, double z);

		// Function for finding gradient of simplex noise in 2D
		double sGrad(int hash, double x, double y);

		// Function for finding gradient of simplex noise in 3D
		double sGrad(int hash, double x, double y, double z);

		// Simplex noise gens
		// return the derivatives at x,y if non-null pointers dx,dy are supplied
		double simplex(double x, double y, double* dx, double* dy);

		// Returns value of noise at given point xyz
		double simplex(double x, double y, double z);

		float simplex3D(const glm::vec3 & p, glm::vec3 & dNoise) const;
	};

}
#endif