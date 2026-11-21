#pragma once
#ifndef DIAMOND_DOGS_LOOM_FREEZE_RESOLVE_DRAW_HPP
#define DIAMOND_DOGS_LOOM_FREEZE_RESOLVE_DRAW_HPP
#include "freeze/FreezeContext.hpp"
#include <cstdint>
#include <source_location>
#include <string_view>
#include <vector>

// Step 3, for the statements of draw and Record passes. Each function resolves the names in one kind of statement and
// checks the values that need no lookup (LM101, LM109), plus the at-most-once rule (LM113). ResolveNames.cpp calls these
// from ResolvePass.
namespace loom::freeze
{

    // A statement that may appear at most once appeared again. Where is the second one, OtherWhere the first.
    void ReportTwice(FreezeContext& context, std::source_location second, std::source_location first);

    FrozenDraw ResolveDraw(FreezeContext& context, const DrawRecord& draw);

    FrozenColorTarget ResolveColorTarget(FreezeContext& context, const ColorTargetRecord& color);

    // Two targets in one slot is LM113. The lists are a handful of entries, so a scan of the earlier ones is enough.
    std::vector<FrozenColorTarget> ResolveColorTargets(FreezeContext& context,
                                                       const std::vector<ColorTargetRecord>& colors);

    FrozenDepthTarget ResolveDepthTarget(FreezeContext& context, const DepthTargetRecord& depth);

    std::vector<FrozenVertexBuffer> ResolveVertexBuffers(FreezeContext& context,
                                                         const std::vector<VertexBufferRecord>& buffers);

    FrozenIndexBuffer ResolveIndexBuffer(FreezeContext& context, const IndexBufferRecord& index);

    // Reports every repeat after the first, then returns the first, or nothing when the list is empty.
    template<typename Statement>
    inline const Statement* AtMostOnce(FreezeContext& context, const std::vector<Statement>& statements)
    {
        if (statements.empty())
        {
            return nullptr;
        }

        for (std::size_t index = 1; index < statements.size(); ++index)
        {
            ReportTwice(context, statements[index].Where, statements.front().Where);
        }

        return &statements.front();
    }

    // Everything a draw pass states besides its shader, binds and params.
    void ResolveDrawStatements(FreezeContext& context, const PassRecord& pass, FrozenPass& frozen);

    FrozenUse ResolveUse(FreezeContext& context, const UseRecord& use);

    // A Record pass has exactly one recorder (LM105) and no other statements besides its anchors and uses.
    void ResolveRecordStatements(FreezeContext& context, const PassRecord& pass, FrozenPass& frozen);

}

#endif // !DIAMOND_DOGS_LOOM_FREEZE_RESOLVE_DRAW_HPP
