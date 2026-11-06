#include "reflect/ValueAccess.hpp"
#include "reflect/Describe.hpp"
#include "reflect/TypeInfo.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>

namespace foundation::reflect
{
    namespace
    {
        template<typename Integer>
        int64_t ReadStored(const void* value) noexcept
        {
            Integer stored{};
            std::memcpy(&stored, value, sizeof(Integer));

            // signed values sign-extend and unsigned values zero-extend
            return static_cast<int64_t>(stored);
        }

        template<typename Integer>
        void WriteStored(void* value, int64_t bits) noexcept
        {
            const Integer stored = static_cast<Integer>(bits);

            std::memcpy(value, &stored, sizeof(Integer));
        }

        // memcpy, not a typed store, because an Enum object cannot alias its underlying type
        void WriteIntegerBits(const TypeInfo& type, void* value, int64_t bits) noexcept
        {
            switch (type.IntegerWidthBytes)
            {
            case 1:
                WriteStored<uint8_t>(value, bits);
                break;
            case 2:
                WriteStored<uint16_t>(value, bits);
                break;
            case 4:
                WriteStored<uint32_t>(value, bits);
                break;
            default:
                WriteStored<uint64_t>(value, bits);
                break;
            }
        }

        std::optional<PathError> UnwrapActiveValue(ResolvedValue& resolved, int32_t offset) noexcept
        {
            while (resolved.Type->Kind == TypeKind::Optional || resolved.Type->Kind == TypeKind::Variant)
            {
                const bool isOptional = resolved.Type->Kind == TypeKind::Optional;
                const void* contained = isOptional ? resolved.Type->OptionalValue(resolved.Address)
                                                   : resolved.Type->ActiveValue(resolved.Address);

                if (contained == nullptr)
                {
                    return PathError{ ValueError::NoActiveValue, offset };
                }

                const TypeInfo& containedType =
                    isOptional ? resolved.Type->Element()
                               : resolved.Type->Alternatives[static_cast<size_t>(
                                     resolved.Type->ActiveIndex(resolved.Address))]();

                resolved.Type = &containedType;
                resolved.Address = contained;
            }

            return std::nullopt;
        }

        std::optional<PathError> StepIntoField(ResolvedValue& resolved, std::string_view name,
                                                int32_t offset) noexcept
        {
            if (std::optional<PathError> error = UnwrapActiveValue(resolved, offset))
            {
                return error;
            }

            if (resolved.Type->Kind != TypeKind::Struct)
            {
                return PathError{ ValueError::NotAStruct, offset };
            }

            const FieldInfo* field = FindField(*resolved.Type, name);

            if (field == nullptr)
            {
                return PathError{ ValueError::UnknownField, offset };
            }

            resolved.Address = FieldAddress(*field, resolved.Address);
            resolved.Type = &field->Type();
            resolved.Metadata = &field->Metadata;
            resolved.Writable = resolved.Writable && !field->Metadata.ReadOnly;

            return std::nullopt;
        }

        std::optional<PathError> StepIntoElement(ResolvedValue& resolved, int64_t index,
                                                  int32_t offset) noexcept
        {
            if (std::optional<PathError> error = UnwrapActiveValue(resolved, offset))
            {
                return error;
            }

            if (resolved.Type->Kind != TypeKind::Sequence)
            {
                return PathError{ ValueError::NotASequence, offset };
            }

            if (index >= resolved.Type->ElementCount(resolved.Address))
            {
                return PathError{ ValueError::IndexOutOfRange, offset };
            }

            resolved.Address = resolved.Type->ElementAt(resolved.Address, index);
            resolved.Writable = resolved.Writable && resolved.Type->ElementsAreWritable;
            resolved.Type = &resolved.Type->Element();

            return std::nullopt;
        }

        bool IsDigit(char character) noexcept
        {
            return character >= '0' && character <= '9';
        }

