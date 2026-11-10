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
        constexpr PathResult k_PathSucceeded{ ValueResult::Success, 0 };

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

        [[nodiscard]] PathResult UnwrapActiveValue(ResolvedValue& resolved, int32_t offset) noexcept
        {
            while (resolved.Type->Kind == TypeKind::Optional || resolved.Type->Kind == TypeKind::Variant)
            {
                const bool isOptional = resolved.Type->Kind == TypeKind::Optional;
                const void* contained = isOptional ? resolved.Type->OptionalValue(resolved.Address)
                                                   : resolved.Type->ActiveValue(resolved.Address);

                if (contained == nullptr)
                {
                    return PathResult{ ValueResult::NoActiveValue, offset };
                }

                const TypeInfo& containedType =
                    isOptional ? resolved.Type->Element()
                               : resolved.Type->Alternatives[static_cast<size_t>(
                                     resolved.Type->ActiveIndex(resolved.Address))]();

                resolved.Type = &containedType;
                resolved.Address = contained;
            }

            return k_PathSucceeded;
        }

        [[nodiscard]] PathResult StepIntoField(ResolvedValue& resolved,
                                              std::string_view name,
                                              int32_t offset) noexcept
        {
            const PathResult unwrapped = UnwrapActiveValue(resolved, offset);
            if (!unwrapped.Succeeded())
            {
                return unwrapped;
            }

            if (resolved.Type->Kind != TypeKind::Struct)
            {
                return PathResult{ ValueResult::NotAStruct, offset };
            }

            const FieldInfo* field = FindField(*resolved.Type, name);

            if (field == nullptr)
            {
                return PathResult{ ValueResult::UnknownField, offset };
            }

            resolved.Address = FieldAddress(*field, resolved.Address);
            resolved.Type = &field->Type();
            resolved.Metadata = &field->Metadata;
            resolved.Writable = resolved.Writable && !field->Metadata.ReadOnly;

            return k_PathSucceeded;
        }

        [[nodiscard]] PathResult StepIntoElement(ResolvedValue& resolved,
                                                int64_t index,
                                                int32_t offset) noexcept
        {
            const PathResult unwrapped = UnwrapActiveValue(resolved, offset);
            if (!unwrapped.Succeeded())
            {
                return unwrapped;
            }

            if (resolved.Type->Kind != TypeKind::Sequence)
            {
                return PathResult{ ValueResult::NotASequence, offset };
            }

            if (index >= resolved.Type->ElementCount(resolved.Address))
            {
                return PathResult{ ValueResult::IndexOutOfRange, offset };
            }

            resolved.Address = resolved.Type->ElementAt(resolved.Address, index);
            resolved.Writable = resolved.Writable && resolved.Type->ElementsAreWritable;
            resolved.Type = &resolved.Type->Element();

            return k_PathSucceeded;
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

        [[nodiscard]] ValueResult CheckRange(const FieldMetadata* metadata, double value) noexcept
        {
            if (metadata == nullptr || !metadata->Range.has_value())
            {
                return ValueResult::Success;
            }

            if (value < metadata->Range->Min || value > metadata->Range->Max)
            {
                return ValueResult::OutOfRange;
            }

            return ValueResult::Success;
        }

        float NarrowLimit(double limit) noexcept
        {
            constexpr double k_FloatMax = std::numeric_limits<float>::max();

            return static_cast<float>(std::clamp(limit, -k_FloatMax, k_FloatMax));
        }

        // compares at float precision, so that a value read from the field and written back is never refused. A
        // limit of 0.1 is below 0.1f as a double.
        [[nodiscard]] ValueResult CheckFloat32Range(const FieldMetadata* metadata, float value) noexcept
        {
            if (metadata == nullptr || !metadata->Range.has_value())
            {
                return ValueResult::Success;
            }

            if (value < NarrowLimit(metadata->Range->Min) || value > NarrowLimit(metadata->Range->Max))
            {
                return ValueResult::OutOfRange;
            }

            return ValueResult::Success;
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

        [[nodiscard]] ValueResult WriteFloat(const ResolvedValue& resolved,
                                            void* address,
                                            const ScalarValue& value) noexcept
        {
            const std::optional<double> number = ScalarAsNumber(value);

            if (!number.has_value())
            {
                return ValueResult::WrongValueType;
            }

            if (!std::isfinite(*number))
            {
                return ValueResult::NotFinite;
            }

            if (resolved.Type->Kind == TypeKind::Float64)
            {
                const ValueResult inRange = CheckRange(resolved.Metadata, *number);
                if (inRange != ValueResult::Success)
                {
                    return inRange;
                }

                *static_cast<double*>(address) = *number;

                return ValueResult::Success;
            }

            if (std::abs(*number) > static_cast<double>(std::numeric_limits<float>::max()))
            {
                return ValueResult::OutOfRange;
            }

            const float narrowed = static_cast<float>(*number);
            const ValueResult inRange = CheckFloat32Range(resolved.Metadata, narrowed);
            if (inRange != ValueResult::Success)
            {
                return inRange;
            }

            *static_cast<float*>(address) = narrowed;

            return ValueResult::Success;
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

        [[nodiscard]] ValueResult WriteInteger(const ResolvedValue& resolved,
                                              void* address,
                                              const ScalarValue& value) noexcept
        {
            if (!std::holds_alternative<int64_t>(value) && !std::holds_alternative<uint64_t>(value))
            {
                return ValueResult::WrongValueType;
            }

            const std::optional<int64_t> bits = FitInteger(*resolved.Type, value);

            if (!bits.has_value())
            {
                return ValueResult::OutOfRange;
            }

            const ValueResult inRange = CheckRange(resolved.Metadata, *ScalarAsNumber(value));
            if (inRange != ValueResult::Success)
            {
                return inRange;
            }

            WriteIntegerBits(*resolved.Type, address, *bits);

            return ValueResult::Success;
        }

        [[nodiscard]] ValueResult WriteEnumCase(const ResolvedValue& resolved,
                                               void* address,
                                               const ScalarValue& value) noexcept
        {
            const std::string_view* caseName = std::get_if<std::string_view>(&value);

            if (caseName == nullptr)
            {
                return ValueResult::WrongValueType;
            }

            const EnumCaseInfo* enumCase = FindEnumCase(*resolved.Type, *caseName);

            if (enumCase == nullptr)
            {
                return ValueResult::UnknownEnumCase;
            }

            WriteIntegerBits(*resolved.Type, address, enumCase->Value);

            return ValueResult::Success;
        }

        [[nodiscard]] ValueResult WriteBool(void* address, const ScalarValue& value) noexcept
        {
            const bool* flag = std::get_if<bool>(&value);

            if (flag == nullptr)
            {
                return ValueResult::WrongValueType;
            }

            *static_cast<bool*>(address) = *flag;

            return ValueResult::Success;
        }

        [[nodiscard]] ValueResult WriteString(const ResolvedValue& resolved,
                                             void* address,
                                             const ScalarValue& value)
        {
            const std::string_view* text = std::get_if<std::string_view>(&value);

            if (text == nullptr)
            {
                return ValueResult::WrongValueType;
            }

            if (resolved.Type->WriteString == nullptr)
            {
                return ValueResult::ReadOnly;
            }

            resolved.Type->WriteString(address, *text);

            return ValueResult::Success;
        }

        [[nodiscard]] ValueResult WriteScalar(const ResolvedValue& resolved,
                                             void* address,
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
                return ValueResult::NotAScalar;
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

    std::expected<ResolvedValue, PathResult> ResolvePath(const TypeInfo& root, const void* object,
                                                        std::string_view path)
    {
        ResolvedValue resolved{ .Type = &root, .Address = object };

        size_t position = 0;

        while (position < path.size())
        {
            const int32_t offset = static_cast<int32_t>(position);
            const char first = path[position];
            PathResult step{ k_PathSucceeded };

            if (first == '[')
            {
                const std::optional<int64_t> index = ParseIndex(path, position);

                if (!index.has_value())
                {
                    return std::unexpected(PathResult{ ValueResult::MalformedPath, offset });
                }

                step = StepIntoElement(resolved, *index, offset);
            }
            else
            {
                const bool startsWithDot = first == '.';

                // a name must follow a dot, and a dot must separate a name from what comes before it
                if (startsWithDot == (position == 0))
                {
                    return std::unexpected(PathResult{ ValueResult::MalformedPath, offset });
                }

                position += startsWithDot ? 1 : 0;

                const std::string_view name = ParseFieldName(path, position);

                if (name.empty())
                {
                    return std::unexpected(PathResult{ ValueResult::MalformedPath, offset });
                }

                step = StepIntoField(resolved, name, offset);
            }

            if (!step.Succeeded())
            {
                return std::unexpected(step);
            }
        }

        return resolved;
    }

    PathResult SetValue(const TypeInfo& root, void* object, std::string_view path, const ScalarValue& value)
    {
        std::expected<ResolvedValue, PathResult> resolved = ResolvePath(root, object, path);

        if (!resolved.has_value())
        {
            return resolved.error();
        }

        const int32_t pathEnd = static_cast<int32_t>(path.size());
        const PathResult unwrapped = UnwrapActiveValue(*resolved, pathEnd);
        if (!unwrapped.Succeeded())
        {
            return unwrapped;
        }

        if (!resolved->Writable)
        {
            return PathResult{ ValueResult::ReadOnly, pathEnd };
        }

        // the caller passed a non-const object, and ResolvePath only read through it
        void* address = const_cast<void*>(resolved->Address);

        return PathResult{ WriteScalar(*resolved, address, value), pathEnd };
    }

    std::string_view ValueResultText(ValueResult error) noexcept
    {
        switch (error)
        {
        case ValueResult::Invalid:
            return "the error code was never set";
        case ValueResult::Success:
            return "no error";
        case ValueResult::MalformedPath:
            return "the path is malformed";
        case ValueResult::UnknownField:
            return "the type has no field with this name";
        case ValueResult::NotAStruct:
            return "a field name follows a value that is not a struct";
        case ValueResult::NotASequence:
            return "an index follows a value that is not a sequence";
        case ValueResult::IndexOutOfRange:
            return "the index is past the end of the sequence";
        case ValueResult::NoActiveValue:
            return "the path crosses an empty optional or a valueless variant";
        case ValueResult::ReadOnly:
            return "the value is read-only";
        case ValueResult::NotAScalar:
            return "only a bool, number, string or enum value can be written";
        case ValueResult::WrongValueType:
            return "the new value has the wrong type for this field";
        case ValueResult::UnknownEnumCase:
            return "the enum has no case with this name";
        case ValueResult::NotFinite:
            return "the new value is NaN or infinite";
        case ValueResult::OutOfRange:
            return "the new value is outside the limits of this field";
        }

        return "unknown error";
    }
}
