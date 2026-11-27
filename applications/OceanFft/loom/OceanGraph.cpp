#include "OceanGraph.hpp"
#include "../simulation/OceanSimulation.hpp"
#include "DonelanBannerNormLUT.hpp"
#include "LoomGraph.hpp"
#include "Declarations.hpp"
#include "GraphBuilder.hpp"
#include "OceanFft.host.hpp"
#include "PassBuilders.hpp"
#include "LoomKeys.hpp"
#include "ResourceContext.hpp"
#include "ResourceFlags.hpp"
#include "ResourceFuture.hpp"
#include "ResourceTypes.hpp"
#include "RhiResourceFlags.hpp"
#include "ShaderLibraryTypes.hpp"
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <utility>

namespace ocean_loom
{  
    // Yes it's lazy, but we're in a source file so let me be gross!
    using namespace loom;

    namespace
    {
        namespace host = ocean_fft_shaders;
        constexpr host::CookProfile Profile = host::CookProfile::Spirv;
        using CascadeParameters = host::CascadeParameters<Profile, lodestone::LayoutRule::Std140>;
        using TickRows = host::OceanFft::OceanTickRowsCS::Params<Profile>;
        using ColumnsResolve = host::OceanFft::OceanColumnsResolveCS::Params<Profile>;
        using SpectrumParameters = host::SpectrumParameters<Profile, lodestone::LayoutRule::Std140>;

        // buffers used during sim: first two are uniform/constant buffers,
        // the next three are simulation buffers
        constexpr ResourceKey<Buffer> CascadeParams{ "Ocean.CascadeParams" };
        constexpr ResourceKey<Buffer> SpectrumParams{ "Ocean.SpectrumParams" };
        constexpr ResourceKey<Buffer> WaveData{ "Ocean.WaveData" };
        constexpr ResourceKey<Buffer> h0K{ "Ocean.h0K" };
        constexpr ResourceKey<Buffer> h0KConjugate{ "Ocean.h0KConjugate" };
        constexpr ResourceKey<Buffer> IfftInput{ "Ocean.IfftInput" };
        // resolve outputs - one layer per simulation cascade.
        constexpr ResourceKey<Image> Displacement{ "Ocean.Displacement" };
        constexpr ResourceKey<Image> Derivatives{ "Ocean.Derivatives" };
        constexpr ResourceKey<Image> Whitecaps{ "Ocean.Whitecaps" };
        // the Donelan-Banner normalization LUT: the host loads it, so it comes in as an import
        constexpr ResourceKey<Image> NormalizationLut{ "Ocean.NormalizationLut" };
        constexpr ResourceKey<Sampler> NormalizationSampler{ "Ocean.NormalizationSampler" };
        // passes and entrypoint params
        constexpr PassKey SpectrumInitPass{ "Ocean.SpectrumInit" };
        constexpr PassKey SpectrumConjugatePass{ "Ocean.SpectrumConjugate" };
        constexpr PassKey TickRowsPass{ "Ocean.TickRows" };
        constexpr PassKey ColumnsResolvePass{ "Ocean.ColumnsResolve" };
        constexpr ParamsKey<TickRows> TickRowsParams{ "Ocean.TickRowsParams" };
        constexpr ParamsKey<ColumnsResolve> ColumnsResolveParams{ "Ocean.ColumnsResolveParams" };

        constexpr uint32_t IfftSize{ 512u };
        constexpr uint32_t CascadeCount{ 5u };

        // Wonderfully, most of the Ifft-declared and used buffers have their sizes entirely
        // governed by the shader reflection data!
        constexpr BufferDeclaration ShaderSizedBuffer(ResourceKey<Buffer> key, ResourceLifetime lifetime) noexcept
        {
            return BufferDeclaration
            {
                .Key = key,
                .Lifetime = lifetime,
                .Size = SizeSource::Derived(), // the magic value! says to read data from reflection
                .Cpu = CpuAccess::None, // OceanFft tick buffers are entirely GPU-resident (yay!)
                .HistoryDepth = 0,
                .NoHistoryAction = OnNoHistory::Invalid // this is a valid value for 0 history depth buffers
            };
        }