        // position is at the opening bracket. Returns the index and advances the position past the closing bracket
        std::optional<int64_t> ParseIndex(std::string_view path, size_t& position) noexcept
        {
            const size_t first = position + 1;

            if (first >= path.size() || !IsDigit(path[first]))
            {
                return std::nullopt;
            }

            int64_t index = 0;
            const char* end = path.data() + path.size();
            const std::from_chars_result result = std::from_chars(path.data() + first, end, index);

            if (result.ec != std::errc{} || result.ptr == end || *result.ptr != ']')
            {
                return std::nullopt;
            }

            position = static_cast<size_t>(result.ptr - path.data()) + 1;

            return index;
        }

        std::string_view ParseFieldName(std::string_view path, size_t& position) noexcept
        {
            const size_t end = std::min(path.find_first_of(".[", position), path.size());
            const std::string_view name = path.substr(position, end - position);

            position = end;

            return name;
        }

        std::optional<ValueError> CheckRange(const FieldMetadata* metadata, double value) noexcept
        {
            if (metadata == nullptr || !metadata->Range.has_value())
            {
                return std::nullopt;
            }

            if (value < metadata->Range->Min || value > metadata->Range->Max)
            {
                return ValueError::OutOfRange;
            }

            return std::nullopt;
        }

        float NarrowLimit(double limit) noexcept
        {
            constexpr double k_FloatMax = std::numeric_limits<float>::max();

            return static_cast<float>(std::clamp(limit, -k_FloatMax, k_FloatMax));
        }

        // compares at float precision, so that a value read from the field and written back is never refused. A
        // limit of 0.1 is below 0.1f as a double.
        std::optional<ValueError> CheckFloat32Range(const FieldMetadata* metadata, float value) noexcept
        {
            if (metadata == nullptr || !metadata->Range.has_value())
            {
                return std::nullopt;
            }

            if (value < NarrowLimit(metadata->Range->Min) || value > NarrowLimit(metadata->Range->Max))
            {
                return ValueError::OutOfRange;
            }

            return std::nullopt;
        }

        std::optional<double> ScalarAsNumber(const ScalarValue& value) noexcept
        {
            if (const double* number = std::get_if<double>(&value))
            {
                return *number;
            }

            if (const int64_t* integer = std::get_if<int64_t>(&value))
            {
                return static_cast<double>(*integer);
            }

            if (const uint64_t* unsignedInteger = std::get_if<uint64_t>(&value))
            {
                return static_cast<double>(*unsignedInteger);
            }

            return std::nullopt;
        }

        std::optional<ValueError> WriteFloat(const ResolvedValue& resolved, void* address,
                                             const ScalarValue& value) noexcept
        {
            const std::optional<double> number = ScalarAsNumber(value);

            if (!number.has_value())
            {
                return ValueError::WrongValueType;
            }

            if (!std::isfinite(*number))
            {
                return ValueError::NotFinite;
            }

            if (resolved.Type->Kind == TypeKind::Float64)
            {
                if (std::optional<ValueError> error = CheckRange(resolved.Metadata, *number))
                {
                    return error;
                }

                *static_cast<double*>(address) = *number;

                return std::nullopt;
            }

            if (std::abs(*number) > static_cast<double>(std::numeric_limits<float>::max()))
            {
                return ValueError::OutOfRange;
            }

            const float narrowed = static_cast<float>(*number);

            if (std::optional<ValueError> error = CheckFloat32Range(resolved.Metadata, narrowed))
            {
                return error;
            }

            *static_cast<float*>(address) = narrowed;

            return std::nullopt;
        }

