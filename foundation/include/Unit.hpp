#pragma once
#ifndef FOUNDATION_UNIT_HPP
#define FOUNDATION_UNIT_HPP

/**
 * @brief A type representing a void value, used for tasks or functions that do not return a value. Avoids needing template specializations for `void` case or types.
 * Inspired by other languages and reading through the folly library's implementation of this concept
 */
struct Unit
{
    constexpr bool operator==(const Unit&) const noexcept
    {
        return true; // Unit is a singleton type, always equal to itself
    }

    constexpr bool operator!=(const Unit&) const noexcept
    {
        return false; // Unit is a singleton type, never unequal to itself
    }
};

constexpr Unit unit{};

#endif // !FOUNDATION_UNIT_HPP