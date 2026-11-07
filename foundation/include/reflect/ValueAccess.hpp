#pragma once
#ifndef DIAMOND_DOGS_FOUNDATION_REFLECT_VALUE_ACCESS_HPP
#define DIAMOND_DOGS_FOUNDATION_REFLECT_VALUE_ACCESS_HPP
#include <cstdint>
#include <expected>
#include <string_view>
#include <variant>

namespace foundation::reflect
{
    struct FieldMetadata;
    struct TypeInfo;

    enum class ValueResult : uint8_t
    {
        Invalid = 0,
        Success = 1,
        MalformedPath,
        UnknownField,
        NotAStruct,
        NotASequence,
        IndexOutOfRange,
        // an empty optional or a valueless variant is on the path
        NoActiveValue,
        ReadOnly,
        NotAScalar,
        WrongValueType,
        UnknownEnumCase,
        NotFinite,
        OutOfRange,
    };

    struct PathResult
    {
        ValueResult Code{ ValueResult::Invalid };
        // the position in the path where the walk stopped. The path size for an error in the value itself
        int32_t Offset{ 0 };

        [[nodiscard]] constexpr bool Succeeded() const noexcept
        {
            return Code == ValueResult::Success;
        }
    };

    struct ResolvedValue
    {
        const TypeInfo* Type{ nullptr };
        const void* Address{ nullptr };
        // the metadata of the last field on the path, which also applies to the elements of that field. Null
        // when the path contains no field
        const FieldMetadata* Metadata{ nullptr };
        // false when the path crosses a ReadOnly field or a span of const elements
        bool Writable{ true };
    };

    using ScalarValue = std::variant<bool, int64_t, uint64_t, double, std::string_view>;

    /** @brief Returns the sign-extended or zero-extended bits of an Int or Enum value, as the encoding does.
     */
    [[nodiscard]] int64_t ReadIntegerBits(const TypeInfo& type, const void* value) noexcept;

    /** @brief Walks a path such as "Spectrum.Bands[2].Height" from a root value. Optionals and variants on the
     *  path are transparent: a name or an index applies to the contained value. An empty path returns the root.
     */
    [[nodiscard]] std::expected<ResolvedValue, PathResult> ResolvePath(const TypeInfo& root,
                                                                      const void* object,
                                                                      std::string_view path);

    /** @brief Writes one scalar at the path. A refused write leaves the object unchanged. A float field accepts
     *  any number, an integer field accepts only an integer, an enum field accepts only a case name, a bool
     *  field accepts only a bool, and a string field accepts only a string. A non-finite number is always
     *  refused. */
    [[nodiscard]] PathResult SetValue(const TypeInfo& root,
                                     void* object,
                                     std::string_view path,
                                     const ScalarValue& value);

    /** @brief Returns one sentence that describes the error, for tools to show. */
    [[nodiscard]] std::string_view ValueResultText(ValueResult error) noexcept;
}

#endif // !DIAMOND_DOGS_FOUNDATION_REFLECT_VALUE_ACCESS_HPP
