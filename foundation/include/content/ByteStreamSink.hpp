#pragma once
#ifndef DIAMOND_DOGS_FOUNDATION_CONTENT_BYTE_STREAM_SINK_HPP
#define DIAMOND_DOGS_FOUNDATION_CONTENT_BYTE_STREAM_SINK_HPP

#include "content/CanonicalSink.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace foundation::content
{
    // The one writer of the canonical byte layout: every tag, width, length and little-endian value is written
    // here, once. A derived class only says where the bytes go, so two sinks cannot disagree about the layout.
    // The CanonicalSink functions are final for that reason.
    class ByteStreamSink : public CanonicalSink
    {
    public:
        void BeginStruct(std::string_view type_name) final;
        void EndStruct() final;
        void BeginField(std::string_view field_name) final;
        void WriteBool(bool value) final;
        void WriteInt(int64_t value, int32_t width_bytes, bool is_signed) final;
        void WriteFloat32(float value) final;
        void WriteFloat64(double value) final;
        void WriteString(std::string_view value) final;
        void WriteEnum(std::string_view type_name, std::string_view case_name) final;
        void BeginArray(uint64_t count) final;
        void EndArray() final;
        void WriteOptional(bool present) final;
        void WriteVariant(std::string_view alternative_name) final;
        void WriteKeyBytes(uint64_t low, uint64_t high) final;

    protected:
        // Receives the bytes of the stream in order. A write of one value can call it more than once.
        virtual void Append(const std::byte* data, size_t length) = 0;

    private:
        void appendTag(CanonicalTag tag);
        void appendLittleEndian(uint64_t value, int32_t width_bytes);
        void appendString(std::string_view text);
    };
}

#endif // !DIAMOND_DOGS_FOUNDATION_CONTENT_BYTE_STREAM_SINK_HPP
