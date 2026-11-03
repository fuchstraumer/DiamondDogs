#pragma once
#ifndef DIAMONDDOGS_MATH_HASHES_HPP
#define DIAMONDDOGS_MATH_HASHES_HPP
#include "Math.hpp"
#include <cstddef>
#include <functional>

// std::hash for the storage types, so they work as unordered container keys - vertex welding being
// the motivating case. Vector/Matrix are deliberately not hashable: unused lanes may hold garbage that
// would hash unequal for equal values.
//
// These hash float bit patterns as-is, so +0.0f and -0.0f hash differently despite comparing equal,
// and a NaN never matches itself. Quantize before hashing when welding geometry.

namespace math::detail
{
    // boost::hash_combine
    constexpr std::size_t HashCombine(std::size_t seed, std::size_t hash) noexcept
    {
        return seed ^ (hash + 0x9e3779b9u + (seed << 6) + (seed >> 2));
    }

    inline std::size_t HashFloat(float value) noexcept
    {
        return std::hash<float>{}(value);
    }

} // namespace math::detail

namespace std
{
    template<>
    struct hash<math::Float2>
    {
        std::size_t operator()(const math::Float2& vec) const noexcept
        {
            using namespace math::detail;
            return HashCombine(HashFloat(vec.x), HashFloat(vec.y));
        }
    };

    template<>
    struct hash<math::Float3>
    {
        std::size_t operator()(const math::Float3& vec) const noexcept
        {
            using namespace math::detail;
            std::size_t seed = HashCombine(HashFloat(vec.x), HashFloat(vec.y));
            return HashCombine(seed, HashFloat(vec.z));
        }
    };

    template<>
    struct hash<math::Float4>
    {
        std::size_t operator()(const math::Float4& vec) const noexcept
        {
            using namespace math::detail;
            std::size_t seed = HashCombine(HashFloat(vec.x), HashFloat(vec.y));
            seed = HashCombine(seed, HashFloat(vec.z));
            return HashCombine(seed, HashFloat(vec.w));
        }
    };
} // namespace std

#endif // !DIAMONDDOGS_MATH_HASHES_HPP
