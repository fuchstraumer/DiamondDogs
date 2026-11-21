#pragma once
#ifndef DIAMOND_DOGS_LOOM_LAID_OUT_GRAPH_HPP
#define DIAMOND_DOGS_LOOM_LAID_OUT_GRAPH_HPP
#include "LoomFormats.hpp"
#include "PassStatements.hpp"
#include "RhiFlags.hpp"
#include "ShaderLibraryTypes.hpp"
#include "freeze/FrozenGraph.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The Layout stage's output, the last Compile stage. Layout gives Realize the facts that it needs to create pipelines
// and record dispatches: the group count of each dispatch, and one pipeline description per distinct pipeline. It
// checks the attachments against the fragment shader (LM602), the vertex inputs (LM603), the images that a pass both
// reads and writes as a target (LM604), and the device limits that Resolve does not check (LM601).
namespace loom
{
    class DiagnosticSink;
    struct BoundGraph;
    struct ResolvedGraph;
    struct SizedGraph;

    // The device limits that Layout checks, in VkPhysicalDeviceLimits terms. Resolve checks the workgroup size, the
    // invocations, the workgroup memory and the push constant bytes (LM207), so they are not here. Fill every field:
    // a limit of 0 rejects each pass that uses that kind of thing.
    struct LayoutLimits
    {
        uint32_t MaxComputeWorkGroupCountX{ 0u };
        uint32_t MaxComputeWorkGroupCountY{ 0u };
        uint32_t MaxComputeWorkGroupCountZ{ 0u };
        uint32_t MaxColorAttachments{ 0u };
        uint32_t MaxBoundDescriptorSets{ 0u };
        uint32_t MaxPerStageDescriptorSamplers{ 0u };
        uint32_t MaxPerStageDescriptorUniformBuffers{ 0u };
        uint32_t MaxPerStageDescriptorStorageBuffers{ 0u };
        uint32_t MaxPerStageDescriptorSampledImages{ 0u };
        uint32_t MaxPerStageDescriptorStorageImages{ 0u };
        uint32_t MaxPerStageResources{ 0u };
        uint32_t MaxUniformBufferRange{ 0u };
        uint32_t MaxStorageBufferRange{ 0u };
        uint32_t MaxImageDimension1D{ 0u };
        uint32_t MaxImageDimension2D{ 0u };
        uint32_t MaxImageDimension3D{ 0u };
    };

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
        std::vector<LaidOutColor> Colors;   // by slot
        lodestone::TextureFormat DepthFormat{ k_NoTextureFormat };
        rhi::CompareOp DepthCompare{ rhi::CompareOp::Never };
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

    struct LaidOutGraph
    {
        std::vector<LaidOutPass> Passes;
        std::vector<PipelineDescription> Pipelines;
    };

    // Reports every Layout error it finds, not only the first one. Returns no graph when it reported an error. Keep
    // the bundle open while the sink holds diagnostics: a semantic or a binding name is a view into it.
    [[nodiscard]] std::optional<LaidOutGraph> Layout(const FrozenGraph& frozen,
                                                     const ResolvedGraph& resolved,
                                                     const BoundGraph& bound,
                                                     const SizedGraph& sized,
                                                     const LayoutLimits& limits,
                                                     DiagnosticSink& sink);

}

#endif // !DIAMOND_DOGS_LOOM_LAID_OUT_GRAPH_HPP
