#pragma once
#ifndef DIAMOND_DOGS_LOOM_FREEZE_FREEZE_HPP
#define DIAMOND_DOGS_LOOM_FREEZE_FREEZE_HPP
#include "FrozenGraph.hpp"
#include "Declarations.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "LoomKeys.hpp"
#include "Records.hpp"
#include <cstdint>
#include <source_location>
#include <string_view>
#include <unordered_map>

// What every Freeze step shares: the key classes, the context, the report helpers, and the flattened pass list.
// Function bodies live in these headers while the stage is being written; they move to .cpp files later.
namespace loom::freeze
{

    enum class KeyClass : uint8_t
    {
        Invalid = 0,
        Buffer = 1, // the three resource classes all index FrozenGraph::Resources. They are separate so
        Image = 2,  // that one name as a buffer and as an image is LM107, not LM102.
        Sampler = 3,
        FrameTarget = 4,
        Anchor = 5,
        Pass = 6,
        Sequence = 7
    };

    constexpr ResourceKind KeyClassToKind(KeyClass key_class) noexcept
    {
        switch (key_class)
        {
        case KeyClass::Buffer:
            return ResourceKind::Buffer;
        case KeyClass::Image:
            return ResourceKind::Image;
        case KeyClass::Sampler:
            return ResourceKind::Sampler;
        default:
            return ResourceKind::Invalid;
        }
    }

    constexpr KeyClass ClassOf(ResourceKind kind) noexcept
    {
        switch (kind)
        {
        case ResourceKind::Buffer:
            return KeyClass::Buffer;
        case ResourceKind::Image:
            return KeyClass::Image;
        case ResourceKind::Sampler:
            return KeyClass::Sampler;
        case ResourceKind::Invalid:
        default:
            return KeyClass::Invalid;
        }
    }

    struct KeyEntry
    {
        KeyClass Class{ KeyClass::Invalid };
        int32_t Index{ -1 };
        std::source_location Where;
    };

    struct FreezeContext
    {
        const GraphRecord& Record;
        DiagnosticSink& Sink;
        FrozenGraph Graph;
        std::unordered_map<uint64_t, KeyEntry> Keys;
    };

    inline Diagnostic MakeDiagnostic(DiagnosticCode code, std::source_location where)
    {
        return Diagnostic
        {
            .Code = code,
            .Severity = DiagnosticSeverity::Error,
            .Where = where,
        };
    }

    inline void ReportMissing(FreezeContext& context, std::string_view field_name, std::source_location where)
    {
        Diagnostic diag = MakeDiagnostic(DiagnosticCode::LM101, where);
        diag.Actual.FieldName = field_name;
        context.Sink.Report(diag);
    }

    inline void ReportOutOfRange(FreezeContext& context,
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

    // A separate name, not an overload: an int32_t argument would convert equally well to int64_t and to double.
    inline void ReportOutOfRangeReal(FreezeContext& context,
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

    // An enum value that is set but not usable here. Reported as its integer, since the sinks cannot name it.
    template<typename Enum>
    inline void ReportOutOfRangeEnum(FreezeContext& context, std::string_view field_name, Enum value,
                              std::source_location where)
    {
        ReportOutOfRange(context, field_name, static_cast<int64_t>(value), where);
    }

    // Where one pass came from in the record. Freeze flattens top-level and nested passes into one list.
    struct PassSource
    {
        const PassRecord* Pass{ nullptr };
        // The sequence's index in GraphRecord::Sequences. It equals its index in FrozenGraph::Sequences
        // unless PublishSequences skipped a duplicate, and that reported an error, so the graph is discarded
        // anyway.
        SequenceIndex Sequence;
        int32_t PositionInSequence{ -1 };
    };

}

#endif // !DIAMOND_DOGS_LOOM_FREEZE_FREEZE_HPP
