#pragma once
#ifndef DIAMOND_DOGS_LOOM_FREEZE_PUBLISH_HPP
#define DIAMOND_DOGS_LOOM_FREEZE_PUBLISH_HPP
#include "freeze/FreezeContext.hpp"
#include <cstdint>
#include <source_location>
#include <string_view>
#include <vector>

// Step 1: enter every name into the key table (LM102, LM107), and create the rows that later steps fill in.
namespace loom::freeze
{

    bool Publish(FreezeContext& context, KeyRef key, KeyClass key_class, int32_t index, std::source_location where);

    void PublishBuiltinAnchor(FreezeContext& context, AnchorKey anchor);

    // Runs first, on an empty graph, so the two anchors land at k_BeginFrameAnchorIndex and
    // k_EndFrameAnchorIndex.
    void PublishBuiltinAnchors(FreezeContext& context);

    void PublishBuffers(FreezeContext& context);

    void PublishImages(FreezeContext& context);

    void PublishSamplers(FreezeContext& context);

    void PublishImports(FreezeContext& context);

    void PublishAnchors(FreezeContext& context);

    void PublishFrameTargets(FreezeContext& context);

    // An exposed anchor belongs to its sequence, so it is published from the sequence walk, not with the
    // others.
    void PublishExposedAnchors(FreezeContext& context,
                        const SequenceRecord& sequence_record,
                        SequenceIndex sequence_index,
                        FrozenSequence& sequence);

    // FirstPass, PassCount and the anchor uses are filled in after the passes are flattened.
    void PublishSequences(FreezeContext& context);

    constexpr bool EarlierDeclared(const PassSource& left, const PassSource& right) noexcept
    {
        return left.Pass->DeclarationOrder < right.Pass->DeclarationOrder;
    }

    std::vector<PassSource> CollectPasses(const GraphRecord& record);

    // A pass's index is its place in the sorted list, which is its DeclarationOrder. The FrozenPass rows are
    // written later, one per source in this same order, so the indices published here stay true.
    void PublishPasses(FreezeContext& context, const std::vector<PassSource>& sources);

}

#endif // !DIAMOND_DOGS_LOOM_FREEZE_PUBLISH_HPP
