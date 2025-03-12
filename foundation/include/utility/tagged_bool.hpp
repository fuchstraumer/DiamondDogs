#pragma once
#ifndef DIAMOND_DOGS_TAGGED_BOOL_HPP
#define DIAMOND_DOGS_TAGGED_BOOL_HPP

template<typename Tag>
class tagged_bool
{
    bool value;
    template<typename>
    friend class tagged_bool;
public:
    constexpr explicit tagged_bool(bool v) : value{v} {}
    constexpr explicit tagged_bool(int) = delete;
    constexpr explicit tagged_bool(double) = delete;
    constexpr explicit tagged_bool(void*) = delete;

    template<typename OtherTag>
    constexpr explicit tagged_bool(tagged_bool<OtherTag> b) : value{b.value} {}

    constexpr explicit operator bool() const noexcept { return value; }
    constexpr tagged_bool operator!() const noexcept { return tagged_bool{!value}; }

    friend constexpr bool operator==(tagged_bool l, tagged_bool r) { return l.value == r.value; }
    friend constexpr bool operator!=(tagged_bool l, tagged_bool r) { return l.value != r.value; }
    
};

#endif //!DIAMOND_DOGS_TAGGED_BOOL_HPP
