#pragma once
#ifndef TERRAIN_PLUGIN_NOISE_GENERATORS_HPP
#define TERRAIN_PLUGIN_NOISE_GENERATORS_HPP
#include <cstdint>
#include "glm/fwd.hpp"
#include <random>

constexpr static uint32_t FNV_32_PRIME = 0x01000193;
constexpr static uint32_t FNV_32_INIT = 2166136261;
constexpr static uint32_t FNV_MASK_8 = (1 << 8) - 1;

float FBM(const glm::vec3& pos, const int32_t& seed, const float& freq, const size_t& octaves, const float& lacun, const float& persistance);
float FBM_Bounded(const glm::vec3& pos, const int32_t& seed, const float& freq, const size_t& octaves, const float& lacun, const float& persistence, const float& min, const float& max);

//float FBM(const glm::vec2& pos, const int32_t& seed, const float& freq, const size_t& octaves, const float& lacun, const float& persistance);
//float FBM_Bounded(const glm::vec2& pos, const int32_t& seed, const float& freq, const size_t& octaves, const float& lacun, const float& persistence, const float& min, const float& max);

float Billow(const glm::vec3& pos, const int32_t& seed, const float& freq, const size_t& octaves, const float& lacun, const float& persistance);
//float Billow_Bounded(const glm::vec3& pos, const int32_t& seed, const float& freq, const size_t& octaves, const float& lacun, const float& persistence, const float& min, const float& max);

//float Billow(const glm::vec2& pos, const int32_t& seed, const float& freq, const size_t& octaves, const float& lacun, const float& persistance);
//float Billow_Bounded(const glm::vec2& pos, const int32_t& seed, const float& freq, const size_t& octaves, const float& lacun, const float& persistence, const float& min, const float& max);

float RidgedMulti(const glm::vec3& pos, const int32_t& seed, const float& freq, const size_t& octaves, const float& lacun, const float& persistance);
//float RidgedMulti_Bounded(const glm::vec3& pos, const int32_t& seed, const float& freq, const size_t& octaves, const float& lacun, const float& persistence, const float& min, const float& max);

//float RidgedMulti(const glm::vec2& pos, const int32_t& seed, const float& freq, const size_t& octaves, const float& lacun, const float& persistance);
//float RidgedMulti_Bounded(const glm::vec2& pos, const int32_t& seed, const float& freq, const size_t& octaves, const float& lacun, const float& persistence, const float& min, const float& max);

float DecarpientierSwiss(const glm::vec3& pos, const int32_t& seed, const float& freq, const size_t& octaves, const float& lacun, const float& persistance);
float DecarpientierSwiss_Bounded(const glm::vec3& pos, const int32_t& seed, const float& freq, const size_t& octaves, const float& lacun, const float& persistence, const float& min, const float& max);

//float DecarpientierSwiss(const glm::vec2& pos, const int32_t& seed, const float& freq, const size_t& octaves, const float& lacun, const float& persistance);
    

#endif // !TERRAIN_PLUGIN_NOISE_GENERATORS_HPP
