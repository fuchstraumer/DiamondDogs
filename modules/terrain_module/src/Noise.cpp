#include "Noise.hpp"
#include "NoiseGen.hpp"
#include "glm/vec3.hpp"
#include "glm/vec2.hpp"
#include "glm/gtc/type_ptr.hpp"

float FBM(const glm::vec3 & pos, const int32_t& seed, const float & freq, const size_t & octaves, const float & lacun, const float & persistance) {
    glm::vec3 npos = pos * freq;
    float sum = 0.0f, amplitude = 1.0f;
    NoiseGen& ng = NoiseGen::Get();
    for (size_t i = 0; i < octaves; ++i) {
        sum += ng.SimplexNoiseWithDerivative3D(npos.x, npos.y, npos.z, nullptr) * amplitude;
        npos *= lacun;
        amplitude *= persistance;
    }

    return sum;
}

float FBM_Bounded(const glm::vec3 & pos, const int32_t & seed, const float & freq, const size_t & octaves, const float & lacun, const float & persistence, const float & min, const float & max) {
    return (FBM(pos, seed, freq, octaves, lacun, persistence) - min) / (max - min);
}

//float FBM(const glm::vec2 & pos, const int32_t & seed, const float & freq, const size_t & octaves, const float & lacun, const float & persistance) {
//    glm::vec2 npos = pos * freq;
//    float sum = 0.0f, amplitude = 1.0f;
//    NoiseGen ng;
//    for (size_t i = 0; i < octaves; ++i) {
//        sum += ng.SimplexNoiseWithDerivative3D(npos.x, npos.y, nullptr, nullptr) * amplitude;
//        npos *= lacun;
//        amplitude *= persistance;
//    }
//
//    return sum;
//}

//float FBM_Bounded(const glm::vec2 & pos, const int32_t & seed, const float & freq, const size_t & octaves, const float & lacun, const float & persistence, const float & min, const float & max) {
//    return (FBM(pos, seed, freq, octaves, lacun, persistence) - min) / (max - min);
//}

float Billow(const glm::vec3 & pos, const int32_t & seed, const float & freq, const size_t & octaves, const float & lacun, const float & persistance) {
    glm::vec3 npos = pos * freq;
    float sum = 0.0f, amplitude = 1.0f;
    NoiseGen& ng = NoiseGen::Get();
    for (size_t i = 0; i < octaves; ++i) {
        sum += fabsf(ng.SimplexNoiseWithDerivative3D(npos.x, npos.y, npos.z, nullptr)) * amplitude;
        npos *= lacun;
        amplitude *= persistance;
    }
    return sum;
}

float RidgedMulti(const glm::vec3 & pos, const int32_t & seed, const float & freq, const size_t & octaves, const float & lacun, const float & persistance) {
    glm::vec3 npos = pos * freq;
    float sum = 0.0f, amplitude = 1.0f;
    NoiseGen ng;
    for (size_t i = 0; i < octaves; ++i) {
        sum += 1.0f - fabsf(ng.SimplexNoiseWithDerivative3D(npos.x, npos.y, npos.z, nullptr)) * amplitude;
        npos *= lacun;
        amplitude *= persistance;
    }
    return sum;
}

float DecarpientierSwiss(const glm::vec3 & pos, const int32_t & seed, const float & freq, const size_t & octaves, const float & lacun, const float & persistance) {
    float sum = 0.0f;
    float amplitude = 1.0f;
    float warp = 0.03f;

    glm::vec3 sp = pos * freq;
    glm::vec3 dSum = glm::vec3(0.0f);
    NoiseGen& ng = NoiseGen::Get();

    for (size_t i = 0; i < octaves; ++i) {
        glm::vec3 deriv;
        float n = ng.SimplexNoiseWithDerivative3D(sp.x, sp.y, sp.z, glm::value_ptr(deriv));
        sum += (1.0f - fabsf(n)) * amplitude;
        dSum += amplitude * -n * deriv;
        sp *= lacun;
        sp += (warp * dSum);
        amplitude *= (glm::clamp(sum, 0.0f, 1.0f) * persistance);
    }

    return sum;
}

float DecarpientierSwiss_Bounded(const glm::vec3 & pos, const int32_t & seed, const float & freq, const size_t & octaves, const float & lacun, const float & persistence, const float & min, const float & max) {
    return (DecarpientierSwiss(pos, seed, freq, octaves, lacun, persistence) - min) / (max - min);
}
//
//float DecarpientierSwiss(const glm::vec2 & pos, const int32_t & seed, const float & freq, const size_t & octaves, const float & lacun, const float & persistance) {
//    float sum = 0.0f;
//    float amplitude = 1.0f;
//    float warp = 0.1f;
//
//    glm::vec2 sp = pos * freq;
//    glm::vec2 dSum = glm::vec2(0.0f);
//    NoiseGen& ng = NoiseGen::Get();
//
//    for (size_t i = 0; i < octaves; ++i) {
//        glm::vec2 deriv;
//        float n = ng.sdnoise2(sp.x, sp.y, &deriv.x, &deriv.y);
//        sum += (1.0f - fabsf(n)) * amplitude;
//        dSum += amplitude * -n * deriv;
//        sp *= lacun;
//        sp += (warp * dSum);
//        amplitude *= (glm::clamp(sum, 0.0f, 1.0f) * persistance);
//    }
//
//    return sum;
//}
