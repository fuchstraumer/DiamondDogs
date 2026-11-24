#pragma once
#ifndef DIAMOND_DOGS_LOOM_DRAW_GRAPH_TEST_HPP
#define DIAMOND_DOGS_LOOM_DRAW_GRAPH_TEST_HPP
#include "FrameTarget.hpp"
#include "LoomKeys.hpp"
#include "LoomGraph.hpp"
#include "DeviceRequirements.hpp"
#include <array>
#include <memory>
#include <span>

class ResourceContext;

namespace bootstrap
{
    struct DeviceFilter;
}

namespace rhi
{
    class ShaderBundle;
    class Device;
}

namespace loom
{
    class CollectingSink;
}

// The draw graph of LoomDrawTest (headless) and LoomTriangleTest (a swapchain). Four passes into the frame target
// k_Screen: Cull (compute) writes one indirect draw and a count; Prepass draws depth through DrawIndirectCount; GBuffer
// draws two colors with depth Equal; Light draws a fullscreen triangle from both and LoomFrame. Prepass and GBuffer cull
// back faces, and the triangle is counterclockwise in Y-up NDC.
namespace loom_draw
{
    inline constexpr loom::ResourceKey<loom::Buffer> k_Positions{ "LoomDraw.Positions" };
    inline constexpr loom::FrameTargetKey k_Screen{ "LoomDraw.Screen" };
    inline constexpr loom::PassKey k_Light{ "LoomDraw.Light" };
    inline constexpr std::array<float, 4u> k_FallbackColor{ 0.1f, 0.2f, 0.3f, 1.0f };

    // The passes in declaration order, which is their pass index
    enum PassIndex : int32_t
    {
        Cull = 0,
        Prepass = 1,
        GBuffer = 2,
        Light = 3,
        PassCount = 4
    };

    // Clip-space xyz and w = 1
    inline constexpr std::array<std::array<float, 4u>, 3u> k_Triangle
    {
        std::array<float, 4u>{ -0.8f, -0.8f, 0.5f, 1.0f },
        std::array<float, 4u>{ 0.8f, -0.8f, 0.5f, 1.0f },
        std::array<float, 4u>{ 0.0f, 0.8f, 0.5f, 1.0f },
    };

    // Shader objects give the dynamic state; DrawIndirectCount is a Vulkan 1.2 feature
    [[nodiscard]] std::span<const bootstrap::Need> DeviceNeeds() noexcept;

    struct RealizeArguments
    {
        const rhi::ShaderBundle& Bundle;
        const bootstrap::DeviceFilter& Filter;
        ResourceContext& Resources;
        const rhi::Device& Device;
        int32_t FrameSlots;
        loom::CollectingSink& Sink;
        std::span<const loom::FrameTargetBinding> Targets;
        double TargetSettleSeconds;
    };

    // Builds, compiles and realizes the graph. Prints the failure to stderr and returns null when a stage fails.
    [[nodiscard]] std::unique_ptr<loom::Graph> BuildAndRealize(const RealizeArguments& arguments);

    // The six positions: vertices 3 to 5 repeat 0 to 2, because the indirect draw starts at vertex 3
    [[nodiscard]] rhi::ResourceFuture<GraphicsResource> CreatePositions(ResourceContext& resources);

    void PrintDiagnostics(const loom::CollectingSink& sink);
}

#endif // !DIAMOND_DOGS_LOOM_DRAW_GRAPH_TEST_HPP