        // returns the bits to store, or nothing when the value does not fit the width and sign of the field
        std::optional<int64_t> FitInteger(const TypeInfo& type, const ScalarValue& value) noexcept
        {
            const uint64_t unsignedMax = type.IntegerWidthBytes == 8
                                             ? std::numeric_limits<uint64_t>::max()
                                             : (uint64_t{ 1 } << (type.IntegerWidthBytes * 8)) - 1;
            const uint64_t signedMax = unsignedMax >> 1;

            if (const uint64_t* unsignedInteger = std::get_if<uint64_t>(&value))
            {
                const uint64_t limit = type.IntegerIsSigned ? signedMax : unsignedMax;

                if (*unsignedInteger > limit)
                {
                    return std::nullopt;
                }

                return static_cast<int64_t>(*unsignedInteger);
            }

            const int64_t integer = std::get<int64_t>(value);

            if (!type.IntegerIsSigned)
            {
                const bool fits = integer >= 0 && std::cmp_less_equal(integer, unsignedMax);

                return fits ? std::optional<int64_t>{ integer } : std::nullopt;
            }

            const int64_t signedLimit = static_cast<int64_t>(signedMax);
            const bool fits = integer >= -signedLimit - 1 && integer <= signedLimit;

            return fits ? std::optional<int64_t>{ integer } : std::nullopt;
        }

        std::optional<ValueError> WriteInteger(const ResolvedValue& resolved, void* address,
                                               const ScalarValue& value) noexcept
        {
            if (!std::holds_alternative<int64_t>(value) && !std::holds_alternative<uint64_t>(value))
            {
                return ValueError::WrongValueType;
            }

            const std::optional<int64_t> bits = FitInteger(*resolved.Type, value);

            if (!bits.has_value())
            {
                return ValueError::OutOfRange;
            }

            if (std::optional<ValueError> error = CheckRange(resolved.Metadata, *ScalarAsNumber(value)))
            {
                return error;
            }

            WriteIntegerBits(*resolved.Type, address, *bits);

            return std::nullopt;
        }

        std::optional<ValueError> WriteEnumCase(const ResolvedValue& resolved, void* address,
                                                const ScalarValue& value) noexcept
        {
            const std::string_view* caseName = std::get_if<std::string_view>(&value);

            if (caseName == nullptr)
            {
                return ValueError::WrongValueType;
            }

            const EnumCaseInfo* enumCase = FindEnumCase(*resolved.Type, *caseName);

            if (enumCase == nullptr)
            {
                return ValueError::UnknownEnumCase;
            }

            WriteIntegerBits(*resolved.Type, address, enumCase->Value);

            return std::nullopt;
        }

        std::optional<ValueError> WriteBool(void* address, const ScalarValue& value) noexcept
        {
            const bool* flag = std::get_if<bool>(&value);

            if (flag == nullptr)
            {
                return ValueError::WrongValueType;
            }

            *static_cast<bool*>(address) = *flag;

            return std::nullopt;
        }

        std::optional<ValueError> WriteString(const ResolvedValue& resolved, void* address,
                                              const ScalarValue& value)
        {
            const std::string_view* text = std::get_if<std::string_view>(&value);

            if (text == nullptr)
            {
                return ValueError::WrongValueType;
            }

            if (resolved.Type->WriteString == nullptr)
            {
                return ValueError::ReadOnly;
            }

            resolved.Type->WriteString(address, *text);

            return std::nullopt;
        }

        std::optional<ValueError> WriteScalar(const ResolvedValue& resolved, void* address,
                                              const ScalarValue& value)
        {
            switch (resolved.Type->Kind)
            {
            case TypeKind::Bool:
                return WriteBool(address, value);
            case TypeKind::Int:
                return WriteInteger(resolved, address, value);
            case TypeKind::Float32:
                [[fallthrough]];
            case TypeKind::Float64:
                return WriteFloat(resolved, address, value);
            case TypeKind::String:
                return WriteString(resolved, address, value);
            case TypeKind::Enum:
                return WriteEnumCase(resolved, address, value);
            default:
                return ValueError::NotAScalar;
            }
        }
    }

