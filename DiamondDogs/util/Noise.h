#pragma once
#ifndef NOISE_H
#define NOISE_H
#include <random>
/*
	Class: Noise Generator
	Primary noise generation system for this library/engine.
	Sources:
	http://www.decarpentier.nl/scape-procedural-basics
	http://www.decarpentier.nl/scape-procedural-extensions
	http://www.iquilezles.org/www/articles/morenoise/morenoise.htm
	http://stackoverflow.com/questions/4297024/3d-perlin-noise-analytical-derivative
	https://github.com/Auburns/FastNoise/blob/master/FastNoise.cpp
	http://webstaff.itn.liu.se/~stegu/simplexnoise/DSOnoises.html
*/

// Load of default values for noise gens
static const double FBM_FREQ = 0.0002; static const int FBM_OCTAVES = 5;
static const float FBM_LACUN = 2.0f; static const float FBM_GAIN = 1.5f;
static const double BILLOW_FREQ = 0.0018; static const int BILLOW_OCTAVES = 4;
static const float BILLOW_LACUN = 2.50f; static const float BILLOW_GAIN = 1.20f;
static const double RIDGED_FREQ = 0.00008; static const int RIDGED_OCTAVES = 6;
static const float RIDGED_LACUN = 2.50f; static const float RIDGED_GAIN = 2.20f;
static double SWISS_FREQ = 0.008; static double SWISS_OCTAVES = 7; // This is a slow terrain function because of high octaves
static float SWISS_LACUNARITY = 3.0f; static float SWISS_GAIN = 1.5f;

class NoiseGenerator {
public:
	// Seed used to seed the noise functions
	int Seed;
	std::mt19937 RGen;

	NoiseGenerator(int seed) {
		Seed = seed;
		// Seed the random number generator
		RGen.seed(Seed);
		buildHash();
	}

	// Noise generation functions
	// Fractal-Brownian-Motion perlin noise based terrain gen.
	double PerlinFBM(int x, int y, int z, double frequency, int octaves, float lacunarity, float gain);
	//double SimplexFBM(int x, int y, int z, double frequency, int octaves, float lacunarity, float gain);
	double SimplexFBM(int x, int y, double freq = FBM_FREQ, int octaves = FBM_OCTAVES, float lac = FBM_LACUN, float gain = FBM_GAIN);

	// Billowy perlin generator, takes the absolute value of the perlin gen and generally works at low gain and low freq
	double PerlinBillow(int x, int y, int z, double frequency, int octaves, float lacunarity, float gain);
	//double SimplexBillow(int x, int y, int z, double frequency, int octaves, float lacunarity, float gain);
	double SimplexBillow(int x, int y, double freq = BILLOW_FREQ, int octaves = BILLOW_OCTAVES, float lac = BILLOW_LACUN, float gain = BILLOW_GAIN);

	// Generates ridges and other bizarre patterns. Currently broken.
	double PerlinRidged(int x, int y, int z, double freq, int octaves, float lac, float gain);
	//double SimplexRidged(int x, int y, int z, double freq, int octaves, float lac, float gain);;
	double SimplexRidged(int x, int y, double freq = RIDGED_FREQ, int octaves = RIDGED_OCTAVES, float lac = RIDGED_LACUN, float gain = RIDGED_GAIN);

private:

	// Hash table containing values to be used for gradient vectors
	// Leave as unsigned char - getting this to fit in the cache is optimal
	unsigned char hashTable[512];

	// Build the hash table for this generator
	void buildHash();

	// Finds the dot product of x,y,z and the gradient vector "hash". 
	// Source: http://riven8192.blogspot.com/2010/08/calculate-perlinnoise-twice-as-fast.html //
	double grad(int hash, double x, double y, double z)
	{
		switch (hash & 0xF)
		{
		case 0x0: return  x + y;
		case 0x1: return -x + y;
		case 0x2: return  x - y;
		case 0x3: return -x - y;
		case 0x4: return  x + z;
		case 0x5: return -x + z;
		case 0x6: return  x - z;
		case 0x7: return -x - z;
		case 0x8: return  y + z;
		case 0x9: return -y + z;
		case 0xA: return  y - z;
		case 0xB: return -y - z;
		case 0xC: return  y + x;
		case 0xD: return -y + z;
		case 0xE: return  y - x;
		case 0xF: return -y - z;
		default: return 0; // never happens
		}
	}

	// Perlin noise generation function
	double perlin(double x, double y, double z);

	// Simplex noise generation functions
	// return the derivatives to the pointers given if non-null
	double simplex(double x, double y, double* dx, double* dy);
	double simplex(const glm::dvec2& pos, const glm::dvec2* deriv);
	double simplex(double x, double y, double z, double* dx, double *dy, double *dz);
	double simplex(const glm::dvec3& pos, const glm::dvec3* deriv);
	double simplex(double x, double y, double z, double w, double *dx, double *dy, double *dz, double *dw);
	double simplex(const glm::dvec4& pos, const glm::dvec4* deriv);

};

#endif // !NOISE_H
