#pragma once
#ifndef DIAMOND_DOGS_LOOM_FREEZE_REPORTS_HPP
#define DIAMOND_DOGS_LOOM_FREEZE_REPORTS_HPP
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include <cstdint>
#include <source_location>
#include <string_view>

// Reports.cpp: the diagnostics that every Freeze check reports (LM101, LM109).
namespace loom::freeze
{
    struct FreezeContext;

    Diagnostic MakeDiagnostic(DiagnosticCode code, std::source_location where);
    void ReportMissing(FreezeContext& context, std::string_view field_name, std::source_location where);
    void ReportOutOfRange(FreezeContext& context,
                   std::string_view field_name,
                   int64_t value,
                   std::source_location where);
    // A separate name, not an overload: an int32_t argument would convert equally well to int64_t and to double.
    void ReportOutOfRangeReal(FreezeContext& context,
                       std::string_view field_name,
                       double value,
                       std::source_location where);

    // An enum value that is set but not usable here. Reported as its integer, since the sinks cannot name it.
    template<typename Enum>
    inline void ReportOutOfRangeEnum(FreezeContext& context, std::string_view field_name, Enum value,
                              std::source_location where)
    {
        ReportOutOfRange(context, field_name, static_cast<int64_t>(value), where);
    }

}

#endif // !DIAMOND_DOGS_LOOM_FREEZE_REPORTS_HPP
