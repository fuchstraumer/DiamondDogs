#pragma once
#ifndef DIAMOND_DOGS_HEIGHT_SAMPLE_HPP
#define DIAMOND_DOGS_HEIGHT_SAMPLE_HPP
#include <vector>
#include "Noise.hpp"
#include "glm/vec2.hpp"

using HeightSample = glm::vec2;

std::vector<HeightSample> GetNoiseHeightmap(const size_t& num_samples, const glm::vec3& starting_location, const float& noise_step_size);

#endif //!DIAMOND_DOGS_HEIGHT_SAMPLE_HPP
