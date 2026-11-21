#include "freeze/Reports.hpp"
#include "freeze/FreezeContext.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include <cstdint>
#include <source_location>
#include <string_view>

namespace loom::freeze
{

    Diagnostic MakeDiagnostic(DiagnosticCode code, std::source_location where)
    {
        return Diagnostic
        {
            .Code = code,
            .Severity = DiagnosticSeverity::Error,
            .Where = where,
        };
    }

    void ReportMissing(FreezeContext& context, std::string_view field_name, std::source_location where)
    {
        Diagnostic diag = MakeDiagnostic(DiagnosticCode::LM101, where);
        diag.Actual.FieldName = field_name;
        context.Sink.Report(diag);
    }

    void ReportOutOfRange(FreezeContext& context,
                   std::string_view field_name,
                   int64_t value,
                   std::source_location where)
    {
        Diagnostic diag = MakeDiagnostic(DiagnosticCode::LM109, where);
        diag.Actual = DiagnosticValue
        {
            .FieldName = field_name,
            .Kind = DiagnosticValueKind::Integer,
            .Text = {},
            .Integer = value
        };
        context.Sink.Report(diag);
    }

    void ReportOutOfRangeReal(FreezeContext& context,
                       std::string_view field_name,
                       double value,
                       std::source_location where)
    {
        Diagnostic diag = MakeDiagnostic(DiagnosticCode::LM109, where);
        diag.Actual = DiagnosticValue
        {
            .FieldName = field_name,
            .Kind = DiagnosticValueKind::Real,
            .Text = {},
            .Integer = 0,
            .Real = value
        };
        context.Sink.Report(diag);
    }

}
