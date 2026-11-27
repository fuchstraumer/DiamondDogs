#pragma once
#ifndef DIAMOND_DOGS_LOOM_COMPILE_HPP
#define DIAMOND_DOGS_LOOM_COMPILE_HPP
#include "CompiledGraph.hpp"
#include "DeviceCapability.hpp"
#include "LoomDeviceLimits.hpp"
#include "LoomKeys.hpp"
#include <optional>
#include <span>
#include <string>

namespace rhi
{
    class ShaderBundle;
}

// Compile.cpp: ChoosePaths, then the six Compile stages, chained (spec section 5).
namespace loom
{
    class DiagnosticSink;
    class GraphBuilder;

    // One path that Compile must choose for its list, whatever the device has. For tests, and for a debug menu that
    // compares tiers.
    struct ForcedPath
    {
        PathListKey List;
        PathKey Path;
    };

    // What Compile needs besides the graph: the device-free bundle (G11), and the device's enabled capabilities and
    // limits. Compile copies what it needs, so the bundle can close once Compile returns. Keep it open while the sink
    // holds Compile's diagnostics: binding names and semantics are views into it.
    struct CompileInputs
    {
        const rhi::ShaderBundle& Bundle;
        CapabilitySupportFn Supports{ nullptr };
        void* SupportsUser{ nullptr };
        ShaderDeviceLimits ShaderLimits;
        LayoutLimits Limits;
        // The neutral capabilities the device was created with, sorted: bootstrap's DeviceFilter::Capabilities.
        // Path lists choose with them; a graph with no path list ignores them.
        std::span<const bootstrap::DeviceCapability> Capabilities;
        // Empty: Compile chooses each list's path by the device
        std::span<const ForcedPath> ForcedPaths;
    };

    // Freeze, Resolve, Bind, Size, Order and Layout. Each stage reports every error it finds, and Compile stops after
    // the first stage that reported one. Returns no graph when a stage failed.
    [[nodiscard]] std::optional<CompiledGraph> Compile(const GraphBuilder& builder,
                                                       const CompileInputs& inputs,
                                                       DiagnosticSink& sink);

    // Each path list's chosen path, and why Compile rejected each path before it. Empty for a graph with no path list.
    [[nodiscard]] std::string ExplainPaths(const CompiledGraph& compiled);

}

#endif // !DIAMOND_DOGS_LOOM_COMPILE_HPP
