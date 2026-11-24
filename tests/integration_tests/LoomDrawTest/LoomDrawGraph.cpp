#include "LoomDrawGraph.hpp"
#include "Compile.hpp"
#include "CompiledGraph.hpp"
#include "Declarations.hpp"
#include "DeviceStage.hpp"
#include "GraphBuilder.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "PassBuilders.hpp"
#include "PassStatements.hpp"
#include "ResourceContext.hpp"
#include "ResourceTypes.hpp"
#include "RhiResourceFlags.hpp"
#include "ShaderBundle.hpp"
#include "ShaderProgram.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iostream>
#include <optional>
#include <utility>

namespace
{
    using namespace loom_draw;

    constexpr loom::ResourceKey<loom::Buffer> k_Arguments{ "LoomDraw.Arguments" };
    constexpr loom::ResourceKey<loom::Buffer> k_DrawCount{ "LoomDraw.DrawCount" };
    constexpr loom::ResourceKey<loom::Image> k_Depth{ "LoomDraw.Depth" };
    constexpr loom::ResourceKey<loom::Image> k_GAlbedo{ "LoomDraw.Albedo" };
    constexpr loom::ResourceKey<loom::Image> k_GNormal{ "LoomDraw.Normal" };
    constexpr loom::ResourceKey<loom::Sampler> k_Point{ "LoomDraw.Point" };

    constexpr loom::PassKey k_Cull{ "LoomDraw.Cull" };
    constexpr loom::PassKey k_Prepass{ "LoomDraw.Prepass" };
    constexpr loom::PassKey k_GBuffer{ "LoomDraw.GBuffer" };

    loom::ImageDeclaration TargetSized(loom::ResourceKey<loom::Image> key, lodestone::TextureFormat format)
    {
        return loom::ImageDeclaration
        {
            .Key = key,
            .Lifetime = loom::ResourceLifetime::Frame,
            .Format = format,
            .Extent = loom::ExtentSource::OfTarget(k_Screen, 1.0f),
            .Mips = loom::MipSource::One(),
            .Layers = 1,
            .Cpu = loom::CpuAccess::None,
            .HistoryDepth = 0,
            .NoHistoryAction = loom::OnNoHistory::Invalid,
        };
    }

    loom::BufferDeclaration Bytes(loom::ResourceKey<loom::Buffer> key, int64_t bytes)
    {
        return loom::BufferDeclaration
        {
            .Key = key,
            .Lifetime = loom::ResourceLifetime::Frame,
            .Size = loom::SizeSource::Bytes(bytes),
            .Cpu = loom::CpuAccess::None,
            .HistoryDepth = 0,
            .NoHistoryAction = loom::OnNoHistory::Invalid,
        };
    }

    void DeclareResources(loom::GraphBuilder& graph)
    {
        graph.Import(k_Screen);
        const loom::OutputFallback fallback = loom::OutputFallback::Clear(k_FallbackColor[0], k_FallbackColor[1],
                                                                          k_FallbackColor[2], k_FallbackColor[3]);
        graph.Output(loom::OutputDeclaration{ .Target = k_Screen, .OnSkip = fallback });

        graph.Import(loom::BufferImportDeclaration{ .Key = k_Positions, .ByteSize = 96 });
        graph.Declare(Bytes(k_Arguments, 16));
        graph.Declare(Bytes(k_DrawCount, 4));
        graph.Declare(TargetSized(k_Depth, lodestone::TextureFormat::Depth32Float));
        graph.Declare(TargetSized(k_GAlbedo, lodestone::TextureFormat::RGBA8Unorm));
        graph.Declare(TargetSized(k_GNormal, lodestone::TextureFormat::RGBA16Float));

        SamplerCreateInfo point{};
        point.MagFilter = ImageFilteringMode::Nearest;
        point.MinFilter = ImageFilteringMode::Nearest;
        point.MipFilter = ImageFilteringMode::Nearest;
        point.AddressU = SamplerCreateInfo::AddressMode::ClampToEdge;
        point.AddressV = SamplerCreateInfo::AddressMode::ClampToEdge;
        point.AddressW = SamplerCreateInfo::AddressMode::ClampToEdge;
        point.Name = "LoomDraw.Point";
        graph.Declare(loom::SamplerDeclaration{ .Key = k_Point, .CreateInfo = point });
    }

