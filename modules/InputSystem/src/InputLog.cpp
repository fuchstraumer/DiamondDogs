#include "InputLog.hpp"
#include "InputTypes.hpp"
#include <charconv>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{
    constexpr int32_t k_NumberBufferSize = 32;
    constexpr int32_t k_ReservedBytesPerEvent = 128;
    constexpr int64_t k_MaxCodePoint = 0x10FFFF;

    void AppendInteger(std::string& out, int64_t value);

    void AppendFloat(std::string& out, float value);

    void AppendEvent(std::string& out, const input::InputEvent& event);

    [[nodiscard]] std::optional<std::string_view> FindValue(std::string_view line, std::string_view key);

    [[nodiscard]] input::InputLogError ReadIntegerField(std::string_view line, std::string_view key, int64_t min_value,
        int64_t max_value, int64_t& out);

    [[nodiscard]] input::InputLogError ReadFloatField(std::string_view line, std::string_view key, float& out);

    [[nodiscard]] input::InputLogError ReadNameField(std::string_view line, std::string_view key,
        std::string_view& out);

    [[nodiscard]] input::InputLogResult<input::InputEvent> ParseLine(std::string_view line);

    [[nodiscard]] std::string_view TrimLine(std::string_view line);
}

namespace input
{
    std::string_view ToString(InputLogError error) noexcept
    {
        switch (error)
        {
        case InputLogError::Invalid: return "Invalid";
        case InputLogError::Success: return "Success";
        case InputLogError::MalformedLine: return "MalformedLine";
        case InputLogError::MissingKey: return "MissingKey";
        case InputLogError::UnknownControl: return "UnknownControl";
        case InputLogError::UnknownKind: return "UnknownKind";
        case InputLogError::UnknownDevice: return "UnknownDevice";
        case InputLogError::MalformedNumber: return "MalformedNumber";
        default: return "Unknown";
        }
    }

    std::string WriteInputLog(std::span<const InputEvent> events)
    {
        std::string text;
        text.reserve(events.size() * k_ReservedBytesPerEvent);

        for (const InputEvent& event : events)
        {
            AppendEvent(text, event);
        }

        return text;
    }

    InputLogResult<std::vector<InputEvent>> ReadInputLog(std::string_view text)
    {
        std::vector<InputEvent> events;

        while (!text.empty())
        {
            const size_t lineEnd = text.find('\n');
            const std::string_view rawLine = text.substr(0, lineEnd);
            text.remove_prefix(lineEnd == std::string_view::npos ? text.size() : lineEnd + 1);

            const std::string_view line = TrimLine(rawLine);

            if (line.empty())
            {
                continue;
            }

            InputLogResult<InputEvent> parsed = ParseLine(line);

            if (!parsed)
            {
                return std::unexpected(parsed.error());
            }

            events.push_back(*parsed);
        }

        return events;
    }
}

namespace
{
    void AppendInteger(std::string& out, int64_t value)
    {
        char buffer[k_NumberBufferSize];
        const std::to_chars_result result = std::to_chars(buffer, buffer + k_NumberBufferSize, value);

        out.append(buffer, result.ptr);
    }

    void AppendFloat(std::string& out, float value)
    {
        char buffer[k_NumberBufferSize];
        const std::to_chars_result result = std::to_chars(buffer, buffer + k_NumberBufferSize, value);

        out.append(buffer, result.ptr);
    }

    void AppendEvent(std::string& out, const input::InputEvent& event)
    {
        out += "{\"t\":";
        AppendInteger(out, event.TimestampNs);
        out += ",\"kind\":\"";
        out += input::ToString(event.Kind);
        out += "\",\"control\":\"";
        out += input::ToString(event.Control);
        out += "\",\"device\":\"";
        out += input::ToString(event.Device);
        out += "\",\"index\":";
        AppendInteger(out, event.DeviceIndex);
        out += ",\"mods\":";
        AppendInteger(out, static_cast<int64_t>(event.Modifiers));
        out += ",\"flags\":";
        AppendInteger(out, static_cast<int64_t>(event.Flags));
        out += ",\"x\":";
        AppendFloat(out, event.X);
        out += ",\"y\":";
        AppendFloat(out, event.Y);
        out += ",\"cp\":";
        AppendInteger(out, event.CodePoint);
        out += "}\n";
    }

    std::optional<std::string_view> FindValue(std::string_view line, std::string_view key)
    {
        size_t searchStart = 0;

        while (true)
        {
            const size_t keyStart = line.find(key, searchStart);

            if (keyStart == std::string_view::npos)
            {
                return std::nullopt;
            }

            searchStart = keyStart + 1;

            const size_t afterKey = keyStart + key.size();
            const bool quotedBefore = keyStart > 0 && line[keyStart - 1] == '"';
            const bool colonAfter = line.substr(afterKey, 2) == "\":";

            if (!quotedBefore || !colonAfter)
            {
                continue;
            }

            const std::string_view rest = line.substr(afterKey + 2);
            const size_t valueEnd = rest.find_first_of(",}");

            return rest.substr(0, valueEnd);
        }
    }