        constexpr ImageDeclaration CascadeImage(ResourceKey<Image> key) noexcept
        {
            return ImageDeclaration
            {
                .Key = key,
                .Lifetime = ResourceLifetime::Persistent, // each pass samples last frames result
                .Format = lodestone::TextureFormat::RGBA16Float,
                .Extent = ExtentSource::Derived(), // same as buffers, yay!
                .Mips = MipSource::Full(), // generate full mip chain (later)
                .Layers = LayerSource::Derived(),
                .Cpu = CpuAccess::None,
                .HistoryDepth = 0,
                .NoHistoryAction = OnNoHistory::Invalid
            };
        }

        CascadeParameters MakeCascadeParameters() noexcept
        {
            static_assert(sizeof(ocean::CascadeParameters) == sizeof(CascadeParameters));
            static_assert(offsetof(ocean::CascadeParameters, LengthScales) == offsetof(CascadeParameters, LengthScales));
            static_assert(sizeof(ocean::CascadeParameters::LengthScales) == sizeof(CascadeParameters::LengthScales));
            static_assert(offsetof(ocean::CascadeParameters, CutoffsHigh) == offsetof(CascadeParameters, CutoffsHigh));
            static_assert(sizeof(ocean::CascadeParameters::CutoffsHigh) == sizeof(CascadeParameters::CutoffsHigh));
            static_assert(offsetof(ocean::CascadeParameters, CutoffsLow) == offsetof(CascadeParameters, CutoffsLow));
            static_assert(sizeof(ocean::CascadeParameters::CutoffsLow) == sizeof(CascadeParameters::CutoffsLow));
            static_assert(offsetof(ocean::CascadeParameters, Dimensions) == offsetof(CascadeParameters, Dimensions));
            static_assert(sizeof(ocean::CascadeParameters::Dimensions) == sizeof(CascadeParameters::Dimensions));
            static_assert(offsetof(ocean::CascadeParameters, Pad0) == offsetof(CascadeParameters, Pad0));
            static_assert(sizeof(ocean::CascadeParameters::Pad0) == sizeof(CascadeParameters::Pad0));
            
            CascadeParameters cascadeParams{};
            ocean::CascadeParameters legacyParams = ocean::MakeCascadeParameters(IfftSize);
            std::memcpy(&cascadeParams, &legacyParams, sizeof(CascadeParameters));
            return cascadeParams;
        }

        rhi::ResourceFuture<GraphicsResource> CreateUniform(ResourceContext& rsrc_context,
                                                            std::span<const std::byte> data_bytes,
                                                            const size_t size,
                                                            const char* name)
        {
            const RhiBufferResourceData data
            {
                .Data = data_bytes,
                .DestinationOffset = 0u
            };

            BufferCreateInfo bufferInfo
            {
                .Domain = ResourceDomain::CPUToGPU,
                .Flags = {},
                .ViewFormat = std::nullopt,
                .Size = size,
                .Name = name
            };

            return rsrc_context.CreateBuffer(bufferInfo, std::span{ &data, 1 });
        }

        rhi::ResourceFuture<GraphicsResource> CreateNormalizationLut(ResourceContext& rsrc_context)
        {
            RhiImageResourceData imageData
            {
                .Data = std::as_bytes(std::span{ DonelanBannerNormLUT }),
                .Width = DonelanBannerNormLUTWidth,
                .Height = DonelanBannerNormLUTHeight
            };

            ImageCreateInfo imageInfo
            {
                .Domain = ResourceDomain::GPUOnly,
                .Flags = {},
                .Type = ImageType::Type2D,
                .Format = CommonFormats::R32_Float,
                .Usage = ImageUsageBits::SampledImage | ImageUsageBits::ComputeShaderSRV,
                .Width = DonelanBannerNormLUTWidth,
                .Height = DonelanBannerNormLUTHeight,
                .Depth = 1u,
                .MipLevels = 1u,
                .ArrayLayers = 1u,
                .SampleCount = 1u,
                .ViewInfo = ImageViewCreateInfo{},
                .SamplerInfo = std::nullopt,
                .Name = "Ocean.NormalizationLut"
            };

            return rsrc_context.CreateImage(imageInfo, std::span{ &imageData, 1 }, ImageUsageBits::ComputeShaderSRV);
        }

    }