    void AddPasses(loom::GraphBuilder& graph)
    {
        constexpr loom::RunPolicy k_EveryFrame = loom::RunPolicy::EveryFrame;
        constexpr loom::QueueAffinity k_Graphics = loom::QueueAffinity::Graphics;
        constexpr loom::RasterState k_CullBack{ .Cull = loom::CullMode::Back, .Front = loom::FrontFace::CounterClockwise };

        const auto cull = [](loom::DispatchBuilder& pass)
        {
            pass.Shader("LoomDraw", "CullCS");
            pass.Bind("DrawArguments", k_Arguments, loom::AccessModifier::WriteOnly);
            pass.Bind("DrawCount", k_DrawCount, loom::AccessModifier::WriteOnly);
            pass.DispatchThreads(1, 1, 1);
        };

        const auto prepass = [&k_CullBack](loom::DrawBuilder& pass)
        {
            pass.Shaders("LoomDraw", "SceneVS", "");
            pass.Bind("Positions", k_Positions);
            pass.Raster(k_CullBack);
            pass.Depth(k_Depth, loom::DepthLoad::Clear(1.0f), loom::AttachmentStore::Store, loom::CompareOp::Less,
                       loom::DepthWrite::Enabled);
            pass.DrawIndirectCount(k_Arguments, 0, k_DrawCount, 0, 1);
        };

        const auto gbuffer = [&k_CullBack](loom::DrawBuilder& pass)
        {
            pass.Shaders("LoomDraw", "SceneVS", "GBufferPS");
            pass.Bind("Positions", k_Positions);
            pass.Raster(k_CullBack);
            pass.Color(0, k_GAlbedo, loom::ColorLoad::Clear(0.0f, 0.0f, 0.0f, 0.0f), loom::AttachmentStore::Store,
                       loom::BlendMode::Opaque);
            pass.Color(1, k_GNormal, loom::ColorLoad::Clear(0.0f, 0.0f, 0.0f, 0.0f), loom::AttachmentStore::Store,
                       loom::BlendMode::Opaque);
            pass.Depth(k_Depth, loom::DepthLoad::Keep(), loom::AttachmentStore::Store, loom::CompareOp::Equal,
                       loom::DepthWrite::Disabled);
            pass.Draw(3, 1, 0, 0);
        };

        const auto light = [](loom::DrawBuilder& pass)
        {
            pass.Shaders("LoomDraw", "FullscreenVS", "LightPS");
            pass.Bind("Albedo", k_GAlbedo);
            pass.Bind("Normal", k_GNormal);
            pass.Bind("PointSampler", k_Point);
            pass.Color(0, k_Screen, loom::ColorLoad::DontCare(), loom::AttachmentStore::Store, loom::BlendMode::Opaque);
            pass.Draw(3, 1, 0, 0);
        };

        graph.AddDispatch(k_Cull, k_EveryFrame, k_Graphics, cull);
        graph.AddDraw(k_Prepass, k_EveryFrame, k_Graphics, prepass);
        graph.AddDraw(k_GBuffer, k_EveryFrame, k_Graphics, gbuffer);
        graph.AddDraw(k_Light, k_EveryFrame, k_Graphics, light);
    }

    constexpr bootstrap::Need k_Needs[]
    {
        bootstrap::Need
        {
            .Name = rhi::k_ShaderObjectFeature,
            .Kind = bootstrap::NeedKind::DeviceFeature,
            .Level = bootstrap::NeedLevel::Required,
        },
        bootstrap::Need
        {
            .Name = "VkPhysicalDeviceVulkan12Features.drawIndirectCount",
            .Kind = bootstrap::NeedKind::DeviceFeature,
            .Level = bootstrap::NeedLevel::Required,
        },
    };
}

namespace loom_draw
{

    std::span<const bootstrap::Need> DeviceNeeds() noexcept
    {
        return k_Needs;
    }

    void PrintDiagnostics(const loom::CollectingSink& sink)
    {
        for (const loom::Diagnostic& diagnostic : sink.Diagnostics())
        {
            std::cerr << std::format("{} {}: {} (pass '{}', resource '{}')\n", loom::ToString(diagnostic.Code),
                                     loom::ToString(diagnostic.Severity), loom::SummaryOf(diagnostic.Code),
                                     diagnostic.Pass.Name, diagnostic.Resource.Name);
        }
    }

    std::unique_ptr<loom::Graph> BuildAndRealize(const RealizeArguments& arguments)
    {
        loom::GraphBuilder builder{ "LoomDraw.Graph", "LoomDraw.Shaders" };
        DeclareResources(builder);
        AddPasses(builder);

        const loom::CompileInputs inputs
        {
            .Bundle = arguments.Bundle,
            .Supports = arguments.Filter.Supports,
            .SupportsUser = arguments.Filter.User,
            .ShaderLimits = loom::ShaderLimitsOf(arguments.Device),
            .Limits = loom::LayoutLimitsOf(arguments.Device),
        };

        std::optional<loom::CompiledGraph> compiled = loom::Compile(builder, inputs, arguments.Sink);

        if (!compiled.has_value())
        {
            std::cerr << "Compile failed\n";
            PrintDiagnostics(arguments.Sink);
            return nullptr;
        }

        const loom::RealizeInputs realizeInputs
        {
            .Resources = arguments.Resources,
            .Device = arguments.Device,
            .FrameSlots = arguments.FrameSlots,
            .Timestamps = loom::PassTimestamps::Off,
            .Sink = arguments.Sink,
            .FrameTargets = arguments.Targets,
            .TargetSettleSeconds = arguments.TargetSettleSeconds,
        };

        loom::RealizeResult realized = loom::Graph::Realize(std::move(*compiled), realizeInputs);

        if (!realized.has_value())
        {
            std::cerr << std::format("Realize refused the graph: {}\n", loom::ToString(realized.error()));
            return nullptr;
        }

        return std::move(*realized);
    }

    rhi::ResourceFuture<GraphicsResource> CreatePositions(ResourceContext& resources)
    {
        std::array<std::array<float, 4u>, 6u> positions{};
        std::ranges::copy(k_Triangle, positions.begin());
        std::ranges::copy(k_Triangle, positions.begin() + 3);
        const RhiBufferResourceData initial{ std::as_bytes(std::span{ positions }), 0u };

        BufferCreateInfo info{};
        info.Domain = ResourceDomain::GPUOnly;
        info.Usage = BufferUsageBits::VertexShaderSRV;
        info.Size = sizeof(positions);
        info.Name = "LoomDraw.Positions";
        return resources.CreateBuffer(info, std::span{ &initial, 1u });
    }

}