    int64_t ReadIntegerBits(const TypeInfo& type, const void* value) noexcept
    {
        switch (type.IntegerWidthBytes)
        {
        case 1:
            return type.IntegerIsSigned ? ReadStored<int8_t>(value) : ReadStored<uint8_t>(value);
        case 2:
            return type.IntegerIsSigned ? ReadStored<int16_t>(value) : ReadStored<uint16_t>(value);
        case 4:
            return type.IntegerIsSigned ? ReadStored<int32_t>(value) : ReadStored<uint32_t>(value);
        default:
            return ReadStored<int64_t>(value);
        }
    }

    std::expected<ResolvedValue, PathError> ResolvePath(const TypeInfo& root, const void* object,
                                                        std::string_view path)
    {
        ResolvedValue resolved{ .Type = &root, .Address = object };

        size_t position = 0;

        while (position < path.size())
        {
            const int32_t offset = static_cast<int32_t>(position);
            const char first = path[position];
            std::optional<PathError> error;

            if (first == '[')
            {
                const std::optional<int64_t> index = ParseIndex(path, position);

                if (!index.has_value())
                {
                    return std::unexpected(PathError{ ValueError::MalformedPath, offset });
                }

                error = StepIntoElement(resolved, *index, offset);
            }
            else
            {
                const bool startsWithDot = first == '.';

                // a name must follow a dot, and a dot must separate a name from what comes before it
                if (startsWithDot == (position == 0))
                {
                    return std::unexpected(PathError{ ValueError::MalformedPath, offset });
                }

                position += startsWithDot ? 1 : 0;

                const std::string_view name = ParseFieldName(path, position);

                if (name.empty())
                {
                    return std::unexpected(PathError{ ValueError::MalformedPath, offset });
                }

                error = StepIntoField(resolved, name, offset);
            }

            if (error.has_value())
            {
                return std::unexpected(*error);
            }
        }

        return resolved;
    }

    std::expected<void, PathError> SetValue(const TypeInfo& root, void* object, std::string_view path,
                                            const ScalarValue& value)
    {
        std::expected<ResolvedValue, PathError> resolved = ResolvePath(root, object, path);

        if (!resolved.has_value())
        {
            return std::unexpected(resolved.error());
        }

        const int32_t pathEnd = static_cast<int32_t>(path.size());

        if (std::optional<PathError> error = UnwrapActiveValue(*resolved, pathEnd))
        {
            return std::unexpected(*error);
        }

        if (!resolved->Writable)
        {
            return std::unexpected(PathError{ ValueError::ReadOnly, pathEnd });
        }

        // the caller passed a non-const object, and ResolvePath only read through it
        void* address = const_cast<void*>(resolved->Address);

        if (std::optional<ValueError> error = WriteScalar(*resolved, address, value))
        {
            return std::unexpected(PathError{ *error, pathEnd });
        }

        return {};
    }

    std::string_view ValueErrorText(ValueError error) noexcept
    {
        switch (error)
        {
        case ValueError::Invalid:
            return "no error";
        case ValueError::MalformedPath:
            return "the path is malformed";
        case ValueError::UnknownField:
            return "the type has no field with this name";
        case ValueError::NotAStruct:
            return "a field name follows a value that is not a struct";
        case ValueError::NotASequence:
            return "an index follows a value that is not a sequence";
        case ValueError::IndexOutOfRange:
            return "the index is past the end of the sequence";
        case ValueError::NoActiveValue:
            return "the path crosses an empty optional or a valueless variant";
        case ValueError::ReadOnly:
            return "the value is read-only";
        case ValueError::NotAScalar:
            return "only a bool, number, string or enum value can be written";
        case ValueError::WrongValueType:
            return "the new value has the wrong type for this field";
        case ValueError::UnknownEnumCase:
            return "the enum has no case with this name";
        case ValueError::NotFinite:
            return "the new value is NaN or infinite";
        case ValueError::OutOfRange:
            return "the new value is outside the limits of this field";
        }

        return "unknown error";
    }
}
