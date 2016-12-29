#ifndef NOISE_GEN_H
#define NOISE_GEN_H

/*
	Class: Noise Generator
	Able to generate simplex noise along with a number of noise variants
    that use the simplex generator as the base.
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
static float SWISS_FREQ = 0.3f; static float SWISS_OCTAVES = 6;
static float SWISS_LACUNARITY = 1.3f; static float SWISS_GAIN = 1.00f;

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
	float SimplexFBM(float x, float y, float freq = FBM_FREQ, int octaves = FBM_OCTAVES, float lac = FBM_LACUN, float gain = FBM_GAIN);
	float SimplexFBM_3D(float x, float y, float z, float freq = FBM_FREQ, int octaves = FBM_OCTAVES, float lac = FBM_LACUN, float gain = FBM_GAIN);
	float SimplexFBM_3DBounded(float x, float y, float z, float low, float high, float freq = FBM_FREQ, int octaves = FBM_OCTAVES, float lac = FBM_LACUN, float gain = FBM_GAIN);

	// Billowy perlin generator, takes the absolute value of the perlin gen and generally works at low gain and low freq
	float SimplexBillow(int x, int y, float freq = BILLOW_FREQ, int octaves = BILLOW_OCTAVES, float lac = BILLOW_LACUN, float gain = BILLOW_GAIN);
	float SimplexBillow_3D(float x, float y, float z, float freq = BILLOW_FREQ, int octaves = BILLOW_OCTAVES, float lac = BILLOW_LACUN, float gain = BILLOW_GAIN);
	float SimplexBillow_3DBounded(float x, float y, float z, float low, float high, float freq = BILLOW_FREQ, int octaves = BILLOW_OCTAVES, float lac = BILLOW_LACUN, float gain = BILLOW_GAIN);

	// Generates ridges and other bizarre patterns. Currently broken.
	float SimplexRidged(int x, int y, float freq = RIDGED_FREQ, int octaves = RIDGED_OCTAVES, float lac = RIDGED_LACUN, float gain = RIDGED_GAIN);
	float SimplexRidged_3D(float x, float y, float z, float freq = RIDGED_FREQ, int octaves = RIDGED_OCTAVES, float lac = RIDGED_LACUN, float gain = RIDGED_GAIN);
	float SimplexRidged_3DBounded(float x, float y, float z, float low, float high, float freq = FBM_FREQ, int octaves = FBM_OCTAVES, float lac = FBM_LACUN, float gain = FBM_GAIN);

	// Swiss noise
	float SimplexSwiss_3D(float x, float y, float z, float freq = SWISS_FREQ, int octaves = SWISS_OCTAVES, float lac = SWISS_LACUNARITY, float gain = SWISS_GAIN);
	float SimplexSwiss_3DBounded(float x, float y, float z, float low, float high, float freq = SWISS_FREQ, int octaves = SWISS_OCTAVES, float lac = SWISS_LACUNARITY, float gain = SWISS_GAIN);

private:

	// Hash table containing values to be used for gradient vectors
	// Leave as unsigned char - getting this to fit in the cache is optimal
	unsigned char hash[512];

	// Build the hash table for this generator
	void buildHash();
    
	// Simplex noise generation functions
	// return the derivatives to the pointers given if non-null
	float simplex(float x, float y, float* dx, float* dy);
	float simplex(float x, float y, float z, float* dx, float *dy, float *dz);
	float simplex(float x, float y, float z, float w, float *dx, float *dy, float *dz, float *dw);


};

#endif