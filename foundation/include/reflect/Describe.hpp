#pragma once
#ifndef DIAMOND_DOGS_FOUNDATION_REFLECT_DESCRIBE_HPP
#define DIAMOND_DOGS_FOUNDATION_REFLECT_DESCRIBE_HPP
#include <array>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace foundation::reflect
{
    // declared and never defined, so a type without a descriptor fails to compile where it is used
    template<typename T>
    struct Describe;

    /** @brief The inclusive limits of a numeric field. A write outside these limits is invalid. */
    struct FieldRange
    {
        double Min;
        double Max;
    };

    /** @brief The limit for a range that is open at one end, for example { 0.0, k_NoLimit }. */
    inline constexpr double k_NoLimit = std::numeric_limits<double>::infinity();

    /** @brief Optional facts about a field. Recipe keys ignore all of this, so that editing it never
     *  invalidates content. */
    struct FieldMetadata
    {
        std::string_view Unit;
        std::optional<FieldRange> Range;
        std::string_view Doc;
        bool ReadOnly{ false };
        bool Hidden{ false };
        // a command call must give this field
        bool Required{ false };
    };

    template<typename Owner, typename Member>
    struct Field
    {
        std::string_view Name;
        Member Owner::* Pointer;
        FieldMetadata Metadata;
    };

    template<typename E>
    struct EnumCase
    {
        std::string_view Name;
        E Value;
        std::string_view Doc;
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

    template<DescribedStruct T>
    inline constexpr size_t k_FieldCount =
        std::tuple_size_v<std::remove_cvref_t<decltype(Describe<T>::Fields)>>;

    /** @brief Returns the optional Doc member of a type's descriptor, or an empty view when it has none. */
    template<typename T>
    constexpr std::string_view DescribedDoc() noexcept
    {
        if constexpr (requires { Describe<T>::Doc; })
        {
            return Describe<T>::Doc;
        }
        else
        {
            return {};
        }
    }

    namespace detail
    {
        constexpr bool NamesAreUnique(std::span<const std::string_view> names) noexcept
        {
            for (size_t index = 0; index < names.size(); ++index)
            {
                for (size_t earlier = 0; earlier < index; ++earlier)
                {
                    if (names[earlier] == names[index])
                    {
                        return false;
                    }
                }
            }

            return true;
        }

        // MSVC 14.51 evaluates NaN <= 1.0 as true in a constant expression, and false at runtime. Clang
        // returns false for both. NaN == NaN is correct on both compilers, so we test for a NaN limit first.
        constexpr bool RangeIsOrdered(const std::optional<FieldRange>& range) noexcept
        {
            if (!range.has_value())
            {
                return true;
            }

            const bool hasNaNLimit = range->Min != range->Min || range->Max != range->Max;

            return !hasNaNLimit && range->Min <= range->Max;
        }

        template<typename T, size_t... Indices>
        consteval bool FieldNamesAreUnique(std::index_sequence<Indices...>)
        {
            const std::array<std::string_view, sizeof...(Indices)> names
            {
                std::get<Indices>(Describe<T>::Fields).Name...
            };

            return NamesAreUnique(names);
        }

        template<typename T, size_t... Indices>
        consteval bool FieldRangesAreOrdered(std::index_sequence<Indices...>)
        {
            return (RangeIsOrdered(std::get<Indices>(Describe<T>::Fields).Metadata.Range) && ...);
        }

        template<typename E, size_t... Indices>
        consteval bool EnumCaseNamesAreUnique(std::index_sequence<Indices...>)
        {
            const std::array<std::string_view, sizeof...(Indices)> names
            {
                Describe<E>::Cases[Indices].Name...
            };

            return NamesAreUnique(names);
        }
    }

    /** @brief Returns true when no two fields of the descriptor have the same name. */
    template<DescribedStruct T>
    consteval bool FieldNamesAreUnique()
    {
        return detail::FieldNamesAreUnique<T>(std::make_index_sequence<k_FieldCount<T>>{});
    }

    /** @brief Returns true when every range in the descriptor has Min <= Max. A NaN limit returns false. */
    template<DescribedStruct T>
    consteval bool FieldRangesAreOrdered()
    {
        return detail::FieldRangesAreOrdered<T>(std::make_index_sequence<k_FieldCount<T>>{});
    }

    /** @brief Returns true when no two cases of the descriptor have the same name. */
    template<DescribedEnum E>
    consteval bool EnumCaseNamesAreUnique()
    {
        return detail::EnumCaseNamesAreUnique<E>(std::make_index_sequence<std::size(Describe<E>::Cases)>{});
    }

    /** @brief Fails the build for a struct descriptor with a duplicate field name or an inverted range. */
    template<DescribedStruct T>
    consteval void CheckStructDescriptor()
    {
        static_assert(FieldNamesAreUnique<T>(), "Describe<T>: two fields have the same name");
        static_assert(FieldRangesAreOrdered<T>(), "Describe<T>: a field range has Min > Max or a NaN limit");
    }

    /** @brief Fails the build for an enum descriptor that has a duplicate case name. */
    template<DescribedEnum E>
    consteval void CheckEnumDescriptor()
    {
        static_assert(EnumCaseNamesAreUnique<E>(), "Describe<E>: two enum cases have the same name");
    }
}

#endif // !DIAMOND_DOGS_FOUNDATION_REFLECT_DESCRIBE_HPP
