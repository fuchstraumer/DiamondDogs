#pragma once
#ifndef DIAMOND_DOGS_LOOM_DIAGNOSTIC_HPP
#define DIAMOND_DOGS_LOOM_DIAGNOSTIC_HPP
#include "LoomError.hpp"
#include "LoomKeys.hpp"
#include <source_location>
#include <string_view>

namespace loom
{
    struct DiagnosticValue
    {
        std::string_view FieldName;
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

    

}

#endif // !DIAMOND_DOGS_LOOM_DIAGNOSTIC_HPP
