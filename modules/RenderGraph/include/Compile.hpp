#pragma once
#ifndef DIAMOND_DOGS_LOOM_COMPILE_HPP
#define DIAMOND_DOGS_LOOM_COMPILE_HPP
#include "CompiledGraph.hpp"
#include "LoomDeviceLimits.hpp"
#include <optional>

namespace rhi
{
    class ShaderBundle;
}

// Compile.cpp: the six Compile stages, chained (spec section 5).
namespace loom
{
    class DiagnosticSink;
    class GraphBuilder;

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
    };

    // Freeze, Resolve, Bind, Size, Order and Layout. Each stage reports every error it finds, and Compile stops after
    // the first stage that reported one. Returns no graph when a stage failed.
    [[nodiscard]] std::optional<CompiledGraph> Compile(const GraphBuilder& builder,
                                                       const CompileInputs& inputs,
                                                       DiagnosticSink& sink);

}

#endif // !DIAMOND_DOGS_LOOM_COMPILE_HPP
