#pragma once
#ifndef FOUNDATION_UTILITY_ENUM_FLAG_UTILS_HPP
#define FOUNDATION_UTILITY_ENUM_FLAG_UTILS_HPP
#include <type_traits>

/**
 * @file EnumFlagUtils.hpp
 * @brief Utilities for working with enum class bitflags in a type-safe manner.
 * Defines a macro to generate bitwise operators for enum classes used as bitmasks, and a shim that allows
 * for safe boolean checks on the results of these operations (while preserving stored value in bitmask).
*/

template<typename T>
using UnderlyingType = typename std::underlying_type<T>::type;

/** @brief Converts an enum class value to its underlying integral type */
template<typename T>
constexpr UnderlyingType<T> ToUnderlying(T value) noexcept
{
    return static_cast<UnderlyingType<T>>(value);
}

/** @brief Stores the bitmask value and preserves that intact, but also lets us convert to bool for quick checks after mask operations */
template<typename T>
struct BitmaskTrueType
{
    T value;
    constexpr BitmaskTrueType(T _value) : value(_value) {}
    constexpr operator T() const { return value; }
    constexpr explicit operator bool() const noexcept { return ToUnderlying(value) != 0; }
};

/** @brief Macro to generate bitwise operators for enum classes used as bitmasks, including operations with BitmaskTrueType for safe boolean checks */
#define MAKE_ENUM_CLASS_FLAGS(EnumType)                                                 \
    inline constexpr EnumType operator|(EnumType lhs, EnumType rhs) noexcept                        \
    {                                                                                               \
        return static_cast<EnumType>(ToUnderlying(lhs) | ToUnderlying(rhs));                        \
    }                                                                                               \
    inline constexpr EnumType operator&(EnumType lhs, EnumType rhs) noexcept                        \
    {                                                                                               \
        return static_cast<EnumType>(ToUnderlying(lhs) & ToUnderlying(rhs));                        \
    }                                                                                               \
    inline constexpr EnumType operator^(EnumType lhs, EnumType rhs) noexcept                        \
    {                                                                                               \
        return static_cast<EnumType>(ToUnderlying(lhs) ^ ToUnderlying(rhs));                        \
    }                                                                                               \
    inline constexpr EnumType operator~(EnumType value) noexcept                                    \
    {                                                                                               \
        return static_cast<EnumType>(~ToUnderlying(value));                                         \
    }                                                                                               \
    inline BitmaskTrueType<EnumType> operator|(EnumType lhs, BitmaskTrueType<EnumType> rhs) noexcept\
    {                                                                                               \
        return BitmaskTrueType<EnumType>(lhs | rhs.value);                                          \
    }                                                                                               \
    inline BitmaskTrueType<EnumType> operator&(EnumType lhs, BitmaskTrueType<EnumType> rhs) noexcept\
    {                                                                                               \
        return BitmaskTrueType<EnumType>(lhs & rhs.value);                                          \
    }                                                                                               \
    inline BitmaskTrueType<EnumType> operator^(EnumType lhs, BitmaskTrueType<EnumType> rhs) noexcept\
    {                                                                                               \
        return BitmaskTrueType<EnumType>(lhs ^ rhs.value);                                          \
    }                                                                                               \
    inline constexpr EnumType& operator|=(EnumType& lhs, EnumType rhs) noexcept                     \
    {                                                                                               \
        lhs = lhs | rhs;                                                                            \
        return lhs;                                                                                 \
    }                                                                                               \
    inline constexpr EnumType& operator&=(EnumType& lhs, EnumType rhs) noexcept                     \
    {                                                                                               \
        lhs = lhs & rhs;                                                                            \
        return lhs;                                                                                 \
    }                                                                                               \
    inline constexpr EnumType& operator^=(EnumType& lhs, EnumType rhs) noexcept                     \
    {                                                                                               \
        lhs = lhs ^ rhs;                                                                            \
        return lhs;                                                                                 \
    }

#endif // !FOUNDATION_UTILITY_ENUM_FLAG_UTILS_HPP
