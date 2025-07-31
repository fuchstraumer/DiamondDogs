#pragma once
#ifndef DIAMOND_DOGS_FOUNDATION_MATH_HASHES_HPP
#define DIAMOND_DOGS_FOUNDATION_MATH_HASHES_HPP

#include "Math.hpp"
#include <functional>

namespace std
{
    namespace detail
    {
        // Hash combining utility using a robust algorithm to minimize collisions
        // This combines hashes with bit shifting and prime multiplication for better distribution
        inline constexpr size_t hash_combine(size_t seed, size_t hash) noexcept
        {
            // Using the boost::hash_combine algorithm with golden ratio constant
            return seed ^ (hash + 0x9e3779b9 + (seed << 6) + (seed >> 2));
        }
        
        template<typename T>
        inline constexpr size_t hash_value(const T& value) noexcept
        {
            return std::hash<T>{}(value);
        }
    }

    // Float2 hash specialization
    template<>
    struct hash<math::Float2>
    {
        constexpr size_t operator()(const math::Float2& vec) const noexcept
        {
            size_t seed = detail::hash_value(vec.x);
            return detail::hash_combine(seed, detail::hash_value(vec.y));
        }
    };

    // Float3 hash specialization
    template<>
    struct hash<math::Float3>
    {
        constexpr size_t operator()(const math::Float3& vec) const noexcept
        {
            size_t seed = detail::hash_value(vec.x);
            seed = detail::hash_combine(seed, detail::hash_value(vec.y));
            return detail::hash_combine(seed, detail::hash_value(vec.z));
        }
    };

    // Float4 hash specialization
    template<>
    struct hash<math::Float4>
    {
        constexpr size_t operator()(const math::Float4& vec) const noexcept
        {
            size_t seed = detail::hash_value(vec.x);
            seed = detail::hash_combine(seed, detail::hash_value(vec.y));
            seed = detail::hash_combine(seed, detail::hash_value(vec.z));
            return detail::hash_combine(seed, detail::hash_value(vec.w));
        }
    };

    // Int2 hash specialization
    template<>
    struct hash<math::Int2>
    {
        constexpr size_t operator()(const math::Int2& vec) const noexcept
        {
            size_t seed = detail::hash_value(vec.x);
            return detail::hash_combine(seed, detail::hash_value(vec.y));
        }
    };

    // Int3 hash specialization
    template<>
    struct hash<math::Int3>
    {
        constexpr size_t operator()(const math::Int3& vec) const noexcept
        {
            size_t seed = detail::hash_value(vec.x);
            seed = detail::hash_combine(seed, detail::hash_value(vec.y));
            return detail::hash_combine(seed, detail::hash_value(vec.z));
        }
    };

    // Int4 hash specialization
    template<>
    struct hash<math::Int4>
    {
        constexpr size_t operator()(const math::Int4& vec) const noexcept
        {
            size_t seed = detail::hash_value(vec.x);
            seed = detail::hash_combine(seed, detail::hash_value(vec.y));
            seed = detail::hash_combine(seed, detail::hash_value(vec.z));
            return detail::hash_combine(seed, detail::hash_value(vec.w));
        }
    };

    // UInt2 hash specialization
    template<>
    struct hash<math::UInt2>
    {
        constexpr size_t operator()(const math::UInt2& vec) const noexcept
        {
            size_t seed = detail::hash_value(vec.x);
            return detail::hash_combine(seed, detail::hash_value(vec.y));
        }
    };

    // UInt3 hash specialization
    template<>
    struct hash<math::UInt3>
    {
        constexpr size_t operator()(const math::UInt3& vec) const noexcept
        {
            size_t seed = detail::hash_value(vec.x);
            seed = detail::hash_combine(seed, detail::hash_value(vec.y));
            return detail::hash_combine(seed, detail::hash_value(vec.z));
        }
    };

    // UInt4 hash specialization
    template<>
    struct hash<math::UInt4>
    {
        constexpr size_t operator()(const math::UInt4& vec) const noexcept
        {
            size_t seed = detail::hash_value(vec.x);
            seed = detail::hash_combine(seed, detail::hash_value(vec.y));
            seed = detail::hash_combine(seed, detail::hash_value(vec.z));
            return detail::hash_combine(seed, detail::hash_value(vec.w));
        }
    };

} // namespace std

#endif // !DIAMOND_DOGS_FOUNDATION_MATH_HASHES_HPP
