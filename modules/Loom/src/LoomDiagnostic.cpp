#include "LoomDiagnostic.hpp"
#include "JsonWriter.hpp"
#include "LoomError.hpp"
#include "LoomKeys.hpp"
#include <cstdint>
#include <format>
#include <iterator>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace loom
{

    namespace
    {

        // Lower case, because an IDE output window matches "error" and "warning" in that form.
        std::string_view SeverityWord(DiagnosticSeverity severity) noexcept
        {
            switch (severity)
            {
            case DiagnosticSeverity::Invalid: return "invalid";
            case DiagnosticSeverity::Note:    return "note";
            case DiagnosticSeverity::Warning: return "warning";
            case DiagnosticSeverity::Error:   return "error";
            case DiagnosticSeverity::Fatal:   return "fatal error";
            }

            return "invalid";
        }

        // A value-initialized source_location has line 0, which means "not set".
        bool HasLocation(const std::source_location& where) noexcept
        {
            return where.line() != 0u;
        }

        bool HasValue(const DiagnosticValue& value) noexcept
        {
            return !value.FieldName.empty() || value.Kind != DiagnosticValueKind::None;
        }

        void AppendTextLine(std::string& out, std::string_view label, std::string_view value)
        {
            if (value.empty())
            {
                return;
            }

            std::format_to(std::back_inserter(out), "    {}: {}\n", label, value);
        }

        // "Layers = 0", "Lifetime", or "= 0" when only the value is known.
        void AppendTextValue(std::string& out, std::string_view label, const DiagnosticValue& value)
        {
            if (!HasValue(value))
            {
                return;
            }

            std::format_to(std::back_inserter(out), "    {}: {}", label, value.FieldName);
            std::string_view separator = value.FieldName.empty() ? "= " : " = ";

            switch (value.Kind)
            {
            case DiagnosticValueKind::None:    break;
            case DiagnosticValueKind::Text:    std::format_to(std::back_inserter(out), "{}{}", separator, value.Text); break;
            case DiagnosticValueKind::Integer: std::format_to(std::back_inserter(out), "{}{}", separator, value.Integer); break;
            }

            out.push_back('\n');
        }

        void WriteJsonLocation(detail::JsonWriter& writer, std::string_view name, const std::source_location& where)
        {
            writer.Key(name);
            writer.BeginObject();
            writer.KeyString("file", where.file_name());
            writer.KeyUInt("line", where.line());
            writer.KeyUInt("column", where.column());
            writer.EndObject();
        }

        // The hash is a hex string: a 64-bit integer does not survive a reader that parses numbers as doubles.
        void WriteJsonKey(detail::JsonWriter& writer, std::string_view name, const KeyRef& key)
        {
            writer.Key(name);
            writer.BeginObject();
            writer.KeyString("name", key.Name);
            writer.KeyString("hash", std::format("{:016x}", key.Hash));
            writer.EndObject();
        }

        void WriteJsonValue(detail::JsonWriter& writer, std::string_view name, const DiagnosticValue& value)
        {
            writer.Key(name);
            writer.BeginObject();

            if (!value.FieldName.empty())
            {
                writer.KeyString("field", value.FieldName);
            }

            switch (value.Kind)
            {
            case DiagnosticValueKind::None:    break;
            case DiagnosticValueKind::Text:    writer.KeyString("text", value.Text); break;
            case DiagnosticValueKind::Integer: writer.KeyInt("integer", value.Integer); break;
            }

            writer.EndObject();
        }

    }

    DiagnosticSink::~DiagnosticSink() = default;

    void DiagnosticSink::Report(const Diagnostic& diagnostic)
    {
        switch (diagnostic.Severity)
        {
        case DiagnosticSeverity::Error:
        case DiagnosticSeverity::Fatal:   ++errorCount; break;
        case DiagnosticSeverity::Warning: ++warningCount; break;
        case DiagnosticSeverity::Note:    ++noteCount; break;
        // A record with no severity is a Loom fault. Count it as an error, so that it cannot pass in silence.
        case DiagnosticSeverity::Invalid: ++errorCount; break;
        }

        onReport(diagnostic);
    }

    int32_t DiagnosticSink::ErrorCount() const noexcept
    {
        return errorCount;
    }

    int32_t DiagnosticSink::WarningCount() const noexcept
    {
        return warningCount;
    }

    int32_t DiagnosticSink::NoteCount() const noexcept
    {
        return noteCount;
    }

    std::span<const Diagnostic> CollectingSink::Diagnostics() const noexcept
    {
        return diagnostics;
    }

    void CollectingSink::Clear() noexcept
    {
        diagnostics.clear();
    }

    void CollectingSink::onReport(const Diagnostic& diagnostic)
    {
        diagnostics.push_back(diagnostic);
    }

    std::string_view TextSink::Text() const noexcept
    {
        return text;
    }

    void TextSink::Clear() noexcept
    {
        text.clear();
    }

    void TextSink::onReport(const Diagnostic& diagnostic)
    {
        text += FormatText(diagnostic);
    }

    std::string_view JsonSink::Text() const noexcept
    {
        return text;
    }

    void JsonSink::Clear() noexcept
    {
        text.clear();
    }

    void JsonSink::onReport(const Diagnostic& diagnostic)
    {
        text += FormatJson(diagnostic);
    }

    std::string FormatText(const Diagnostic& diagnostic)
    {
        std::string out;

        if (HasLocation(diagnostic.Where))
        {
            std::format_to(std::back_inserter(out), "{}({}): ", diagnostic.Where.file_name(), diagnostic.Where.line());
        }

        std::format_to(std::back_inserter(out), "{} {} [{}]: {}\n", SeverityWord(diagnostic.Severity),
            ToString(diagnostic.Code), ToString(StageOf(diagnostic.Code)), SummaryOf(diagnostic.Code));

        AppendTextLine(out, "pass", diagnostic.Pass.Name);
        AppendTextLine(out, "key", diagnostic.Resource.Name);
        AppendTextLine(out, "binding", diagnostic.Binding);
        AppendTextValue(out, "expected", diagnostic.Expected);
        AppendTextValue(out, "actual", diagnostic.Actual);

        if (HasLocation(diagnostic.OtherWhere))
        {
            std::format_to(std::back_inserter(out), "    see also: {}({})\n", diagnostic.OtherWhere.file_name(),
                diagnostic.OtherWhere.line());
        }

        if (!diagnostic.Suggestion.empty())
        {
            std::format_to(std::back_inserter(out), "    did you mean: {}?\n", diagnostic.Suggestion);
        }

        return out;
    }

    std::string FormatJson(const Diagnostic& diagnostic)
    {
        detail::JsonWriter writer{ detail::JsonStyle::Compact };
        writer.BeginObject();
        writer.KeyString("code", ToString(diagnostic.Code));
        writer.KeyString("stage", ToString(StageOf(diagnostic.Code)));
        writer.KeyString("severity", ToString(diagnostic.Severity));
        writer.KeyString("summary", SummaryOf(diagnostic.Code));

        // Each optional member is left out when it is not set, so a reader tests for presence, not for "".
        if (HasLocation(diagnostic.Where))
        {
            WriteJsonLocation(writer, "where", diagnostic.Where);
        }

        if (HasLocation(diagnostic.OtherWhere))
        {
            WriteJsonLocation(writer, "otherWhere", diagnostic.OtherWhere);
        }

        if (!diagnostic.Pass.Name.empty())
        {
            WriteJsonKey(writer, "pass", diagnostic.Pass);
        }

        if (!diagnostic.Resource.Name.empty())
        {
            WriteJsonKey(writer, "key", diagnostic.Resource);
        }

        if (!diagnostic.Binding.empty())
        {
            writer.KeyString("binding", diagnostic.Binding);
        }

        if (HasValue(diagnostic.Expected))
        {
            WriteJsonValue(writer, "expected", diagnostic.Expected);
        }

        if (HasValue(diagnostic.Actual))
        {
            WriteJsonValue(writer, "actual", diagnostic.Actual);
        }

        if (!diagnostic.Suggestion.empty())
        {
            writer.KeyString("suggestion", diagnostic.Suggestion);
        }

        writer.EndObject();

        // Every container above is closed, so Finish cannot fail. An empty line marks it if a later edit breaks that.
        detail::JsonResult<std::string> document = writer.Finish();
        std::string out = document.has_value() ? std::move(*document) : std::string{};
        out.push_back('\n');
        return out;
    }

}
