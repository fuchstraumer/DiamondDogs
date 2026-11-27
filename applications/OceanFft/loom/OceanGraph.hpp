#pragma once
#ifndef DIAMOND_DOGS_OCEAN_LOOM_GRAPH_HPP
#define DIAMOND_DOGS_OCEAN_LOOM_GRAPH_HPP
#include "LoomKeys.hpp"
#include "ResourceTypes.hpp"

namespace loom
{
    class GraphBuilder;
    class Graph;
}

class ResourceContext;

// The OceanFft frame as one Loom graph. Each Register function adds one stage of the frame. The shell fills in only
// RegisterUi; the other stages are empty, and the port fills them in.
namespace ocean_loom
{
    struct SimulationImports
    {
        GraphicsResource CascadeParams;
        GraphicsResource SpectrumParams;
        GraphicsResource NormalizationLut;
    };

    [[nodiscard]] SimulationImports SupplySimulation(loom::Graph& graph, ResourceContext& resource_context);
    void SetSimulationParams(loom::Graph& graph, float time_seconds, float choppiness);

    inline constexpr loom::FrameTargetKey k_Screen{ "Ocean.Screen" };

    // Declares the screen target and its Output. Without a writer that runs, the screen is cleared to this color.
    void RegisterScreen(loom::GraphBuilder& graph);

    // The spectrum, the per-frame tick, the IFFT cascades, the resolve and the mips (OceanSimulation)
    void RegisterSimulation(loom::GraphBuilder& graph);

    // The persistent foam clipmap and its step (OceanFoam). The replay is N Ticks with the draw passes disabled.
    void RegisterFoam(loom::GraphBuilder& graph);

    // The atmosphere precompute and the environment (WhenInputsChange)
    void RegisterEnvironment(loom::GraphBuilder& graph);

    // The CDLOD cull, the depth prepass, the surface and seabed GBuffer, and the Hi-Z
    void RegisterSurface(loom::GraphBuilder& graph);

    // The deferred light, the TAA resolve (history images), and auto exposure
    void RegisterLighting(loom::GraphBuilder& graph);

    // The ColorPipe composite into k_Screen
    void RegisterComposite(loom::GraphBuilder& graph);
}

#endif // !DIAMOND_DOGS_OCEAN_LOOM_GRAPH_HPP