    SimulationImports SupplySimulation(loom::Graph& graph, ResourceContext& rsrc_context)
    {
        const CascadeParameters cascade = MakeCascadeParameters();
        const SpectrumParameters spectrum = std::bit_cast<SpectrumParameters>(ocean::SpectrumParameters{});

        rhi::ResourceFuture<GraphicsResource> cascadeReply =
            CreateUniform(rsrc_context, std::as_bytes(std::span{ &cascade, 1 }), sizeof(cascade), "Ocean.CascadeParams");
        rhi::ResourceFuture<GraphicsResource> spectrumReply =
            CreateUniform(rsrc_context, std::as_bytes(std::span{ &spectrum, 1 }), sizeof(spectrum), "Ocean.SpectrumParams");
        rhi::ResourceFuture<GraphicsResource> lutReply = CreateNormalizationLut(rsrc_context);

        // Supply takes the future and does not block: the graph waits for it to be ready itself.
        // But SimulationImports needs the handles for teardown, so read them first.
        SimulationImports imports
        {
            .CascadeParams = graph.BufferOf(CascadeParams),
            .SpectrumParams = graph.BufferOf(SpectrumParams),
            .NormalizationLut = graph.ImageOf(NormalizationLut),
        };

        graph.Supply(CascadeParams, std::move(cascadeReply));
        graph.Supply(SpectrumParams, std::move(spectrumReply));
        graph.Supply(NormalizationLut, std::move(lutReply), ImageUsageBits::ComputeShaderSRV);
        return imports;
    }

    void RegisterScreen(loom::GraphBuilder& graph)
    {
        graph.Import(k_Screen);
        const loom::OutputFallback fallback = loom::OutputFallback::Clear(0.02f, 0.03f, 0.05f, 1.0f);
        graph.Output(loom::OutputDeclaration{ .Target = k_Screen, .OnSkip = fallback });
    }

