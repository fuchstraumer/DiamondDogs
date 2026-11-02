#include "content/TextSink.hpp"

#include "content/Key.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace
{
    // an open struct has no element index
    constexpr int64_t k_StructBlock = -1;

    std::string FormatInt(int64_t value, int32_t width_bytes, bool is_signed);
}

namespace foundation::content
{
    void TextSink::BeginStruct(std::string_view type_name)
    {
        openBlock(std::format("{} {{", type_name), k_StructBlock);
    }

    void TextSink::EndStruct()
    {
        closeBlock("}");
    }

    void TextSink::BeginField(std::string_view field_name)
    {
        pendingLabel = std::format("{}: ", field_name);
    }

    void TextSink::WriteBool(bool value)
    {
        appendLine(value ? "bool true" : "bool false");
    }

    void TextSink::WriteInt(int64_t value, int32_t width_bytes, bool is_signed)
    {
        appendLine(FormatInt(value, width_bytes, is_signed));
    }

    void TextSink::WriteFloat32(float value)
    {
        const uint32_t bits = std::bit_cast<uint32_t>(value);
        appendLine(std::format("f32 {} (0x{:08x})", value, bits));
    }

    void TextSink::WriteFloat64(double value)
    {
        const uint64_t bits = std::bit_cast<uint64_t>(value);
        appendLine(std::format("f64 {} (0x{:016x})", value, bits));
    }

    void TextSink::WriteString(std::string_view value)
    {
        appendLine(std::format("string \"{}\"", value));
    }

    void TextSink::WriteEnum(std::string_view type_name, std::string_view case_name)
    {
        appendLine(std::format("enum {}::{}", type_name, case_name));
    }

    void TextSink::BeginArray(uint64_t count)
    {
        openBlock(std::format("array({}) [", count), 0);
    }

    void TextSink::EndArray()
    {
        closeBlock("]");
    }

    void TextSink::WriteOptional(bool present)
    {
        const std::string label = takeLabel();

        if (present)
        {
            pendingLabel = label + "some ";
            return;
        }

        pendingLabel = label;
        appendLine("none");
    }

    void TextSink::WriteVariant(std::string_view alternative_name)
    {
        const std::string label = takeLabel();
        pendingLabel = std::format("{}variant({}) ", label, alternative_name);
    }

    void TextSink::WriteKeyBytes(uint64_t low, uint64_t high)
    {
        std::array<char, 32> hex{};
        WriteHexWords(high, low, hex);
        appendLine(std::format("key {}", std::string_view{ hex.data(), hex.size() }));
    }

    const std::string& TextSink::Text() const noexcept
    {
        return text;
    }

    std::string TextSink::takeLabel()
    {
        std::string label;

        if (!pendingLabel.empty())
        {
            label = std::move(pendingLabel);
            pendingLabel.clear();
            return label;
        }

        if (blockStack.empty() || blockStack.back() == k_StructBlock)
        {
            return label;
        }

        int64_t& next_index = blockStack.back();
        label = std::format("[{}]: ", next_index);
        ++next_index;

        return label;
    }

    void TextSink::appendLine(std::string_view value)
    {
        const std::string label = takeLabel();
        const size_t indent = blockStack.size() * 2;

        text.append(indent, ' ');
        text.append(label);
        text.append(value);
        text.push_back('\n');
    }

    void TextSink::openBlock(std::string_view opener, int64_t first_index)
    {
        appendLine(opener);
        blockStack.push_back(first_index);
    }

    void TextSink::closeBlock(std::string_view closer)
    {
        blockStack.pop_back();

        // a closer has no label, so it does not use AppendLine
        const size_t indent = blockStack.size() * 2;

        text.append(indent, ' ');
        text.append(closer);
        text.push_back('\n');
    }
}

namespace
{
    std::string FormatInt(int64_t value, int32_t width_bytes, bool is_signed)
    {
        const int32_t widthBits = width_bytes * 8;

        if (is_signed)
        {
            return std::format("i{} {}", widthBits, value);
        }

        const uint64_t mask = width_bytes >= 8 ? ~uint64_t{ 0 } : (uint64_t{ 1 } << widthBits) - 1;
        const uint64_t unsignedValue = static_cast<uint64_t>(value) & mask;

        return std::format("u{} {}", widthBits, unsignedValue);
    }
}
