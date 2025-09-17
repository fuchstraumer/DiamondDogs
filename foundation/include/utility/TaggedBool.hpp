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
class TaggedBool
{
    bool value;
    template<typename>
    friend class TaggedBool;
public:
    /**
     * @brief Constructor from boolean value
     * 
     * @param v Boolean value to store
     * @note Explicit constructor prevents accidental conversions
     */
    constexpr explicit TaggedBool(bool v) noexcept : value{v} {}
    
    /// Deleted constructors to prevent implicit conversions
    constexpr explicit TaggedBool(int) = delete;
    constexpr explicit TaggedBool(double) = delete;
    constexpr explicit TaggedBool(void*) = delete;

    /**
     * @brief Constructor from different TaggedBool type
     * 
     * @tparam OtherTag Tag type of the other TaggedBool
     * @param b Other TaggedBool to convert from
     */
    template<typename OtherTag>
    constexpr explicit TaggedBool(TaggedBool<OtherTag> b) : value{b.value} {}

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
     * @return New TaggedBool with inverted value
     */
    constexpr TaggedBool operator!() const noexcept { return TaggedBool{!value}; }

    /// Equality comparison
    friend constexpr bool operator==(TaggedBool l, TaggedBool r) { return l.value == r.value; }
    /// Inequality comparison
    friend constexpr bool operator!=(TaggedBool l, TaggedBool r) { return l.value != r.value; }
    
};

#endif //!DIAMOND_DOGS_TAGGED_BOOL_HPP
