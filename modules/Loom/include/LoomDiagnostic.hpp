#pragma once
#ifndef DIAMOND_DOGS_LOOM_DIAGNOSTIC_HPP
#define DIAMOND_DOGS_LOOM_DIAGNOSTIC_HPP
#include "LoomError.hpp"
#include "LoomKeys.hpp"
#include <cstdint>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace loom
{
    // None is a real state here (a field with no value, as in LM101), so 0 is None, not Invalid.
    enum class DiagnosticValueKind : uint8_t
    {
        None = 0,
        Text = 1,
        Integer = 2,
    };

    // One side of a comparison. LM101: FieldName "Lifetime", no value. LM109: FieldName "Layers", Integer 0.
    // LM305: FieldName "WindSpeed", Integer 8 (the offset) against Integer 52. Stages never format a string, so a
    // number stays a number until a sink prints it.
    struct DiagnosticValue
    {
        std::string_view FieldName;
        DiagnosticValueKind Kind;
        std::string_view Text;
        int64_t Integer;
    };

    struct Diagnostic
    {
        DiagnosticCode Code;
        DiagnosticSeverity Severity;
        std::source_location Where;
        std::source_location OtherWhere; // for error codes that associate two locations
        KeyRef Pass;
        KeyRef Resource;
        std::string_view Binding; // manifest binding name, or caller literal
        DiagnosticValue Expected;
        DiagnosticValue Actual;
        std::string_view Suggestion;
    };

    // Stages call Report. Report counts by severity, then hands the record to the sink's own onReport.
    // A Diagnostic holds views into key literals and the bundle, so a sink that keeps records must not outlive them.
    class DiagnosticSink
    {
    public:
        virtual ~DiagnosticSink();

        void Report(const Diagnostic& diagnostic);

        [[nodiscard]] int32_t ErrorCount() const noexcept;      // Error and Fatal
        [[nodiscard]] int32_t WarningCount() const noexcept;
        [[nodiscard]] int32_t NoteCount() const noexcept;

    protected:
        virtual void onReport(const Diagnostic& diagnostic) = 0;

    private:
        int32_t errorCount{ 0 };
        int32_t warningCount{ 0 };
        int32_t noteCount{ 0 };
    };

    // Keeps every record. For tests, and for a caller that inspects the records itself.
    class CollectingSink final : public DiagnosticSink
    {
    public:
        [[nodiscard]] std::span<const Diagnostic> Diagnostics() const noexcept;
        void Clear() noexcept;

    protected:
        void onReport(const Diagnostic& diagnostic) override;

    private:
        std::vector<Diagnostic> diagnostics;
    };

    // One block of text per record. The first line has the compiler's "file(line): error CODE:" shape, so an IDE
    // output window can jump to the call site.
    class TextSink final : public DiagnosticSink
    {
    public:
        [[nodiscard]] std::string_view Text() const noexcept;
        void Clear() noexcept;

    protected:
        void onReport(const Diagnostic& diagnostic) override;

    private:
        std::string text;
    };

    // JSON Lines: one object per record, one record per line. For agents and tools.
    class JsonSink final : public DiagnosticSink
    {
    public:
        [[nodiscard]] std::string_view Text() const noexcept;
        void Clear() noexcept;

    protected:
        void onReport(const Diagnostic& diagnostic) override;

    private:
        std::string text;
    };

    // The text that TextSink and JsonSink append for one record.
    [[nodiscard]] std::string FormatText(const Diagnostic& diagnostic);
    [[nodiscard]] std::string FormatJson(const Diagnostic& diagnostic);

}

#endif // !DIAMOND_DOGS_LOOM_DIAGNOSTIC_HPP
