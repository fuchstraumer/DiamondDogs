#include "content/JsonSink.hpp"
#include "content/Key.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace
{
    constexpr std::string_view k_TypeMember{ "$type" };
    // EncodeErased writes this name for a valueless variant, and no value follows it
    constexpr std::string_view k_ValuelessVariant{ "<valueless>" };

    [[nodiscard]] std::string_view NonFiniteText(double value) noexcept;
}

namespace foundation::content
{
    JsonSink::JsonSink(JsonStyle style) noexcept :
        writer{ style }
    {}

    void JsonSink::BeginStruct(std::string_view type_name)
    {
        writer.BeginObject();

        if (!pendingAlternative.empty())
        {
            writer.KeyString(k_TypeMember, type_name);
            pendingAlternative = {};
        }
    }

    void JsonSink::EndStruct()
    {
        writer.EndObject();
    }

    void JsonSink::BeginField(std::string_view field_name)
    {
        writer.Key(field_name);
    }

    void JsonSink::WriteBool(bool value)
    {
        writer.Bool(value);
    }

    void JsonSink::WriteInt(int64_t value, int32_t width_bytes, bool is_signed)
    {
        // value is already sign-extended or zero-extended to 64 bits, so the width changes nothing here
        static_cast<void>(width_bytes);

        if (is_signed)
        {
            writer.Int(value);
            return;
        }

        writer.UInt(static_cast<uint64_t>(value));
    }

    void JsonSink::WriteFloat32(float value)
    {
        if (!std::isfinite(value))
        {
            writer.String(NonFiniteText(static_cast<double>(value)));
            return;
        }

        writer.Float(value);
    }

    void JsonSink::WriteFloat64(double value)
    {
        if (!std::isfinite(value))
        {
            writer.String(NonFiniteText(value));
            return;
        }

        writer.Double(value);
    }

    void JsonSink::WriteString(std::string_view value)
    {
        writer.String(value);
    }

    void JsonSink::WriteEnum(std::string_view type_name, std::string_view case_name)
    {
        if (pendingAlternative.empty())
        {
            writer.String(case_name);
            return;
        }

        writer.BeginObject();
        writer.KeyString(k_TypeMember, type_name);
        writer.KeyString("value", case_name);
        writer.EndObject();
        pendingAlternative = {};
    }

    void JsonSink::BeginArray(uint64_t count)
    {
        static_cast<void>(count);
        writer.BeginArray();
    }

    void JsonSink::EndArray()
    {
        writer.EndArray();
    }

    void JsonSink::WriteOptional(bool present)
    {
        if (!present)
        {
            writer.Null();
        }
    }

    void JsonSink::WriteVariant(std::string_view alternative_name)
    {
        if (alternative_name == k_ValuelessVariant)
        {
            writer.Null();
            return;
        }

        pendingAlternative = alternative_name;
    }

    void JsonSink::WriteKeyBytes(uint64_t low, uint64_t high)
    {
        std::array<char, 32> hex{};

        WriteHexWords(high, low, std::span<char, 32>{ hex });
        writer.String(std::string_view{ hex.data(), hex.size() });
    }

    JsonResult<std::string> JsonSink::Finish() noexcept
    {
        return writer.Finish();
    }
}

namespace
{
    std::string_view NonFiniteText(double value) noexcept
    {
        if (std::isnan(value))
        {
            return "NaN";
        }

        return value > 0.0 ? "Infinity" : "-Infinity";
    }
}
