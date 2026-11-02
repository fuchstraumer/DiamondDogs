#pragma once
#ifndef DIAMOND_DOGS_FOUNDATION_CONTENT_ENCODE_HPP
#define DIAMOND_DOGS_FOUNDATION_CONTENT_ENCODE_HPP

#include "content/CanonicalSink.hpp"
#include "reflect/Describe.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace foundation::content
{
    // Walks a value through its descriptor and writes it to the sink. A type with no encoding fails to compile.
    template<typename T>
    void Encode(const T& value, CanonicalSink& sink);

    // Returns the dotted path of the first NaN, for example "Spectrum.Amplitude", or nothing.
    template<typename T>
    std::optional<std::string> FindNaN(const T& value);

    namespace detail
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

        template<typename T>
        std::string_view DescribedName()
        {
            static_assert(reflect::DescribedStruct<T> || reflect::DescribedEnum<T>,
                          "a variant alternative needs a Describe<T> specialization");

            return reflect::Describe<T>::Name;
        }

        template<FixedInt T>
        void EncodeInt(const T& value, CanonicalSink& sink)
        {
            // signed values sign-extend and unsigned values zero-extend, and the sink writes the low bytes
            const int64_t bits = std::is_signed_v<T> ? static_cast<int64_t>(value)
                                                     : static_cast<int64_t>(static_cast<uint64_t>(value));

            sink.WriteInt(bits, static_cast<int32_t>(sizeof(T)), std::is_signed_v<T>);
        }

        template<reflect::DescribedEnum E>
        void EncodeEnum(const E& value, CanonicalSink& sink)
        {
            // by case name, so a case inserted before this one does not change this one's key
            for (const reflect::EnumCase<E>& enum_case : reflect::Describe<E>::Cases)
            {
                if (enum_case.Value == value)
                {
                    sink.WriteEnum(reflect::Describe<E>::Name, enum_case.Name);
                    return;
                }
            }

            assert(false && "an enum value has no case in its descriptor");
            sink.WriteEnum(reflect::Describe<E>::Name, "<no case>");
        }

        template<typename Owner, typename Member>
        void EncodeField(const Owner& owner, const reflect::Field<Owner, Member>& field, CanonicalSink& sink)
        {
            sink.BeginField(field.Name);
            Encode(owner.*(field.Pointer), sink);
        }

        template<typename T, size_t... Indices>
        void EncodeFields(const T& value, CanonicalSink& sink, std::index_sequence<Indices...>)
        {
            (EncodeField(value, std::get<Indices>(reflect::Describe<T>::Fields), sink), ...);
        }

        template<reflect::DescribedStruct T>
        void EncodeStruct(const T& value, CanonicalSink& sink)
        {
            constexpr size_t fieldCount = std::tuple_size_v<std::remove_cvref_t<decltype(reflect::Describe<T>::Fields)>>;

            sink.BeginStruct(reflect::Describe<T>::Name);
            EncodeFields(value, sink, std::make_index_sequence<fieldCount>{});
            sink.EndStruct();
        }

        struct EncodeVariantVisitor
        {
            CanonicalSink& Sink;

            template<typename Alternative>
            void operator()(const Alternative& alternative) const
            {
                Sink.WriteVariant(DescribedName<Alternative>());
                Encode(alternative, Sink);
            }
        };

        template<typename T>
        bool FindNaNInto(const T& value, std::string& path);

        struct FindNaNVariantVisitor
        {
            std::string& Path;

            template<typename Alternative>
            bool operator()(const Alternative& alternative) const
            {
                return FindNaNInto(alternative, Path);
            }
        };

        template<typename Owner, typename Member>
        bool FindNaNInField(const Owner& owner, const reflect::Field<Owner, Member>& field, std::string& path)
        {
            const size_t restoreSize = path.size();

            if (!path.empty())
            {
                path.push_back('.');
            }

            path.append(field.Name);

            if (FindNaNInto(owner.*(field.Pointer), path))
            {
                return true;
            }

            path.resize(restoreSize);

            return false;
        }

        template<typename T, size_t... Indices>
        bool FindNaNInFields(const T& value, std::string& path, std::index_sequence<Indices...>)
        {
            return (FindNaNInField(value, std::get<Indices>(reflect::Describe<T>::Fields), path) || ...);
        }

        template<typename T>
        bool FindNaNInElements(const T& sequence, std::string& path)
        {
            const size_t restoreSize = path.size();
            size_t index = 0;

            for (const auto& element : sequence)
            {
                path.append("[").append(std::to_string(index)).append("]");

                if (FindNaNInto(element, path))
                {
                    return true;
                }

                path.resize(restoreSize);
                ++index;
            }

            return false;
        }

        template<typename T>
        bool FindNaNInto(const T& value, std::string& path)
        {
            if constexpr (FloatingPoint<T>)
            {
                return std::isnan(value);
            }
            else if constexpr (reflect::DescribedStruct<T>)
            {
                constexpr size_t k_FieldCount =
                    std::tuple_size_v<std::remove_cvref_t<decltype(reflect::Describe<T>::Fields)>>;

                return FindNaNInFields(value, path, std::make_index_sequence<k_FieldCount>{});
            }
            else if constexpr (Sequence<T>)
            {
                return FindNaNInElements(value, path);
            }
            else if constexpr (IsOptional<T>::value)
            {
                return value.has_value() && FindNaNInto(*value, path);
            }
            else if constexpr (IsVariant<T>::value)
            {
                return std::visit(FindNaNVariantVisitor{ path }, value);
            }
            else
            {
                // bools, integers, strings and enums cannot hold a NaN
                return false;
            }
        }
    }

    template<typename T>
    void Encode(const T& value, CanonicalSink& sink)
    {
        if constexpr (std::same_as<T, bool>)
        {
            sink.WriteBool(value);
        }
        else if constexpr (detail::FixedInt<T>)
        {
            detail::EncodeInt(value, sink);
        }
        else if constexpr (std::same_as<T, float>)
        {
            sink.WriteFloat32(value);
        }
        else if constexpr (std::same_as<T, double>)
        {
            sink.WriteFloat64(value);
        }
        else if constexpr (detail::StringLike<T>)
        {
            sink.WriteString(value);
        }
        else if constexpr (reflect::DescribedEnum<T>)
        {
            detail::EncodeEnum(value, sink);
        }
        else if constexpr (reflect::DescribedStruct<T>)
        {
            detail::EncodeStruct(value, sink);
        }
        else if constexpr (detail::Sequence<T>)
        {
            sink.BeginArray(value.size());

            for (const auto& element : value)
            {
                Encode(element, sink);
            }

            sink.EndArray();
        }
        else if constexpr (detail::IsOptional<T>::value)
        {
            sink.WriteOptional(value.has_value());

            if (value.has_value())
            {
                Encode(*value, sink);
            }
        }
        else if constexpr (detail::IsVariant<T>::value)
        {
            std::visit(detail::EncodeVariantVisitor{ sink }, value);
        }
        else
        {
            static_assert(detail::k_AlwaysFalse<T>,
                          "Encode: no encoding for this type. Use bool, a fixed-width integer, float, double, a "
                          "string, a type with a Describe<T> specialization, or a vector, array, span, optional "
                          "or variant of those.");
        }
    }

    template<typename T>
    std::optional<std::string> FindNaN(const T& value)
    {
        std::string path;

        if (detail::FindNaNInto(value, path))
        {
            return path;
        }

        return std::nullopt;
    }
}

#endif // !DIAMOND_DOGS_FOUNDATION_CONTENT_ENCODE_HPP
