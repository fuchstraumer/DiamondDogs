#pragma once
#ifndef DIAMOND_DOGS_FOUNDATION_CONTENT_CANONICAL_SINK_HPP
#define DIAMOND_DOGS_FOUNDATION_CONTENT_CANONICAL_SINK_HPP

#include <cstdint>
#include <string_view>

namespace foundation::content
{
    // One byte before every write in the canonical encoding. Changing a value here changes every key, so the
    // known-answer tests pin them.
    enum class CanonicalTag : uint8_t
    {
        Invalid = 0,
        Bool = 1,
        Int = 2,
        Float32 = 3,
        Float64 = 4,
        String = 5,
        Enum = 6,
        Struct = 7,
        EndStruct = 8,
        Field = 9,
        Array = 10,
        EndArray = 11,
        Optional = 12,
        Variant = 13,
        Key = 14,
    };

    class CanonicalSink
    {
    public:
        CanonicalSink() = default;
        CanonicalSink(const CanonicalSink&) = delete;
        CanonicalSink& operator=(const CanonicalSink&) = delete;
        virtual ~CanonicalSink() = default;

        virtual void BeginStruct(std::string_view type_name) = 0;
        virtual void EndStruct() = 0;
        virtual void BeginField(std::string_view field_name) = 0;
        virtual void WriteBool(bool value) = 0;
        // value holds the integer's bits, sign-extended or zero-extended to 64. Only the low width_bytes are written.
        virtual void WriteInt(int64_t value, int32_t width_bytes, bool is_signed) = 0;
        virtual void WriteFloat32(float value) = 0;
        virtual void WriteFloat64(double value) = 0;
        virtual void WriteString(std::string_view value) = 0;
        virtual void WriteEnum(std::string_view type_name, std::string_view case_name) = 0;
        virtual void BeginArray(uint64_t count) = 0;
        virtual void EndArray() = 0;
        // the value follows when present
        virtual void WriteOptional(bool present) = 0;
        virtual void WriteVariant(std::string_view alternative_name) = 0;
        virtual void WriteKeyBytes(uint64_t low, uint64_t high) = 0;
    };
}

#endif // !DIAMOND_DOGS_FOUNDATION_CONTENT_CANONICAL_SINK_HPP