    // Old code: simulation/OceanSimulation.cpp (RecordTick, and Initialize on a spectrum change)
    void RegisterSimulation([[maybe_unused]] loom::GraphBuilder& graph)
    {
        using namespace ocean_fft_shaders;

        graph.Pin("IFFT_SIZE", AxisValue::Unsigned(IfftSize));
        graph.Import(BufferImportDeclaration{ .Key = CascadeParams, .ByteSize = sizeof(CascadeParameters) });
        graph.Import(BufferImportDeclaration{ .Key = SpectrumParams, .ByteSize = sizeof(SpectrumParameters) });
        graph.Declare(ShaderSizedBuffer(WaveData, ResourceLifetime::Persistent));
        graph.Declare(ShaderSizedBuffer(h0K, ResourceLifetime::Persistent));
        graph.Declare(ShaderSizedBuffer(h0KConjugate, ResourceLifetime::Persistent));
        graph.Declare(ShaderSizedBuffer(IfftInput, ResourceLifetime::Frame));

        graph.Declare(CascadeImage(Displacement));
        graph.Declare(CascadeImage(Derivatives));
        graph.Declare(CascadeImage(Whitecaps));

        // The LUT size comes with the content (DonelanBannerNormLUTWidth x Height in the old code)
        const ImageExpectation lutExpectation = ImageExpectation::AnyExtent(lodestone::TextureFormat::R32Float, 1);
        graph.Import(ImportDeclaration{ .Key = NormalizationLut, .Expectation = lutExpectation });

        SamplerCreateInfo lutSampler{};
        lutSampler.AddressU = SamplerCreateInfo::AddressMode::ClampToEdge;
        lutSampler.AddressV = SamplerCreateInfo::AddressMode::ClampToEdge;
        lutSampler.AddressW = SamplerCreateInfo::AddressMode::ClampToEdge;
        lutSampler.Name = "Ocean.NormalizationSampler";
        graph.Declare(SamplerDeclaration{ .Key = NormalizationSampler, .CreateInfo = lutSampler });

        // The shared bindings are all RW in the Slang, so each Bind states what the entry point really does.
        // Init: h0(k) and the wave data, then the conjugate. Both run again only when a new SpectrumParams or
        // CascadeParams is supplied. Loom orders them by data flow: Conjugate reads the h0K that Init writes.
        const auto spectrumInit = [](DispatchBuilder& builder)
        {
            namespace entryPoint = host::OceanFft::SpectrumInitCS;
            builder.Shader("OceanFft", "SpectrumInitCS");
            builder.Bind(entryPoint::CascadeParams, CascadeParams);
            builder.Bind(entryPoint::SpectrumParams, SpectrumParams);
            builder.Bind(entryPoint::WaveData, WaveData, AccessModifier::WriteOnly);
            builder.Bind(entryPoint::h0K, h0K, AccessModifier::WriteOnly);
            builder.Bind(entryPoint::normTexture, NormalizationLut);
            builder.Bind(entryPoint::normSampler, NormalizationSampler);
            // numthreads (8, 8, 5): one thread per texel per cascade
            builder.DispatchThreads(IfftSize, IfftSize, CascadeCount);
        };

        graph.AddDispatch(SpectrumInitPass,
                          RunPolicy::WhenInputsChange,
                          QueueAffinity::Graphics,
                          spectrumInit);

        const auto spectrumConjugate = [](DispatchBuilder& builder)
        {
            namespace entryPoint = host::OceanFft::SpectrumConjugateCS;
            builder.Shader("OceanFft", "SpectrumConjugateCS");
            builder.Bind(entryPoint::CascadeParams, CascadeParams);
            builder.Bind(entryPoint::h0K, h0K, AccessModifier::ReadOnly);
            builder.Bind(entryPoint::h0KConjugate, h0KConjugate, AccessModifier::WriteOnly);
            builder.DispatchThreads(IfftSize, IfftSize, CascadeCount);
        };

        graph.AddDispatch(SpectrumConjugatePass,
                          RunPolicy::WhenInputsChange,
                          QueueAffinity::Graphics,
                          spectrumConjugate);

        const auto tickRows = [](DispatchBuilder& builder)
        {
            namespace entryPoint = host::OceanFft::OceanTickRowsCS;
            // todo: OceanTickRowsCS should have a ::Name field instead of hardcoding the string here
            builder.Shader("OceanFft", "OceanTickRowsCS");
            builder.Bind(entryPoint::CascadeParams, CascadeParams);
            builder.Bind(entryPoint::WaveData, WaveData, AccessModifier::ReadOnly);
            builder.Bind(entryPoint::h0K, h0K, AccessModifier::ReadOnly);
            builder.Bind(entryPoint::h0KConjugate, h0KConjugate, AccessModifier::ReadOnly);
            builder.Bind(entryPoint::IfftInput, IfftInput, AccessModifier::WriteOnly);
            builder.Params(TickRowsParams);
            builder.DispatchThreads(1, IfftSize / 4, CascadeCount);
        };

        graph.AddDispatch(TickRowsPass,
                          RunPolicy::EveryFrame,
                          QueueAffinity::Graphics,
                          tickRows);
        
        const auto columnsAndResolve = [](DispatchBuilder& builder)
        {
            namespace entryPoint = host::OceanFft::OceanColumnsResolveCS;
            builder.Shader("OceanFft", "OceanColumnsResolveCS");
            builder.Bind(entryPoint::Derivatives, Derivatives, AccessModifier::WriteOnly);
            builder.Bind(entryPoint::Displacement, Displacement, AccessModifier::WriteOnly);
            builder.Bind(entryPoint::IfftInput, IfftInput, AccessModifier::ReadOnly);
            builder.Bind(entryPoint::Whitecaps, Whitecaps, AccessModifier::WriteOnly);
            builder.Params(ColumnsResolveParams);
            builder.DispatchThreads(1, IfftSize / 4, CascadeCount);
        };

        graph.AddDispatch(ColumnsResolvePass,
                          RunPolicy::EveryFrame,
                          QueueAffinity::Graphics,
                          columnsAndResolve);
    }

    // Old code: foam/OceanFoam.cpp and foam/FoamReplay.cpp
    void RegisterFoam([[maybe_unused]] loom::GraphBuilder& graph)
    {
    }

    // Old code: environment/OceanAtmosphere.cpp and environment/OceanEnvironment.cpp
    void RegisterEnvironment([[maybe_unused]] loom::GraphBuilder& graph)
    {
    }

    // Old code: mesh/OceanCdlod.cpp, and the prepass and GBuffer in OceanScene.cpp (recordCommands)
    void RegisterSurface([[maybe_unused]] loom::GraphBuilder& graph)
    {
    }

    // Old code: the deferred, TAA and exposure passes in OceanScene.cpp
    void RegisterLighting([[maybe_unused]] loom::GraphBuilder& graph)
    {
    }

    // Old code: the ColorPipe composite call in OceanScene.cpp
    void RegisterComposite([[maybe_unused]] loom::GraphBuilder& graph)
    {
    }

}
