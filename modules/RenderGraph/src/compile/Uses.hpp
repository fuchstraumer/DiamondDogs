#pragma once
#ifndef DIAMOND_DOGS_LOOM_COMPILE_USES_HPP
#define DIAMOND_DOGS_LOOM_COMPILE_USES_HPP

// Uses.cpp: each pass's merged use of each resource row, with its sync scope; the params keys; the history reads;
// and each row's first and last level.
namespace loom
{
    struct CompiledData;

    namespace compile
    {
        // The frame-target rows (RowCount) and each target's fallback. Runs before GatherCopies and GatherUses.
        void GatherFrameTargets(CompiledData& data);
        void GatherUses(CompiledData& data);
        void GatherParams(CompiledData& data);
        void ComputeSpans(CompiledData& data);
        // History rings: the copies of each row (CompiledData::CopyCounts, ExtraCopyBase, CopyRows)
        void GatherCopies(CompiledData& data);
    }
}

#endif // !DIAMOND_DOGS_LOOM_COMPILE_USES_HPP
