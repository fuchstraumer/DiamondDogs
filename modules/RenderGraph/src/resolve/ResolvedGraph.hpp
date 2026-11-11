#pragma once
#ifndef DIAMOND_DOGS_LOOM_RESOLVED_GRAPH_HPP
#define DIAMOND_DOGS_LOOM_RESOLVED_GRAPH_HPP
#include "ShaderManifestDevice.hpp"
#include "VariantKey.hpp"
#include <cstdint>
#include <optional>
#include <vector>

namespace lodestone::manifest
{
    class EnvironmentView;
}

namespace rhi
{
    class ShaderBundle;
}

// The Resolve stage's input and output. Resolve reads the shader bundle once, with no device, and selects one variant
// and its entry points for each pass that has a shader (LM201 to LM207).
namespace loom
{
    class DiagnosticSink;
    struct FrozenGraph;

    // Supports must answer from the capabilities that the device has enabled, not from the capabilities that the
    // physical device supports. A variant that needs a capability the device left off fails validation, and no
    // named error says why. Fill every field of Limits: a limit of 0 rejects each instance that has a size to compare.
    struct ResolveInputs
    {
        const rhi::ShaderBundle& Bundle;
        lodestone::manifest::CapabilitySupportFn Supports{ nullptr };
        void* SupportsUser{ nullptr };
        lodestone::manifest::DeviceLimits Limits;
    };

    inline constexpr int32_t k_NoEntryPoint = -1;

    // One row for each FrozenGraph pass, at the same index. A Record pass has no shader: Environment is null and both
    // entry points are k_NoEntryPoint. Variant is valid only for the bundle file it came from, so do not store it.
    struct ResolvedPass
    {
        const lodestone::manifest::EnvironmentView* Environment{ nullptr };
        lodestone::VariantKey Variant{ lodestone::INVALID_VARIANT };
        int32_t EntryPoint{ k_NoEntryPoint };           // the compute entry point, or the vertex entry point of a draw
        int32_t FragmentEntryPoint{ k_NoEntryPoint };   // none for a dispatch, or for a depth-only draw
    };

    struct ResolvedGraph
    {
        std::vector<ResolvedPass> Passes;
    };

    // Reports every Resolve error it finds, not only the first one. Returns no graph when it reported an error. The
    // warnings (LM206) run only on a graph with no errors.
    [[nodiscard]] std::optional<ResolvedGraph> Resolve(const FrozenGraph& frozen,
                                                       const ResolveInputs& inputs,
                                                       DiagnosticSink& sink);

}

#endif // !DIAMOND_DOGS_LOOM_RESOLVED_GRAPH_HPP
