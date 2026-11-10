#pragma once
#ifndef DIAMOND_DOGS_FOUNDATION_CONTENT_JSON_SINK_HPP
#define DIAMOND_DOGS_FOUNDATION_CONTENT_JSON_SINK_HPP
#include "content/CanonicalSink.hpp"
#include "content/JsonWriter.hpp"
#include <cstdint>
#include <string>
#include <string_view>

namespace foundation::content
{
    /** Writes a value as one JSON document. A struct becomes an object, a sequence an array, and an enum its
     *  case name. An empty optional becomes null. A struct that is a variant alternative gains a "$type"
     *  member. JSON has no NaN or infinity, so these become the strings "NaN", "Infinity" and "-Infinity". */
    class JsonSink final : public CanonicalSink
    {
    public:
        explicit JsonSink(JsonStyle style) noexcept;

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

        /** @brief Returns the finished document. Fails when a container was never closed. */
        [[nodiscard]] JsonResult<std::string> Finish() noexcept;

    private:
        JsonWriter writer;
        // the alternative name waits here until its value arrives, because the sink has no end of variant
        // call
        std::string_view pendingAlternative;
    };
}

#endif // !DIAMOND_DOGS_FOUNDATION_CONTENT_JSON_SINK_HPP
