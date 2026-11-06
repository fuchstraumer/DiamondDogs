#pragma once
#ifndef DIAMOND_DOGS_FOUNDATION_REFLECT_VALUE_TRAITS_HPP
#define DIAMOND_DOGS_FOUNDATION_REFLECT_VALUE_TRAITS_HPP
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

// The value types that a descriptor may contain. Encode and TypeInfo both classify through these, so that
// they accept the same types.
namespace foundation::reflect
{
    template<typename T>
    inline constexpr bool k_AlwaysFalse = false;

    template<typename T>
    struct IsVector : std::false_type {};

    template<typename U, typename A>
    struct IsVector<std::vector<U, A>> : std::true_type {};

    template<typename T>
    struct IsStdArray : std::false_type {};

    template<typename U, size_t N>
    struct IsStdArray<std::array<U, N>> : std::true_type {};

    template<typename T>
    struct IsSpan : std::false_type {};

    template<typename U, size_t N>
    struct IsSpan<std::span<U, N>> : std::true_type {};

    template<typename T>
    struct IsOptional : std::false_type {};

    template<typename U>
    struct IsOptional<std::optional<U>> : std::true_type {};

    template<typename T>
    struct IsVariant : std::false_type {};

    template<typename... Alternatives>
    struct IsVariant<std::variant<Alternatives...>> : std::true_type {};

    template<typename T>
    concept FixedInt = std::same_as<T, int8_t> || std::same_as<T, int16_t> || std::same_as<T, int32_t> ||
        std::same_as<T, int64_t> || std::same_as<T, uint8_t> || std::same_as<T, uint16_t> ||
        std::same_as<T, uint32_t> || std::same_as<T, uint64_t>;

    template<typename T>
    concept Sequence = IsVector<T>::value || IsStdArray<T>::value || IsSpan<T>::value;

    template<typename T>
    concept StringLike = std::same_as<T, std::string> || std::same_as<T, std::string_view>;

    template<typename T>
    concept FloatingPoint = std::same_as<T, float> || std::same_as<T, double>;
}

#endif // !DIAMOND_DOGS_FOUNDATION_REFLECT_VALUE_TRAITS_HPP
