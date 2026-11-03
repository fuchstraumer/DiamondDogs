#pragma once
#ifndef DIAMOND_DOGS_FOUNDATION_REFLECT_DESCRIBE_HPP
#define DIAMOND_DOGS_FOUNDATION_REFLECT_DESCRIBE_HPP
#include <string_view>
#include <type_traits>

namespace foundation::reflect
{
    // declared and never defined, so a type without a descriptor fails to compile where it is used
    template<typename T>
    struct Describe;

    template<typename Owner, typename Member>
    struct Field
    {
        std::string_view Name;
        Member Owner::* Pointer;
    };

    template<typename E>
    struct EnumCase
    {
        std::string_view Name;
        E Value;
    };

    template<typename T>
    concept DescribedStruct = requires
    {
        Describe<T>::Name;
        Describe<T>::Fields;
    };

    template<typename T>
    concept DescribedEnum = std::is_enum_v<T> && requires
    {
        Describe<T>::Name;
        Describe<T>::Cases;
    };
}

#endif // !DIAMOND_DOGS_FOUNDATION_REFLECT_DESCRIBE_HPP
