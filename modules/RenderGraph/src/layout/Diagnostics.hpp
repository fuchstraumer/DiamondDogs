#pragma once
#ifndef DIAMOND_DOGS_LOOM_LAYOUT_DIAGNOSTICS_HPP
#define DIAMOND_DOGS_LOOM_LAYOUT_DIAGNOSTICS_HPP
#include "LoomDiagnostic.hpp"
#include "LoomKeys.hpp"
#include <cstdint>
#include <source_location>
#include <string_view>

// Diagnostics.cpp: the values and the report that every Layout check shares.
namespace loom::layout
{
    struct LayoutContext;

    [[nodiscard]] DiagnosticValue TextValue(std::string_view field_name, std::string_view text);
    [[nodiscard]] DiagnosticValue IntegerValue(std::string_view field_name, int64_t value);

    // LM601: Expected is the limit, Actual the value that exceeds it. Both carry the limit's name.
    void ReportLimit(LayoutContext& context, KeyRef pass, KeyRef resource, std::source_location where,
                     std::string_view limit_name, int64_t limit, int64_t value);
}

#endif // !DIAMOND_DOGS_LOOM_LAYOUT_DIAGNOSTICS_HPP
