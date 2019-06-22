#pragma once
#ifndef DIAMOND_DOGS_MURMUR_HASH_HPP
#define DIAMOND_DOGS_MURMUR_HASH_HPP
#include <cstdint>

struct [[nodiscard]] murmur_hash_result_t
{
    uint64_t data[2];
};

[[nodiscard]] constexpr uint64_t MurmurHash2(const void* key, const size_t len, const uint64_t seed) noexcept;
murmur_hash_result_t MurmurHash3(const void* key, size_t len, const uint32_t seed);

#endif //!DIAMOND_DOGS_MURMUR_HASH_HPP
