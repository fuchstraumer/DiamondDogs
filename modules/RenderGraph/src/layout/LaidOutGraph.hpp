#pragma once
#ifndef DIAMOND_DOGS_LOOM_LAID_OUT_GRAPH_HPP
#define DIAMOND_DOGS_LOOM_LAID_OUT_GRAPH_HPP
#include "containers/SmallVector.hpp"
#include "LoomFormats.hpp"
#include "PassStatements.hpp"
#include "ShaderLibraryTypes.hpp"
#include "freeze/FrozenGraph.hpp"
#include <cstdint>
#include <string>
#include <vector>

// The Layout stage's output, the last Compile stage. Layout gives Realize the facts that it needs to create pipelines
// and record dispatches: the group count of each dispatch, and one pipeline description per distinct pipeline. It
// checks the attachments against the fragment shader (LM602), the vertex inputs (LM603), the images that a pass both
// reads and writes as a target (LM604), and the device limits that Resolve does not check (LM601).
namespace loom
{

    enum class GroupCountSource : uint8_t
    {
        Invalid = 0,
        Known = 1,          // Groups holds the count
        AtRealize = 2,      // DispatchThreadsOver an image whose extent Realize knows: divide it by Workgroup
        Indirect = 3        // the argument buffer holds the count
    };

    struct GroupCount
    {
        int32_t X{ 0 };
        int32_t Y{ 0 };
        int32_t Z{ 0 };
    };

    struct LaidOutDispatch
    {
        GroupCountSource Source{ GroupCountSource::Invalid };
        GroupCount Groups;
        lodestone::WorkgroupSize Workgroup;
    };

    // One color attachment of a pipeline. A frame target's format is k_NoTextureFormat: Realize knows it.
    struct LaidOutColor
    {
        int32_t Slot{ -1 };
        lodestone::TextureFormat Format{ k_NoTextureFormat };
        FrameTargetIndex FrameTarget;
        BlendMode Blend{ BlendMode::Invalid };

        bool operator==(const LaidOutColor&) const noexcept = default;
    };

    // What one pipeline is, by name (G9): never by VariantKey, which is valid inside one bundle only. A dispatch has
    // no fragment entry point and no attachments. Two passes with equal descriptions share one pipeline. This is a
    // value compared with ==; the cache that lasts across recompiles belongs to Realize.
    struct PipelineDescription
    {
        std::string Module;
        std::string Variant;                // the variant's suffix: its axis values as names
        std::string EntryPoint;
        std::string FragmentEntryPoint;
        foundation::SmallVector<LaidOutColor, 4> Colors; // by slot. 4 is the typical maximum number of color attachments in a pipeline
        lodestone::TextureFormat DepthFormat{ k_NoTextureFormat };
        CompareOp DepthCompare{ CompareOp::Invalid };
        DepthWrite DepthWrites{ DepthWrite::Invalid };
        RasterState Raster;

        bool operator==(const PipelineDescription&) const noexcept = default;
    };

    inline constexpr int32_t k_NoPipeline = -1;

    // One row for each FrozenGraph pass, at the same index. A Record pass has no pipeline and no dispatch.
    struct LaidOutPass
    {
        LaidOutDispatch Dispatch;
        int32_t Pipeline{ k_NoPipeline };   // an index into LaidOutGraph::Pipelines
    };

    // The stage that fills it, Layout(), is in Layout.hpp.
    struct LaidOutGraph
    {
        std::vector<LaidOutPass> Passes;
        std::vector<PipelineDescription> Pipelines;
    };

}

#endif // !DIAMOND_DOGS_LOOM_LAID_OUT_GRAPH_HPP
