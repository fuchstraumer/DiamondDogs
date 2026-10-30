#include "FrozenGraph.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "LoomKeys.hpp"
#include "Records.hpp"
#include <cstdint>
#include <source_location>
#include <string_view>
#include <unordered_map>

namespace loom
{

    namespace
    {

        enum class KeyClass : uint8_t
        {
            Invalid = 0,
            Resource = 1,
            FrameTarget = 2,
            Anchor = 3,
            Pass = 4,
            Sequence = 5
        };

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

        void ReportOutOfRange(FreezeContext& context, std::string_view field_name, int64_t value,
                              std::source_location where)
        {
            Diagnostic diag = MakeDiagnostic(DiagnosticCode::LM109, where);
            diag.Actual = DiagnosticValue{ .FieldName = field_name,
                                           .Kind = DiagnosticValueKind::Integer,
                                           .Text = {},
                                           .Integer = value };
            context.Sink.Report(diag);
        }

        bool Publish(FreezeContext& context, KeyRef key, KeyClass key_class, int32_t index,
                     std::source_location where)
        {
            // initial publish, checking just the key first
            const auto found = context.Keys.find(key.Hash);
            if (found == context.Keys.end())
            {
                context.Keys.emplace(key.Hash, KeyEntry{ .Class = key_class, .Index = index, .Where = where });
                return true;
            }

            const bool duplicateNameOfClass = found->second.Class == key_class;
            // either duplicate name of the same class, or collision of having name assigned twice to different types
            DiagnosticCode errorCode = duplicateNameOfClass ? DiagnosticCode::LM102 : DiagnosticCode::LM107;
            Diagnostic diag = MakeDiagnostic(errorCode, where);
            diag.Resource = key;
            context.Sink.Report(diag);
            return false;
        }

        void PublishBuffers(FreezeContext& context)
        {
            
        }


    }

}