    input::InputLogError ReadIntegerField(std::string_view line, std::string_view key, int64_t min_value,
        int64_t max_value, int64_t& out)
    {
        const std::optional<std::string_view> token = FindValue(line, key);

        if (!token)
        {
            return input::InputLogError::MissingKey;
        }

        int64_t value = 0;
        const char* tokenEnd = token->data() + token->size();
        const std::from_chars_result result = std::from_chars(token->data(), tokenEnd, value);
        const bool wholeToken = result.ec == std::errc{} && result.ptr == tokenEnd;

        if (!wholeToken || value < min_value || value > max_value)
        {
            return input::InputLogError::MalformedNumber;
        }

        out = value;

        return input::InputLogError::Success;
    }

    input::InputLogError ReadFloatField(std::string_view line, std::string_view key, float& out)
    {
        const std::optional<std::string_view> token = FindValue(line, key);

        if (!token)
        {
            return input::InputLogError::MissingKey;
        }

        float value = 0.0f;
        const char* tokenEnd = token->data() + token->size();
        const std::from_chars_result result = std::from_chars(token->data(), tokenEnd, value);
        const bool wholeToken = result.ec == std::errc{} && result.ptr == tokenEnd;

        if (!wholeToken)
        {
            return input::InputLogError::MalformedNumber;
        }

        out = value;

        return input::InputLogError::Success;
    }

    input::InputLogError ReadNameField(std::string_view line, std::string_view key, std::string_view& out)
    {
        const std::optional<std::string_view> token = FindValue(line, key);

        if (!token)
        {
            return input::InputLogError::MissingKey;
        }

        const bool quoted = token->size() >= 2 && token->front() == '"' && token->back() == '"';

        if (!quoted)
        {
            return input::InputLogError::MalformedLine;
        }

        out = token->substr(1, token->size() - 2);

        return input::InputLogError::Success;
    }

    input::InputLogResult<input::InputEvent> ParseLine(std::string_view line)
    {
        if (line.front() != '{' || line.back() != '}')
        {
            return std::unexpected(input::InputLogError::MalformedLine);
        }

        input::InputEvent event{};
        int64_t timestamp = 0;
        int64_t deviceIndex = 0;
        int64_t modifiers = 0;
        int64_t flags = 0;
        int64_t codePoint = 0;
        std::string_view kindName;
        std::string_view controlName;
        std::string_view deviceName;

        input::InputLogError error = ReadIntegerField(line, "t", INT64_MIN, INT64_MAX, timestamp);

        if (error == input::InputLogError::Success)
        {
            error = ReadNameField(line, "kind", kindName);
        }

        if (error == input::InputLogError::Success)
        {
            error = ReadNameField(line, "control", controlName);
        }

        if (error == input::InputLogError::Success)
        {
            error = ReadNameField(line, "device", deviceName);
        }

        if (error == input::InputLogError::Success)
        {
            error = ReadIntegerField(line, "index", 0, UINT8_MAX, deviceIndex);
        }

        if (error == input::InputLogError::Success)
        {
            error = ReadIntegerField(line, "mods", 0, UINT8_MAX, modifiers);
        }

        if (error == input::InputLogError::Success)
        {
            error = ReadIntegerField(line, "flags", 0, UINT8_MAX, flags);
        }

        if (error == input::InputLogError::Success)
        {
            error = ReadFloatField(line, "x", event.X);
        }

        if (error == input::InputLogError::Success)
        {
            error = ReadFloatField(line, "y", event.Y);
        }

        if (error == input::InputLogError::Success)
        {
            error = ReadIntegerField(line, "cp", 0, k_MaxCodePoint, codePoint);
        }

        if (error != input::InputLogError::Success)
        {
            return std::unexpected(error);
        }

        const std::optional<input::InputEventKind> kind = input::ParseInputEventKind(kindName);

        if (!kind)
        {
            return std::unexpected(input::InputLogError::UnknownKind);
        }

        const std::optional<input::ControlId> control = input::ParseControlId(controlName);

        if (!control)
        {
            return std::unexpected(input::InputLogError::UnknownControl);
        }

        const std::optional<input::DeviceKind> device = input::ParseDeviceKind(deviceName);

        if (!device)
        {
            return std::unexpected(input::InputLogError::UnknownDevice);
        }

        event.TimestampNs = timestamp;
        event.CodePoint = static_cast<uint32_t>(codePoint);
        event.Control = *control;
        event.Kind = *kind;
        event.Device = *device;
        event.DeviceIndex = static_cast<uint8_t>(deviceIndex);
        event.Modifiers = static_cast<input::ModifierFlags>(modifiers);
        event.Flags = static_cast<input::EventFlags>(flags);

        return event;
    }

    std::string_view TrimLine(std::string_view line)
    {
        constexpr std::string_view whitespace = " \t\r";
        const size_t first = line.find_first_not_of(whitespace);

        if (first == std::string_view::npos)
        {
            return std::string_view{};
        }

        const size_t last = line.find_last_not_of(whitespace);

        return line.substr(first, last - first + 1);
    }
}
