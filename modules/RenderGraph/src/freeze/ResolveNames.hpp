#pragma once
#ifndef DIAMOND_DOGS_LOOM_FREEZE_RESOLVE_NAMES_HPP
#define DIAMOND_DOGS_LOOM_FREEZE_RESOLVE_NAMES_HPP
#include "freeze/FreezeContext.hpp"
#include <cstdint>
#include <source_location>
#include <string_view>
#include <vector>

// Step 3: replace every name a record uses with an index into the frozen tables (LM103, LM104, LM107).
namespace loom::freeze
{

    std::vector<FrozenAnchorUse> ResolveAnchorUses(FreezeContext& context, const std::vector<AnchorUseRecord>& uses);

    // The bind's own Kind says which table its key must name, so a buffer key bound as an image is LM107.
    std::vector<FrozenBind> ResolveBinds(FreezeContext& context, const std::vector<BindRecord>& binds);

    FrozenDispatch ResolveDispatch(FreezeContext& context, const DispatchRecord& record);

    // What dispatch and draw passes share: exactly one shader (LM105), the binds and the params.
    void ResolveShaderStatements(FreezeContext& context, const PassRecord& pass, FrozenPass& frozen);

    // Exactly one dispatch: none is LM110, two is LM106 with both lines.
    void ResolveDispatchStatement(FreezeContext& context, const PassRecord& pass, FrozenPass& frozen);

    FrozenPass ResolvePass(FreezeContext& context, const PassSource& source);

    void ResolvePasses(FreezeContext& context, const std::vector<PassSource>& sources);

    void ResolveSequenceRanges(FreezeContext& context);

    void ResolveSequences(FreezeContext& context);

    void ResolveImageTargets(FreezeContext& context);

    void ResolveOutputs(FreezeContext& context);

    void ResolveAnchorOrders(FreezeContext& context);

}

#endif // !DIAMOND_DOGS_LOOM_FREEZE_RESOLVE_NAMES_HPP
