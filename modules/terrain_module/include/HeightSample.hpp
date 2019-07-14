#pragma once
#ifndef DIAMOND_DOGS_HEIGHT_SAMPLE_HPP
#define DIAMOND_DOGS_HEIGHT_SAMPLE_HPP
#include <vector>
#include "Noise.hpp"
#include "glm/vec4.hpp"

struct HeightSample {
    HeightSample() {}
    bool operator<(const HeightSample& other) const;
    bool operator>(const HeightSample& other) const;
    bool operator==(const HeightSample& other) const;
    glm::vec4 Sample;
    glm::vec4 Normal;
};

static inline std::vector<HeightSample> GetNoiseHeightmap(const size_t & num_samples, const glm::vec3 & starting_location, const float & noise_step_size) {
    std::vector<HeightSample> samples;
    samples.resize(num_samples * num_samples);
    glm::vec3 init_pos = starting_location;
    //NoiseGen ng;

    for (size_t j = 0; j < num_samples; ++j) {
        for (size_t i = 0; i < num_samples; ++i) {
            glm::vec3 npos(init_pos.x + i * noise_step_size, init_pos.y + j * noise_step_size, init_pos.z);
            // doing ridged-multi fractal bit here, as it broke elsewhere.
            // TODO: Figure out why this method fails in the noise generator class: but not when done right here.
            float ampl = 1.0f;
            for (size_t k = 0; k < 12; ++k) {
                //samples[i + (j * num_samples)].Sample.x += 1.0f - fabsf(ng.sdnoise3(npos.x, npos.y, npos.z, nullptr) * ampl);
                npos *= 2.10f;
                ampl *= 0.80f;
            }
        }
    }

    return samples;
}

#endif //!DIAMOND_DOGS_HEIGHT_SAMPLE_HPP
