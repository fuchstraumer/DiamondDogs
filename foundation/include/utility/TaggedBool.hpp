#pragma once
#ifndef DIAMOND_DOGS_TAGGED_BOOL_HPP
#define DIAMOND_DOGS_TAGGED_BOOL_HPP

/**
 * @brief Type-safe boolean wrapper that prevents implicit conversions
 * 
 * A strongly-typed boolean class that prevents accidental conversion from
 * integers, pointers, or other types. Each instantiation with a different
 * tag type creates a distinct type, allowing for type-safe function parameters.
 * 
 * @tparam Tag Unique tag type to distinguish different boolean types
 */
template<typename Tag>
class tagged_bool
{
    bool value;
    template<typename>
    friend class tagged_bool;
public:
    /**
     * @brief Constructor from boolean value
     * 
     * @param v Boolean value to store
     * @note Explicit constructor prevents accidental conversions
     */
    constexpr explicit tagged_bool(bool v) : value{v} {}
    
    /// Deleted constructors to prevent implicit conversions
    constexpr explicit tagged_bool(int) = delete;
    constexpr explicit tagged_bool(double) = delete;
    constexpr explicit tagged_bool(void*) = delete;

    /**
     * @brief Constructor from different tagged_bool type
     * 
     * @tparam OtherTag Tag type of the other tagged_bool
     * @param b Other tagged_bool to convert from
     */
    template<typename OtherTag>
    constexpr explicit tagged_bool(tagged_bool<OtherTag> b) : value{b.value} {}

    /**
     * @brief Convert to boolean value
     * 
     * @return Stored boolean value
     * @note Explicit conversion prevents accidental usage in arithmetic
     */
    constexpr explicit operator bool() const noexcept { return value; }
    
    /**
     * @brief Logical NOT operator
     * 
     * @return New tagged_bool with inverted value
     */
    constexpr tagged_bool operator!() const noexcept { return tagged_bool{!value}; }

    /// Equality comparison
    friend constexpr bool operator==(tagged_bool l, tagged_bool r) { return l.value == r.value; }
    /// Inequality comparison
    friend constexpr bool operator!=(tagged_bool l, tagged_bool r) { return l.value != r.value; }
    
};

#endif //!DIAMOND_DOGS_TAGGED_BOOL_HPP
