#pragma once
#ifndef DIAMOND_DOGS_LOOM_FREEZE_RESOLVE_KEY_HPP
#define DIAMOND_DOGS_LOOM_FREEZE_RESOLVE_KEY_HPP
#include "Freeze.hpp"
#include "FrozenGraph.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "LoomKeys.hpp"
#include <cstdint>
#include <source_location>

// The one lookup that every resolve walk ends in, and its typed wrappers (LM103, LM104, LM107).
namespace loom::freeze
{

    inline int32_t ResolveKey(FreezeContext& context, KeyRef key, KeyClass expected_kind, std::source_location where)
    {
        const auto found = context.Keys.find(key.Hash);

        if (found == context.Keys.end())
        {
            DiagnosticCode missingCode =
                (expected_kind == KeyClass::Anchor) ? DiagnosticCode::LM104 : DiagnosticCode::LM103;
            Diagnostic diag = MakeDiagnostic(missingCode, where);
            diag.Resource = key;
            context.Sink.Report(diag);
            return -1; // return an invalid index
        }

        if (found->second.Class != expected_kind)
        {
            Diagnostic diag = MakeDiagnostic(DiagnosticCode::LM107, where);
            diag.OtherWhere = found->second.Where;
            diag.Resource = key;
            context.Sink.Report(diag);
            return -1; // return an invalid index
        }

        return found->second.Index;
    }

    inline ResourceIndex ResolveResource(FreezeContext& context, KeyRef key, KeyClass expected_kind,
                                         std::source_location where)
    {
        return ResourceIndex{ ResolveKey(context, key, expected_kind, where) };
    }

    inline AnchorIndex ResolveAnchor(FreezeContext& context, KeyRef key, std::source_location where)
    {
        return AnchorIndex{ ResolveKey(context, key, KeyClass::Anchor, where) };
    }

}

#endif // !DIAMOND_DOGS_LOOM_FREEZE_RESOLVE_KEY_HPP
