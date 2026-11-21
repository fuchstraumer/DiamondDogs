#include "layout/LayoutContext.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "LoomKeys.hpp"
#include <cstdint>
#include <source_location>
#include <string_view>

namespace loom::layout
{

    DiagnosticValue TextValue(std::string_view field_name, std::string_view text)
    {
        return DiagnosticValue{ .FieldName = field_name, .Kind = DiagnosticValueKind::Text, .Text = text };
    }

    DiagnosticValue IntegerValue(std::string_view field_name, int64_t value)
    {
        return DiagnosticValue{ .FieldName = field_name, .Kind = DiagnosticValueKind::Integer, .Integer = value };
    }

    void ReportLimit(LayoutContext& context, KeyRef pass, KeyRef resource, std::source_location where,
                     std::string_view limit_name, int64_t limit, int64_t value)
    {
        Diagnostic diag
        {
            .Code = DiagnosticCode::LM601,
            .Severity = DefaultSeverityOf(DiagnosticCode::LM601),
            .Where = where,
            .Pass = pass,
            .Resource = resource,
            .Expected = IntegerValue(limit_name, limit),
            .Actual = IntegerValue(limit_name, value),
        };

        context.Sink.Report(diag);
    }

}
