#pragma once
#ifndef NOISE_H
#define NOISE_H
#include "../../stdafx.h"
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
static const float FBM_FREQ = 0.05f; static const int FBM_OCTAVES = 8;
static const float FBM_LACUN = 4.0f; static const float FBM_GAIN = 1.2f;
static const float BILLOW_FREQ = 0.18f; static const int BILLOW_OCTAVES = 4;
static const float BILLOW_LACUN = 2.50f; static const float BILLOW_GAIN = 1.20f;
static const float RIDGED_FREQ = 0.001f; static const int RIDGED_OCTAVES = 6;
static const float RIDGED_LACUN = 1.50f; static const float RIDGED_GAIN = 1.3f;
static float SWISS_FREQ = 0.3f; static float SWISS_OCTAVES = 6; // This is a slow terrain function because of high octaves
static float SWISS_LACUNARITY = 1.3f; static float SWISS_GAIN = 1.00f;
static const float iQ_FREQ = 0.3f; static const int iQ_OCTAVES = 9;
static const float iQ_LACUN = 1.9f; static const float iQ_GAIN = 1.0f;
static const float JORDAN_FREQ = 1.5f; static const int JORDAN_OCTAVES = 7;
static const float JORDAN_LACUN = 2.0f; static const float JORDAN_GAIN = 1.0f;

class NoiseGenerator {
public:
	// Seed used to seed the noise functions
	int Seed;
	std::mt19937 RGen;
	NoiseGenerator() = default;
	~NoiseGenerator() = default;
	NoiseGenerator(int seed) {
		Seed = seed;
		// Seed the random number generator
		RGen.seed(Seed);
		buildHash();
	}

	// Noise generation functions
	// Fractal-Brownian-Motion perlin noise based terrain gen.
	float PerlinFBM(int x, int y, int z, float frequency, int octaves, float lacunarity, float gain);
	//float SimplexFBM(int x, int y, int z, float frequency, int octaves, float lacunarity, float gain);
	float SimplexFBM(float x, float y, float freq = FBM_FREQ, int octaves = FBM_OCTAVES, float lac = FBM_LACUN, float gain = FBM_GAIN);
	float SimplexFBM_3D(float x, float y, float z, float freq = FBM_FREQ, int octaves = FBM_OCTAVES, float lac = FBM_LACUN, float gain = FBM_GAIN);
	float SimplexFBM_3DBounded(float x, float y, float z, float low, float high, float freq = FBM_FREQ, int octaves = FBM_OCTAVES, float lac = FBM_LACUN, float gain = FBM_GAIN);

	// Billowy perlin generator, takes the absolute value of the perlin gen and generally works at low gain and low freq
	float PerlinBillow(int x, int y, int z, float frequency, int octaves, float lacunarity, float gain);
	//float SimplexBillow(int x, int y, int z, float frequency, int octaves, float lacunarity, float gain);
	float SimplexBillow(int x, int y, float freq = BILLOW_FREQ, int octaves = BILLOW_OCTAVES, float lac = BILLOW_LACUN, float gain = BILLOW_GAIN);
	float SimplexBillow_3D(float x, float y, float z, float freq = BILLOW_FREQ, int octaves = BILLOW_OCTAVES, float lac = BILLOW_LACUN, float gain = BILLOW_GAIN);
	float SimplexBillow_3DBounded(float x, float y, float z, float low, float high, float freq = BILLOW_FREQ, int octaves = BILLOW_OCTAVES, float lac = BILLOW_LACUN, float gain = BILLOW_GAIN);

	// Generates ridges and other bizarre patterns. Currently broken.
	float PerlinRidged(int x, int y, int z, float freq, int octaves, float lac, float gain);
	//float SimplexRidged(int x, int y, int z, float freq, int octaves, float lac, float gain);;
	float SimplexRidged(int x, int y, float freq = RIDGED_FREQ, int octaves = RIDGED_OCTAVES, float lac = RIDGED_LACUN, float gain = RIDGED_GAIN);
	float SimplexRidged_3D(float x, float y, float z, float freq = RIDGED_FREQ, int octaves = RIDGED_OCTAVES, float lac = RIDGED_LACUN, float gain = RIDGED_GAIN);
	float SimplexRidged_3DBounded(float x, float y, float z, float low, float high, float freq = FBM_FREQ, int octaves = FBM_OCTAVES, float lac = FBM_LACUN, float gain = FBM_GAIN);

	// iQ noise
	float Simplex_iQ_3D(float x, float y, float z, float freq = iQ_FREQ, int octaves = iQ_OCTAVES, float lac = iQ_LACUN, float gain = iQ_GAIN);
	float Simplex_iQ_3DBounded(float x, float y, float z, float low, float high, float freq = iQ_FREQ, int octaves = iQ_OCTAVES, float lac = iQ_LACUN, float gain = iQ_GAIN);

	// Jordan noise
	float SimplexJordan_3D(float x, float y, float z, float freq = JORDAN_FREQ, int octaves = JORDAN_OCTAVES, float lac = JORDAN_LACUN, float gain = JORDAN_GAIN);
	float SimplexJordan_3DBounded(float x, float y, float z, float low, float high, float freq = JORDAN_FREQ, int octaves = JORDAN_OCTAVES, float lac = JORDAN_LACUN, float gain = JORDAN_GAIN);

	// Swiss noise
	float SimplexSwiss_3D(float x, float y, float z, float freq = SWISS_FREQ, int octaves = SWISS_OCTAVES, float lac = SWISS_LACUNARITY, float gain = SWISS_GAIN);
	float SimplexSwiss_3DBounded(float x, float y, float z, float low, float high, float freq = SWISS_FREQ, int octaves = SWISS_OCTAVES, float lac = SWISS_LACUNARITY, float gain = SWISS_GAIN);

private:

	// Hash table containing values to be used for gradient vectors
	// Leave as unsigned char - getting this to fit in the cache is optimal
	unsigned char hash[512];

	// Build the hash table for this generator
	void buildHash();

	// Finds the dot product of x,y,z and the gradient vector "hash". 
	// Source: http://riven8192.blogspot.com/2010/08/calculate-perlinnoise-twice-as-fast.html //
	float grad(int hash, float x, float y, float z)
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
	float perlin(float x, float y, float z);

	// Simplex noise generation functions
	// return the derivatives to the pointers given if non-null
	float simplex(float x, float y, float* dx, float* dy);
	float simplex(float x, float y, float z, float* dx, float *dy, float *dz);
	float simplex(float x, float y, float z, float w, float *dx, float *dy, float *dz, float *dw);


};

#endif // !NOISE_H
