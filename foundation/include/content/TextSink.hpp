#pragma once
#ifndef DIAMOND_DOGS_FOUNDATION_CONTENT_TEXT_SINK_HPP
#define DIAMOND_DOGS_FOUNDATION_CONTENT_TEXT_SINK_HPP

#include "content/CanonicalSink.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace foundation::content
{
    // The same walk as HashSink, written as indented text with one field per line. Two texts diff to show why a
    // key changed. Floats print as a decimal value and as their hex bits.
    class TextSink final : public CanonicalSink
    {
    public:
        TextSink() = default;

        void BeginStruct(std::string_view type_name) override;
        void EndStruct() override;
        void BeginField(std::string_view field_name) override;
        void WriteBool(bool value) override;
        void WriteInt(int64_t value, int32_t width_bytes, bool is_signed) override;
        void WriteFloat32(float value) override;
        void WriteFloat64(double value) override;
        void WriteString(std::string_view value) override;
        void WriteEnum(std::string_view type_name, std::string_view case_name) override;
        void BeginArray(uint64_t count) override;
        void EndArray() override;
        void WriteOptional(bool present) override;
        void WriteVariant(std::string_view alternative_name) override;
        void WriteKeyBytes(uint64_t low, uint64_t high) override;

        [[nodiscard]] const std::string& Text() const noexcept;

    private:
        // returns the label for the next value: the pending label, or the index inside an open array
        std::string takeLabel();
        // writes the indent, the label and the value as one line
        void appendLine(std::string_view value);
        void openBlock(std::string_view opener, int64_t first_index);
        void closeBlock(std::string_view closer);

        std::string text;
        // the field name, "some " or the variant alternative, written in front of the next value
        std::string pendingLabel;
        // the index of the next element for each open array. -1 marks an open struct.
        std::vector<int64_t> blockStack;
    };
}

#endif // !DIAMOND_DOGS_FOUNDATION_CONTENT_TEXT_SINK_HPP
