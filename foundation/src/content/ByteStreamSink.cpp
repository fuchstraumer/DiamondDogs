#include "content/ByteStreamSink.hpp"
#include "content/CanonicalSink.hpp"
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace foundation::content
{
    static_assert(std::endian::native == std::endian::little, "the encoding is little-endian");

    void ByteStreamSink::BeginStruct(std::string_view type_name)
    {
        appendTag(CanonicalTag::Struct);
        appendString(type_name);
    }

    void ByteStreamSink::EndStruct()
    {
        appendTag(CanonicalTag::EndStruct);
    }

    void ByteStreamSink::BeginField(std::string_view field_name)
    {
        appendTag(CanonicalTag::Field);
        appendString(field_name);
    }

    void ByteStreamSink::WriteBool(bool value)
    {
        appendTag(CanonicalTag::Bool);
        appendLittleEndian(value ? 1 : 0, 1);
    }

    void ByteStreamSink::WriteInt(int64_t value, int32_t width_bytes, bool is_signed)
    {
        appendTag(CanonicalTag::Int);
        appendLittleEndian(static_cast<uint64_t>(width_bytes), 1);
        appendLittleEndian(is_signed ? 1 : 0, 1);
        appendLittleEndian(static_cast<uint64_t>(value), width_bytes);
    }

    void ByteStreamSink::WriteFloat32(float value)
    {
        appendTag(CanonicalTag::Float32);
        appendLittleEndian(std::bit_cast<uint32_t>(value), 4);
    }

    void ByteStreamSink::WriteFloat64(double value)
    {
        appendTag(CanonicalTag::Float64);
        appendLittleEndian(std::bit_cast<uint64_t>(value), 8);
    }

    void ByteStreamSink::WriteString(std::string_view value)
    {
        appendTag(CanonicalTag::String);
        appendString(value);
    }

    void ByteStreamSink::WriteEnum(std::string_view type_name, std::string_view case_name)
    {
        appendTag(CanonicalTag::Enum);
        appendString(type_name);
        appendString(case_name);
    }

    void ByteStreamSink::BeginArray(uint64_t count)
    {
        appendTag(CanonicalTag::Array);
        appendLittleEndian(count, 8);
    }

    void ByteStreamSink::EndArray()
    {
        appendTag(CanonicalTag::EndArray);
    }

    void ByteStreamSink::WriteOptional(bool present)
    {
        appendTag(CanonicalTag::Optional);
        appendLittleEndian(present ? 1 : 0, 1);
    }

    void ByteStreamSink::WriteVariant(std::string_view alternative_name)
    {
        appendTag(CanonicalTag::Variant);
        appendString(alternative_name);
    }

    void ByteStreamSink::WriteKeyBytes(uint64_t low, uint64_t high)
    {
        appendTag(CanonicalTag::Key);
        appendLittleEndian(low, 8);
        appendLittleEndian(high, 8);
    }

    void ByteStreamSink::appendTag(CanonicalTag tag)
    {
        const std::byte byte = static_cast<std::byte>(tag);
        Append(&byte, 1);
    }

    void ByteStreamSink::appendLittleEndian(uint64_t value, int32_t width_bytes)
    {
        std::byte bytes[8]{};

        for (int32_t index = 0; index < width_bytes; ++index)
        {
            bytes[index] = static_cast<std::byte>(value >> (8 * index));
        }

        Append(bytes, static_cast<size_t>(width_bytes));
    }

    void ByteStreamSink::appendString(std::string_view text)
    {
        appendLittleEndian(text.size(), 8);
        Append(reinterpret_cast<const std::byte*>(text.data()), text.size());
    }
}
