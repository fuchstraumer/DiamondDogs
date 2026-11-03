#pragma once
#ifndef DIAMOND_DOGS_LOOM_JSON_WRITER_HPP
#define DIAMOND_DOGS_LOOM_JSON_WRITER_HPP
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

// A copy of Lodestone's tools/json/JsonWriter (same author, MIT). Changes: the style is an explicit argument, not a
// default; a control character escapes as \u00XX with both hex digits; Loom's namespace and spacing.
namespace loom::detail
{

    enum class JsonWriterError : uint8_t
    {
        Invalid = 0,
        Success = 1,
        UnbalancedContainers = 2,
    };

    template<typename T>
    using JsonResult = std::expected<T, JsonWriterError>;

    [[nodiscard]] std::string_view ToString(JsonWriterError error) noexcept;

    enum class JsonStyle : uint8_t
    {
        Invalid = 0,
        Compact = 1,    // one line: what JSON Lines needs
        Pretty = 2,     // a new line per member, four-space indent
    };

    // Writes one JSON document. The only checked failure is an unclosed container at the end.
    class JsonWriter final
    {
    public:
        explicit JsonWriter(JsonStyle style) noexcept;

        void BeginObject() noexcept;
        void EndObject() noexcept;

        void BeginArray() noexcept;
        void EndArray() noexcept;

        // An object member name. Only valid directly inside an object.
        void Key(std::string_view key) noexcept;

        void String(std::string_view value) noexcept;
        void Int(int64_t value) noexcept;
        void UInt(uint64_t value) noexcept;
        void Double(double value) noexcept;
        void Bool(bool value) noexcept;
        void Null() noexcept;

        void KeyString(std::string_view key, std::string_view value) noexcept;
        void KeyInt(std::string_view key, int64_t value) noexcept;
        void KeyUInt(std::string_view key, uint64_t value) noexcept;
        void KeyDouble(std::string_view key, double value) noexcept;
        void KeyBool(std::string_view key, bool value) noexcept;
        void KeyNull(std::string_view key) noexcept;

        // Hands back the finished document. Fails only when a container was never closed.
        [[nodiscard]] JsonResult<std::string> Finish() noexcept;

    private:
        enum class ContainerKind : uint8_t
        {
            Invalid = 0,
            Object,
            Array,
        };

        struct Frame
        {
            ContainerKind Kind{ ContainerKind::Invalid };
            uint32_t ElementCount{ 0u };
        };

        void beginContainer(ContainerKind kind, char opening) noexcept;
        void endContainer(char closing) noexcept;
        void beforeValue() noexcept;
        void writeIndent(size_t depth) noexcept;
        void writeEscaped(std::string_view text) noexcept;

        std::string buffer;
        std::vector<Frame> stack;
        bool pretty{ false };
    };

}

#endif // !DIAMOND_DOGS_LOOM_JSON_WRITER_HPP
