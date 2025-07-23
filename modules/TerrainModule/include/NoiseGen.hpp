#pragma once
#ifndef TERRAIN_MODULE_NOISE_GENERATOR_HPP
#define TERRAIN_MODULE_NOISE_GENERATOR_HPP
#include <cstdint>
#include <array>
#include <random>

class NoiseGen
{
public:

    NoiseGen();

    void Seed(const size_t seed) noexcept;

    static NoiseGen& Get();

    /*
        Functions that return derivatives use pointers default to nullptr on purpose, as there
        is a nonzero cost to using them.
    */
    float SimplexNoiseWithDerivative3D(float x, float y, float z, float* deriv = nullptr) const;

private:

    std::mt19937_64 rng;
    std::random_device randomDvc;
    std::array<int32_t, 512> permutationArray;
};

#endif // !TERRAIN_MODULE_NOISE_GENERATOR_HPP
