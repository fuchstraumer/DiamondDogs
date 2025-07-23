#include "HeightSample.hpp"
#include "Noise.hpp"
#include "glm/vec3.hpp"

std::vector<HeightSample> GetNoiseHeightmap(const size_t& num_samples, const glm::vec3& starting_location, const float& noise_step_size) {
    std::vector<HeightSample> samples;
    samples.resize(num_samples * num_samples, HeightSample(0.0f, 0.0f));
    glm::vec3 init_pos = starting_location;

    for (size_t j = 0; j < num_samples; ++j) {
        for (size_t i = 0; i < num_samples; ++i) {
            glm::vec3 npos(init_pos.x + i * noise_step_size, init_pos.y + j * noise_step_size, init_pos.z);
            samples[i + (j * num_samples)].x = FBM(npos, 0u, 0.4, 6, 1.5, 0.8f);
        }
    }

    return samples;
}
