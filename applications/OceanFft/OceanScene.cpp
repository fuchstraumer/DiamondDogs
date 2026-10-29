#include "OceanScene.hpp"
#include "ArcballCamera.hpp"
#include "ColorPipe.hpp"
#include "DisplayTarget.hpp"
#include "OceanCdlod.hpp"
#include "OceanEnvironment.hpp"
#include "Queue.hpp"
#include "SeabedMaterial.hpp"
#include "Semaphore.hpp"
#include "CommandBuffer.hpp"
#include "CommandPool.hpp"
#include "Device.hpp"
#include "FrameCapture.hpp"
#include "GpuTimers.hpp"
#include "ImGuiModule.hpp"
#include "ImageDataFormats.hpp"
#include "ManifestDescriptors.hpp"
#include "FoamMaterial.hpp"
#include "FoamReplay.hpp"
#include "math/Math.hpp"
#include "OceanFoam.hpp"
#include "OceanSimulation.hpp"
#include "OceanUi.hpp"
#include "PlatformSystem.hpp"
#include "PlatformTypes.hpp"
#include "ResourceContext.hpp"
#include "ResourceFlags.hpp"
#include "ResourceMessageReply.hpp"
#include "ResourceTypes.hpp"
#include "SceneCommands.hpp"
#include "SeaState.hpp"
#include "RhiAssert.hpp"
#include "RhiFlags.hpp"
#include "RhiHandle.hpp"
#include "RhiResult.hpp"
#include "RhiSystem.hpp"
#include "RhiTypes.hpp"
#include "ShaderLibrary.hpp"
#include "ShaderManifestIndex.hpp"
#include "ShaderObjectState.hpp"
#include "ShaderProgram.hpp"
#include "Swapchain.hpp"
#include "VariantKey.hpp"
#include "VulkanScene.hpp"
#include "imgui.h"
#include "math/Math.hpp"
#include "vulkan/vulkan_core.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <expected>
#include <format>
#include <iostream>
#include <memory>
#include <numbers>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
    constexpr uint64_t k_TimeoutNs = 10'000'000'000ull;
    constexpr std::string_view k_RenderModule{ "OceanRender" };
    constexpr std::array<std::string_view, 2> k_SurfaceEntryPoints{ "OceanSurfaceVS", "OceanSurfaceFS" };
    constexpr std::array<std::string_view, 2> k_SeabedEntryPoints{ "SeabedVS", "SeabedFS" };
    // the estimate of GPU timer scopes per frame (the panel shows any that were dropped)
    constexpr uint32_t k_MaxGpuTimerScopes = 128u;
    // the weight of the newest frame in each scope's rolling average
    constexpr double k_GpuTimingSmoothing = 0.05;

    // prints the message to stderr, then throws it, so that a failed setup is never silent
    [[noreturn]] void Fail(std::string_view message);

    struct alignas(16) FrameConstants
    {
        std::array<std::array<float, 4>, 9> SkyShCoeffs{};
        std::array<float, 3> SunRadianceAp1{ 1.0f, 1.0f, 1.0f };
        float FoamRoughnessScale{ 1.0f };
        std::array<float, 3> WaterScatterColorAp1{ 0.02f, 0.15f, 0.2f };
        float SssStrength{ 0.5f };
        std::array<float, 3> SeabedTintAp1{ 0.6f, 0.5f, 0.4f };
        float SeabedRoughnessScale{ 1.0f };
        uint32_t UseCdlod{ 1u };
        uint32_t UseSkyTexture{ 1u };
        float SkyIntensityScale{ 1.0f };
        float LeadrSlopeVarianceScale{ 1.0f };
        std::array<std::array<float, 4>, 4> PrevViewProjectionRows{};
        std::array<float, 2> JitterOffsetNdc{ 0.0f, 0.0f };
        std::array<float, 2> PrevJitterOffsetNdc{ 0.0f, 0.0f };
        uint32_t UseBrunetonAtmosphere{ 0u };
        uint32_t UseDeferredPath{ 0u };
        uint32_t UseSpecularAa{ 1u };
        uint32_t UseTaa{ 1u };
        float SpecularAaStrength{ 1.0f };
        float SsrIntensity{ 1.0f };
        float TaaBlendAlpha{ 0.1f };
        float AutoExposureSpeed{ 1.0f };
    };
    static_assert(sizeof(FrameConstants) == 320u);

    constexpr std::string_view k_DeferredModule{ "OceanDeferred" };
    constexpr std::array<std::string_view, 2> k_GBufferEntryPoints{ "OceanSurfaceVS", "OceanSurfaceGBufferFS" };
    constexpr std::string_view k_SpecularAaEntry{ "OceanSpecularAaCS" };
    constexpr std::string_view k_HiZEntry{ "OceanHiZDownsampleCS" };
    constexpr std::string_view k_DeferredLightingEntry{ "OceanDeferredLightingCS" };
    constexpr std::string_view k_TaaResolveEntry{ "OceanTaaResolveCS" };
    constexpr std::string_view k_AutoExposureEntry{ "OceanAutoExposureCS" };

    template<typename T>
    void PushProgramConstants(rhi::CommandBuffer& cmd, const rhi::ManifestProgram& program, const T& data)
    {
        if (const std::optional<rhi::PushConstantRange> range = program.PushConstants(); range.has_value())
        {
            cmd.PushConstants(program.PipelineLayout(), range->StageFlags, 0u,
                std::as_bytes(std::span{ &data, 1 }).first(std::min<size_t>(sizeof(T), range->Size)));
        }
    }

    struct alignas(16) AtmospherePushConstants
    {
        float BottomRadiusKm{ 6360.0f };
        float TopRadiusKm{ 6420.0f };
        float RayleighScaleHeightKm{ 8.0f };
        float MieScaleHeightKm{ 1.2f };
        math::Float3 RayleighScattering{ 5.802e-3f, 13.558e-3f, 33.1e-3f };
        float MieAnisotropy{ 0.8f };
        math::Float3 MieScattering{ 3.996e-3f, 3.996e-3f, 3.996e-3f };
        float MieExtinction{ 4.440e-3f };
        math::Float3 OzoneAbsorption{ 0.650e-3f, 1.881e-3f, 0.085e-3f };
        float OzoneCenterKm{ 25.0f };
        float OzoneWidthKm{ 15.0f };
        float GroundAlbedo{ 0.1f };
        float Pad0[2]{ 0.0f, 0.0f };
        math::Float3 SolarIrradiance{ 1.0f, 1.0f, 1.0f };
        uint32_t ScatteringOrder{ 1u };
    };
    static_assert(sizeof(AtmospherePushConstants) == 96u);

    struct alignas(16) SpecularAaPushConstants
    {
        std::array<uint32_t, 2> Dimensions{};
        float SpecularAaStrength{ 1.0f };
        float Padding{ 0.0f };
    };
    static_assert(sizeof(SpecularAaPushConstants) == 16u);

    struct alignas(16) HiZPushConstants
    {
        std::array<uint32_t, 2> DstDimensions{};
        std::array<uint32_t, 2> SrcDimensions{};
    };
    static_assert(sizeof(HiZPushConstants) == 16u);

    struct alignas(16) DeferredLightingPushConstants
    {
        std::array<std::array<float, 4>, 4> InvViewProjection{};
        std::array<uint32_t, 2> Dimensions{};
        std::array<float, 2> InvDimensions{};
    };
    static_assert(sizeof(DeferredLightingPushConstants) == 80u);

    struct alignas(16) TaaPushConstants
    {
        std::array<uint32_t, 2> Dimensions{};
        std::array<float, 2> InvDimensions{};
    };
    static_assert(sizeof(TaaPushConstants) == 16u);

    struct alignas(16) AutoExposurePushConstants
    {
        std::array<uint32_t, 2> Dimensions{};
        float DeltaTime{ 0.016f };
        float AdaptationSpeed{ 1.0f };
        float MinLogLum{ -8.0f };
        float MaxLogLum{ 4.0f };
        std::array<float, 2> Padding{ 0.0f, 0.0f };
    };
    static_assert(sizeof(AutoExposurePushConstants) == 32u);

    ocean::OceanCdlodCameraState ComputeCdlodCamera(const ArcballCamera::Matrix4& vp, const ArcballCamera::Vector3& camPos) noexcept
    {
        ocean::OceanCdlodCameraState state{};
        state.CameraPosition = { camPos.x, camPos.y, camPos.z };

        const auto planes = math::ExtractFrustumPlanes(vp, true);
        for (size_t i = 0; i < 6; ++i)
        {
            state.FrustumPlanes[i] = { planes[i].x, planes[i].y, planes[i].z, planes[i].w };
        }

        return state;
    }

    // OceanViewParameters in OceanRenderCommon.slang (std140, all float4)
    struct OceanViewParameters
    {
        std::array<std::array<float, 4>, 4> ViewProjectionRows{};
        std::array<float, 4> CameraPositionTime{};
        // float4[2] in the shader, so eight cascades fit
        std::array<float, 8> LengthScales{};
        std::array<float, 8> CascadeWeights{};
        std::array<float, 8> FoamWeights{};
        std::array<float, 4> SunDirectionSeabedDepth{};
        // x: fade distance in patch lengths, y: mip 0 size in texels, z: 1 when cascades are rotated, w: unused
        std::array<float, 4> LodParameters{};
        // x: WhitecapThreshold, y: FoamAmount, z: CrestSharpness, w: unused
        std::array<float, 4> WhitecapParameters{ 0.5f, 1.0f, 1.0f, 0.0f };
        // x: 1 lets the persistent foam raise the breaking threshold, y: the raise, z, w: texel size of clipmap levels 0 and 1 in meters
        std::array<float, 4> PersistentFoamParameters{};
        // xy: the foam's drift-frame offset in world meters, zw: the world xz the clipmap windows are centered on
        std::array<float, 4> FoamDrift{};
        // x: residual foam amount, y: its dissolve softness, z: meters per tile of the foam texture, w: unused
        std::array<float, 4> FoamResidualParameters{};
        // x: underwater foam strength, y: sun specular scale, zw: unused
        std::array<float, 4> ShadingParameters{};
        // x: albedo gain on the foam texture's luminance, y: normal strength, zw: unused
        std::array<float, 4> FoamTextureParameters{};
        // rgb: the albedo tint of the foam material, w: unused
        std::array<float, 4> FoamTextureTint{};
    };
    static_assert(sizeof(OceanViewParameters) == 320u);

    // OceanGridParameters, the push constants of both vertex stages
    struct OceanGridParameters
    {
        float OriginX{ 0.0f };
        float OriginZ{ 0.0f };
        float Size{ 0.0f };
        uint32_t Resolution{ 1u };
        uint32_t Ring{ 0u };
        uint32_t OuterResolution{ 1u };
    };

    // the display scaling in OceanShading.slang: these must stay equal to k_PaperWhiteNits and k_ScRgbRefNits there
    constexpr float k_ScRgbRefNits = 80.0f;
    constexpr float k_PaperWhiteNits = 348.0f;

    constexpr float k_DegreesToRadians = std::numbers::pi_v<float> / 180.0f;

    /** @brief How the swapchain encodes color, or nothing when FrameCapture cannot convert it */
    std::optional<capture::CaptureEncoding> CaptureEncodingFor(ImageFormat format, ColorSpace color_space)
    {
        std::optional<capture::CaptureEncoding> encoding;
        switch (color_space)
        {
        case ColorSpace::sRGB_Nonlinear:
            encoding = (format.ComponentFormat == ImageComponentFormats::RGBA16) ?
                capture::CaptureEncoding::LinearExtendedSRGB : capture::CaptureEncoding::sRGB;
            break;
        case ColorSpace::Extended_sRGB_Linear:
            encoding = capture::CaptureEncoding::LinearExtendedSRGB;
            break;
        case ColorSpace::HDR10_ST2084:
            encoding = capture::CaptureEncoding::HDR10_PQ;
            break;
        case ColorSpace::PassThrough:
        default:
            if (format.ComponentFormat == ImageComponentFormats::RGBA16)
            {
                encoding = capture::CaptureEncoding::LinearExtendedSRGB;
            }
            else
            {
                encoding = capture::CaptureEncoding::sRGB;
            }
            break;
        }

        if (encoding.has_value() && !capture::FrameCapture::IsSupported(format, *encoding))
        {
            encoding.reset();
        }

        return encoding;
    }

    [[nodiscard]] GraphicsResource WaitForResource(const std::shared_ptr<GraphicsResourceReply>& reply, std::string_view what)
    {
        if (reply == nullptr || reply->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed)
        {
            Fail(std::format("OceanScene: creating {} failed", what));
        }
        return reply->GetResource();
    }
}

OceanScene::OceanScene(rhi::RhiSystem* rhi_system, PlatformWindowSystem* platform_system, OceanSceneSettings _settings) :
    VulkanScene(rhi_system, platform_system),
    settings{ _settings },
    spectrum{ _settings.Simulation.Spectrum },
    choppiness{ _settings.Simulation.Choppiness }
{
    // the simulation starts from the sea state, not from the raw spectrum defaults
    applySeaState();
    settings.Simulation.Spectrum = spectrum;

    camera.Current.Distance = 0.6f * settings.GridSize;

    if (settings.InitialCamera.has_value())
    {
        camera.Current = *settings.InitialCamera;
    }
    initialCameraState = camera.Current;
}

OceanScene::~OceanScene()
{
    Destroy();
}

void OceanScene::Initialize(void* user_data)
{
    createFrameSyncObjects();
    commandPools.reserve(numFramebuffers);
    for (uint32_t i = 0; i < numFramebuffers; ++i)
    {
        std::unique_ptr<rhi::CommandPool>& framePool = commandPools.emplace_back(
            std::make_unique<rhi::CommandPool>(device->Handle(), rhi::CommandPool::Type::Graphics, device->GetQueueFamilyIndices().Graphics));
        RhiAssert(framePool->AllocateCommandBuffers(1));
    }

    // one block per frame command buffer, plus one: a frame's block is collected only after its fence has passed
    gpuTimers = std::make_unique<rhi::GpuTimers>(*device, rhi::GpuTimersCreateInfo{ device->GetQueueFamilyIndices().Graphics, numFramebuffers + 1u, k_MaxGpuTimerScopes });
    LoadShaderObjectStateFunctions(vkDevice);

    resources = std::make_unique<ResourceContext>();
    resources->Initialize(ResourceContextCreateInfo{ rhiSystem->GetInstance(), rhiSystem->GetDevice(), rhiSystem->GetVulkanApiVersion(), settings.ValidationEnabled });

    rhi::ShaderLibraryResult<std::unique_ptr<rhi::ShaderLibrary>> opened = rhi::ShaderLibrary::Open(settings.ManifestPath);
    if (!opened.has_value())
    {
        Fail(std::format("OceanScene: opening {} failed: {}", settings.ManifestPath.string(), rhi::ToString(opened.error())));
    }

    library = std::move(*opened);

    auto created = ocean::OceanSimulation::Create(*device, *resources, *library, settings.Simulation);
    if (!created.has_value())
    {
        Fail(std::format("OceanScene: {}", created.error()));
    }

    simulation = std::move(*created);

    // linear and repeating: each cascade tiles with its own patch size. Anisotropic, because most of the water is seen
    // at a grazing angle, where one trilinear lookup blurs the waves away
    SamplerCreateInfo samplerInfo{};
    VkPhysicalDeviceFeatures features{};
    vkGetPhysicalDeviceFeatures(vkPhysicalDevice, &features);

    if (features.samplerAnisotropy == VK_TRUE && settings.MaxAnisotropy > 1.0f)
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(vkPhysicalDevice, &properties);
        samplerInfo.AnisotropyEnabled = true;
        samplerInfo.MaxAnisotropy = std::min(settings.MaxAnisotropy, properties.limits.maxSamplerAnisotropy);
        anisotropy = samplerInfo.MaxAnisotropy;
    }

    sampler = WaitForResource(resources->CreateSampler(samplerInfo), "the ocean sampler");

    viewBuffers.reserve(numFramebuffers);
    viewBufferPointers.reserve(numFramebuffers);
    for (uint32_t i = 0; i < numFramebuffers; ++i)
    {
        BufferCreateInfo info{};
        info.Domain = ResourceDomain::CPUToGPU;
        info.Flags = ResourceCreationFlags::PersistentlyMapped;
        info.Usage = BufferUsageBits::VertexShaderUBO | BufferUsageBits::FragmentShaderUBO;
        info.Size = sizeof(OceanViewParameters);
        const GraphicsResource& buffer = viewBuffers.emplace_back(WaitForResource(resources->CreateBuffer(info, {}), "a view buffer"));

        std::shared_ptr<PointerMessageReply> map = resources->MapBuffer(buffer, sizeof(OceanViewParameters), 0u);
        if (map->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed)
        {
            Fail("OceanScene: mapping a view buffer failed");
        }

        viewBufferPointers.push_back(map->GetPointer());
    }

    std::expected<std::unique_ptr<ocean::FoamMaterial>, std::string> loadedMaterial =
        ocean::FoamMaterial::Load(*resources, settings.FoamMaterialDirectory, settings.FoamMaterialName);
    if (!loadedMaterial.has_value())
    {
        Fail(std::format("OceanScene: the foam material: {}", loadedMaterial.error()));
    }

    foamMaterial = std::move(*loadedMaterial);

    // the foam binds the view buffers and the sampler, so it is created after them and destroyed before them
    auto createdFoam = ocean::OceanFoam::Create(*device, *resources, *library, *simulation, viewBuffers, sampler, foamSettings);
    if (!createdFoam.has_value())
    {
        Fail(std::format("OceanScene: the foam: {}", createdFoam.error()));
    }

    foam = std::move(*createdFoam);

    // ColorPipe library and pipeline
    auto libResult = rhi::ShaderLibrary::Open(settings.ColorPipeManifestPath);
    if (!libResult.has_value())
    {
        Fail(std::format("OceanScene: loading ColorPipe manifest {}: {}", settings.ColorPipeManifestPath.string(), rhi::ToString(libResult.error())));
    }
    colorPipeLibrary = std::move(*libResult);

    color::SurfaceChoice choice{};
    choice.Format = swapchain->ImageFormat();
    choice.Space = swapchain->ColorSpace();
    if (choice.Space == ColorSpace::sRGB_Nonlinear)
    {
        choice.Encoding = color::DisplayEncoding::SrgbNonlinear;
    }
    else if (choice.Space == ColorSpace::Extended_sRGB_Linear)
    {
        choice.Encoding = color::DisplayEncoding::ScRgbLinear;
    }
    else if (choice.Space == ColorSpace::HDR10_ST2084)
    {
        choice.Encoding = color::DisplayEncoding::Hdr10Pq;
    }
    else
    {
        choice.Encoding = color::DisplayEncoding::SrgbNonlinear;
    }

    const math::Float2 swapExtent = swapchain->GetExtent();
    const color::ColorPipeCreateInfo pipeInfo
    {
        .Device = device,
        .Resources = resources.get(),
        .Library = colorPipeLibrary.get(),
        .TargetSwapchain = const_cast<Swapchain*>(swapchain),
        .Width = static_cast<uint32_t>(swapExtent.x),
        .Height = static_cast<uint32_t>(swapExtent.y),
        .Capabilities = platformSystem->GetActiveDisplayInfo().ColorCapabilities,
        .Surface = choice,
        .Tonemapper = tonemapper
    };

    auto pipeResult = color::ColorPipe::Create(pipeInfo);
    if (!pipeResult.has_value())
    {
        Fail(std::format("OceanScene: creating ColorPipe failed: {}", color::ToString(pipeResult.error())));
    }
    colorPipe = std::move(*pipeResult);
    colorPipe->SetBloomSettings(bloomSettings);
    colorPipe->SetExposureEv(exposureEv);

    // Ocean CDLOD
    ocean::OceanCdlodSettings cdlodSettings{};
    cdlodSettings.LodDistanceScale = lodDistanceScale;
    cdlodSettings.MorphStartRatio = morphStartRatio;
    cdlodSettings.WorldExtent = 131072.0f;
    cdlodSettings.MaxSelectedPatches = 16384u;
    cdlodSettings.MaxNodes = 131072u;
    float initFinestCascadeLength = 3.0f;
    for (uint32_t c = 0u; c < ocean::k_CascadeCount; ++c)
    {
        if (cascadeEnabled[c] && simulation->Settings().LengthScales[c] > 0.0f)
        {
            initFinestCascadeLength = std::min(initFinestCascadeLength, simulation->Settings().LengthScales[c]);
        }
    }
    cdlodSettings.MaxLevel = cdlodMaxLevelOverride.value_or(
        ocean::ComputeCdlodMaxLevel(cdlodSettings.WorldExtent, cdlodSettings.BaseGridCount, simulation->Settings().Size, initFinestCascadeLength, 13u));
    auto cdlodResult = ocean::OceanCdlod::Create(*device, *resources, *library, numFramebuffers, cdlodSettings);
    if (!cdlodResult.has_value())
    {
        Fail(std::format("OceanScene: creating OceanCdlod failed: {}", cdlodResult.error()));
    }
    cdlod = std::move(*cdlodResult);

    // Ocean Environment (Sky, SH, Pyramidal mip gen)
    ocean::OceanEnvironmentSettings envSettings{};
    envSettings.Samples = msaaSamples;
    envSettings.Downsample8K = downsample8k;
    auto envResult = ocean::OceanEnvironment::Create(*device, *resources, *library, envSettings);
    if (!envResult.has_value())
    {
        Fail(std::format("OceanScene: creating OceanEnvironment failed: {}", envResult.error()));
    }
    environment = std::move(*envResult);
    (void)environment->SetPreset(skyPreset);

    // Seabed Material
    if (!settings.SeabedMaterialDirectory.empty())
    {
        seabedMaterial = ocean::LoadSeabedMaterial(settings.SeabedMaterialDirectory, settings.SeabedMaterialName, *resources, *device);
    }

    // Async Compute queue setup
    hasAsyncComputeQueue = device->GetComputeQueueCount() > 0 && device->GetComputeQueue(0) != device->GetGraphicsQueue(0);
    if (hasAsyncComputeQueue)
    {
        asyncTimeline = std::make_unique<rhi::TimelineSemaphore>(device->Handle());
        asyncTimelineValue = 0;
        asyncCommandPools.reserve(numFramebuffers);
        for (uint32_t i = 0u; i < numFramebuffers; ++i)
        {
            std::unique_ptr<rhi::CommandPool>& computePool = asyncCommandPools.emplace_back(
                std::make_unique<rhi::CommandPool>(device->Handle(), rhi::CommandPool::Type::Compute, device->GetQueueFamilyIndices().Compute));
            RhiAssert(computePool->AllocateCommandBuffers(1));
            rhi::RhiSystem::SetObjectName(VK_OBJECT_TYPE_COMMAND_POOL, reinterpret_cast<uint64_t>(computePool->Handle().As<VkCommandPool>()),
                                          std::format("Ocean_AsyncComputeCommandPool_{}", i).c_str());
        }

        rhi::GpuTimersCreateInfo asyncTimerInfo{};
        asyncTimerInfo.QueueFamily = device->GetQueueFamilyIndices().Compute;
        asyncTimerInfo.BlockCount = numFramebuffers + 1u;
        asyncTimerInfo.MaxScopesPerBlock = k_MaxGpuTimerScopes;
        asyncGpuTimers = std::make_unique<rhi::GpuTimers>(*device, asyncTimerInfo);
    }

    // FrameConstants uniform buffers (one per frame-in-flight)
    frameConstantsBuffers.reserve(numFramebuffers);
    frameConstantsBufferPointers.reserve(numFramebuffers);
    for (uint32_t i = 0u; i < numFramebuffers; ++i)
    {
        BufferCreateInfo fcInfo{};
        fcInfo.Domain = ResourceDomain::CPUToGPU;
        fcInfo.Flags = ResourceCreationFlags::PersistentlyMapped;
        fcInfo.Usage = BufferUsageBits::VertexShaderUBO | BufferUsageBits::FragmentShaderUBO;
        fcInfo.Size = sizeof(FrameConstants);
        const GraphicsResource& buffer = frameConstantsBuffers.emplace_back(WaitForResource(resources->CreateBuffer(fcInfo, {}), "a frame constants buffer"));
        rhi::RhiSystem::SetObjectName(VK_OBJECT_TYPE_BUFFER, buffer.VkHandle, std::format("Ocean_FrameConstantsBuffer_{}", i).c_str());

        std::shared_ptr<PointerMessageReply> map = resources->MapBuffer(buffer, sizeof(FrameConstants), 0u);
        if (map->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed)
        {
            Fail("OceanScene: mapping a frame constants buffer failed");
        }
        frameConstantsBufferPointers.push_back(map->GetPointer());
    }

    auto atmoRes = ocean::OceanAtmosphere::Create(*device, *resources, *library);
    if (atmoRes.has_value())
    {
        atmosphere = std::move(*atmoRes);
    }
    else
    {
        Fail(std::format("OceanScene: creating OceanAtmosphere failed: {}", atmoRes.error()));
    }

    createRenderPrograms();
    createDeferredPrograms();
    createRenderTargets();
    std::cout << std::format("OceanFft: msaa {}x, anisotropy {}x, grid {}\n", static_cast<uint32_t>(msaaSamples), anisotropy, settings.GridResolution)
        << std::flush;

    const ImGuiModuleCreateInfo imguiInfo
    {
        platformSystem->GetWindowHandle(),
        rhiSystem->GetInstance(),
        device,
        rhiSystem->GetVulkanApiVersion(),
        numFramebuffers,
        numFramebuffers,
        CommonFormats::RGBA8_UNorm,
        true,
        settings.FontDirectory
    };
    imgui = std::make_unique<ImGuiModule>(imguiInfo);

    // capture only costs anything on the frames it copies, but it needs transfer source usage on the swapchain
    if (settings.Capture.Count > 0u || settings.Commands != nullptr)
    {
        if (swapchain->SupportsTransferSource())
        {
            frameCapture = std::make_unique<capture::FrameCapture>(*resources, numFramebuffers, settings.CaptureDirectory);
        }
        else
        {
            std::cerr << "OceanFft: the swapchain images cannot be a transfer source, so capture is off\n";
        }
    }

    setup = true;
}

void OceanScene::createRenderPrograms()
{
    rhi::ShaderLibraryResult<const lodestone::ManifestIndex*> index = library->ModuleIndex(k_RenderModule);
    if (!index.has_value())
    {
        Fail(std::format("OceanScene: module {}: {}", k_RenderModule, rhi::ToString(index.error())));
    }

    // two sets per frame (surface, seabed), each with up to every sampled texture: Displacement, Derivatives,
    // Whitecaps, FoamClipmap, FoamSurface, FoamNormal. A pool that is too small is not a validation error, but the
    // sets past its end can alias
    constexpr uint32_t k_SampledImagesPerSet = 12u;
    const std::array<VkDescriptorPoolSize, 4> poolSizes
    {
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4u * numFramebuffers },
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4u * numFramebuffers },
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 2u * k_SampledImagesPerSet * numFramebuffers },
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLER, 4u * numFramebuffers },
    };
    const VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0,
        2u * numFramebuffers, static_cast<uint32_t>(poolSizes.size()), poolSizes.data() };
    RhiAssert(rhi::Result(vkCreateDescriptorPool(vkDevice, &poolInfo, nullptr, &descriptorPool)));
    rhi::RhiSystem::SetObjectName(VK_OBJECT_TYPE_DESCRIPTOR_POOL, reinterpret_cast<uint64_t>(descriptorPool), "Ocean_SceneDescriptorPool");

    const bool useWaveOps = settings.Simulation.WaveSize != 0u;
    lodestone::ManifestQueryBuilder renderQuery = (*index)->Query().Where("OCEAN_USE_WAVE_OPS", useWaveOps);
    if (useWaveOps)
    {
        renderQuery = renderQuery.Where("OCEAN_WAVE_SIZE", settings.Simulation.WaveSize);
    }

    for (auto& [program, entryPoints] : { std::pair<DrawProgram*, std::span<const std::string_view>>{ &surface, k_SurfaceEntryPoints },
                                          std::pair<DrawProgram*, std::span<const std::string_view>>{ &seabed, k_SeabedEntryPoints } })
    {
        std::expected<lodestone::VariantKey, lodestone::QueryError> variant = renderQuery.SingleFor(entryPoints);
        if (!variant.has_value())
        {
            Fail(std::format("OceanScene: no {} variant for {}", k_RenderModule, entryPoints[0]));
        }

        rhi::ShaderLibraryResult<std::unique_ptr<rhi::ManifestProgram>> created = library->CreateProgram(device->Handle(), k_RenderModule, *variant, entryPoints);
        if (!created.has_value())
        {
            Fail(std::format("OceanScene: {}: {}", entryPoints[0], rhi::ToString(created.error())));
        }

        program->Program = std::move(*created);

        const VkDescriptorSetLayout setLayout = program->Program->SetLayouts()[0].As<VkDescriptorSetLayout>();
        for (uint32_t frame = 0; frame < numFramebuffers; ++frame)
        {
            VkDescriptorSet& set = program->Sets.emplace_back(VK_NULL_HANDLE);
            const VkDescriptorSetAllocateInfo setInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, descriptorPool, 1, &setLayout };
            RhiAssert(rhi::Result(vkAllocateDescriptorSets(vkDevice, &setInfo, &set)));
            // the simulation's textures stay in General: the compute passes write them as storage images
            const BoundResources bound
            {
                { "View", { viewBuffers[frame] } },
                { "Frame", { frameConstantsBuffers[frame] } },
                { "Displacement", { simulation->Displacement(), VK_IMAGE_LAYOUT_GENERAL } },
                { "Derivatives", { simulation->Derivatives(), VK_IMAGE_LAYOUT_GENERAL } },
                { "Whitecaps", { simulation->Whitecaps(), VK_IMAGE_LAYOUT_GENERAL } },
                { "FoamClipmap", { foam->Clipmap(), VK_IMAGE_LAYOUT_GENERAL } },
                { "FoamSurface", { foamMaterial->Surface() } },
                { "FoamNormal", { foamMaterial->Normal() } },
                { "OceanSampler", { sampler } },
                { "PatchVertices", { cdlod ? cdlod->GridVertexBuffer() : GraphicsResource{} } },
                { "SelectedPatches", { cdlod ? cdlod->SelectedPatchesBuffer() : GraphicsResource{} } },
                { "SkyDomeTexture", { environment ? environment->Image() : GraphicsResource{} } },
                { "SkyDomeSampler", { environment ? environment->Sampler() : sampler } },
                { "SeabedAlbedoTexture", { seabedMaterial ? seabedMaterial->AlbedoView : GraphicsResource{} } },
                { "SeabedOrdTexture", { seabedMaterial ? seabedMaterial->OrdView : GraphicsResource{} } },
                { "SeabedNormalTexture", { seabedMaterial ? seabedMaterial->NormalView : GraphicsResource{} } },
                { "SeabedSampler", { seabedMaterial ? seabedMaterial->Sampler : sampler } },
            };

            if (std::optional<std::string> error = WriteManifestDescriptors(vkDevice, set, **index, *variant, entryPoints, bound); error.has_value())
            {
                Fail(std::format("OceanScene: {}", *error));
            }
        }
    }
}

void OceanScene::updateSurfaceDescriptors()
{
    if (!surface.Program)
    {
        return;
    }

    rhi::ShaderLibraryResult<const lodestone::ManifestIndex*> index = library->ModuleIndex(k_RenderModule);
    if (!index.has_value())
    {
        return;
    }
    const bool useWaveOps = settings.Simulation.WaveSize != 0u;
    lodestone::ManifestQueryBuilder renderQuery = (*index)->Query().Where("OCEAN_USE_WAVE_OPS", useWaveOps);
    if (useWaveOps)
    {
        renderQuery = renderQuery.Where("OCEAN_WAVE_SIZE", settings.Simulation.WaveSize);
    }
    std::expected<lodestone::VariantKey, lodestone::QueryError> variant = renderQuery.SingleFor(k_SurfaceEntryPoints);
    if (!variant.has_value())
    {
        return;
    }

    for (size_t frame = 0; frame < surface.Sets.size(); ++frame)
    {
        VkDescriptorSet set = surface.Sets[frame];
        const BoundResources bound
        {
            { "View", { viewBuffers[frame] } },
            { "Frame", { frameConstantsBuffers[frame] } },
            { "Displacement", { simulation->Displacement(), VK_IMAGE_LAYOUT_GENERAL } },
            { "Derivatives", { simulation->Derivatives(), VK_IMAGE_LAYOUT_GENERAL } },
            { "Whitecaps", { simulation->Whitecaps(), VK_IMAGE_LAYOUT_GENERAL } },
            { "FoamClipmap", { foam->Clipmap(), VK_IMAGE_LAYOUT_GENERAL } },
            { "FoamSurface", { foamMaterial->Surface() } },
            { "FoamNormal", { foamMaterial->Normal() } },
            { "OceanSampler", { sampler } },
            { "PatchVertices", { cdlod ? cdlod->GridVertexBuffer() : GraphicsResource{} } },
            { "SelectedPatches", { cdlod ? cdlod->SelectedPatchesBuffer() : GraphicsResource{} } },
            { "SkyDomeTexture", { environment ? environment->Image() : GraphicsResource{} } },
            { "SkyDomeSampler", { environment ? environment->Sampler() : sampler } },
            { "SeabedAlbedoTexture", { seabedMaterial ? seabedMaterial->AlbedoView : GraphicsResource{} } },
            { "SeabedOrdTexture", { seabedMaterial ? seabedMaterial->OrdView : GraphicsResource{} } },
            { "SeabedNormalTexture", { seabedMaterial ? seabedMaterial->NormalView : GraphicsResource{} } },
            { "SeabedSampler", { seabedMaterial ? seabedMaterial->Sampler : sampler } },
        };

        if (std::optional<std::string> error = WriteManifestDescriptors(vkDevice, set, **index, *variant, k_SurfaceEntryPoints, bound); error.has_value())
        {
            Fail(std::format("OceanScene: {}", *error));
        }
    }
}

void OceanScene::createDeferredPrograms()
{
    rhi::ShaderLibraryResult<const lodestone::ManifestIndex*> renderIndex = library->ModuleIndex(k_RenderModule);
    if (!renderIndex.has_value())
    {
        Fail("OceanScene: failed to get render module index");
    }

    const bool useWaveOps = settings.Simulation.WaveSize != 0u;
    lodestone::ManifestQueryBuilder renderQuery = (*renderIndex)->Query().Where("OCEAN_USE_WAVE_OPS", useWaveOps);
    if (useWaveOps)
    {
        renderQuery = renderQuery.Where("OCEAN_WAVE_SIZE", settings.Simulation.WaveSize);
    }

    std::expected<lodestone::VariantKey, lodestone::QueryError> gbufferVariant = renderQuery.SingleFor(k_GBufferEntryPoints);
    if (!gbufferVariant.has_value())
    {
        Fail("OceanScene: no variant for OceanSurfaceGBufferFS");
    }

    rhi::ShaderLibraryResult<std::unique_ptr<rhi::ManifestProgram>> createdGBuffer =
        library->CreateProgram(device->Handle(), k_RenderModule, *gbufferVariant, k_GBufferEntryPoints);
    if (!createdGBuffer.has_value())
    {
        Fail(std::format("OceanScene: failed to create GBuffer program: {}", rhi::ToString(createdGBuffer.error())));
    }
    surfaceGBuffer.Program = std::move(*createdGBuffer);

    // Compute programs in OceanDeferred module
    rhi::ShaderLibraryResult<const lodestone::ManifestIndex*> deferredIndex = library->ModuleIndex(k_DeferredModule);
    if (!deferredIndex.has_value())
    {
        Fail("OceanScene: failed to get OceanDeferred module index");
    }
    lodestone::ManifestQueryBuilder defQuery = (*deferredIndex)->Query();

    const std::array<std::string_view, 1> specAaEntrySpan{ k_SpecularAaEntry };
    auto specAaVariant = *defQuery.SingleFor(specAaEntrySpan);
    specularAaProgram = *library->CreateProgram(device->Handle(), k_DeferredModule, specAaVariant, specAaEntrySpan);

    const std::array<std::string_view, 1> hiZEntrySpan{ k_HiZEntry };
    auto hiZVariant = *defQuery.SingleFor(hiZEntrySpan);
    hiZProgram = *library->CreateProgram(device->Handle(), k_DeferredModule, hiZVariant, hiZEntrySpan);

    const std::array<std::string_view, 1> defLightEntrySpan{ k_DeferredLightingEntry };
    auto defLightVariant = *defQuery.SingleFor(defLightEntrySpan);
    deferredLightingProgram = *library->CreateProgram(device->Handle(), k_DeferredModule, defLightVariant, defLightEntrySpan);

    const std::array<std::string_view, 1> taaEntrySpan{ k_TaaResolveEntry };
    auto taaVariant = *defQuery.SingleFor(taaEntrySpan);
    taaResolveProgram = *library->CreateProgram(device->Handle(), k_DeferredModule, taaVariant, taaEntrySpan);

    const std::array<std::string_view, 1> autoExpEntrySpan{ k_AutoExposureEntry };
    auto autoExpVariant = *defQuery.SingleFor(autoExpEntrySpan);
    autoExposureProgram = *library->CreateProgram(device->Handle(), k_DeferredModule, autoExpVariant, autoExpEntrySpan);
}

void OceanScene::setupDeferredDescriptors()
{
    if (!surfaceGBuffer.Program)
    {
        return;
    }

    if (deferredDescriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(vkDevice, deferredDescriptorPool, nullptr);
        deferredDescriptorPool = VK_NULL_HANDLE;
    }

    const std::array<VkDescriptorPoolSize, 5> poolSizes{{
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 64u },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 32u },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 64u },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 128u },
        { VK_DESCRIPTOR_TYPE_SAMPLER, 64u },
    }};

    const VkDescriptorPoolCreateInfo poolInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0,
        128u, static_cast<uint32_t>(poolSizes.size()), poolSizes.data()
    };
    RhiAssert(rhi::Result(vkCreateDescriptorPool(vkDevice, &poolInfo, nullptr, &deferredDescriptorPool)));
    rhi::RhiSystem::SetObjectName(VK_OBJECT_TYPE_DESCRIPTOR_POOL, reinterpret_cast<uint64_t>(deferredDescriptorPool), "Ocean_DeferredDescriptorPool");

    updateDeferredDescriptors();
}

void OceanScene::updateDeferredDescriptors()
{
    if (!surfaceGBuffer.Program || deferredDescriptorPool == VK_NULL_HANDLE)
    {
        return;
    }

    rhi::ShaderLibraryResult<const lodestone::ManifestIndex*> renderIndex = library->ModuleIndex(k_RenderModule);
    const bool useWaveOps = settings.Simulation.WaveSize != 0u;
    lodestone::ManifestQueryBuilder renderQuery = (*renderIndex)->Query().Where("OCEAN_USE_WAVE_OPS", useWaveOps);
    if (useWaveOps)
    {
        renderQuery = renderQuery.Where("OCEAN_WAVE_SIZE", settings.Simulation.WaveSize);
    }
    const auto gbufferVariant = *renderQuery.SingleFor(k_GBufferEntryPoints);
    const VkDescriptorSetLayout gbufferLayout = surfaceGBuffer.Program->SetLayouts()[0].As<VkDescriptorSetLayout>();

    surfaceGBuffer.Sets.resize(numFramebuffers);
    for (uint32_t frame = 0; frame < numFramebuffers; ++frame)
    {
        if (surfaceGBuffer.Sets[frame] == VK_NULL_HANDLE)
        {
            const VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, deferredDescriptorPool, 1, &gbufferLayout };
            RhiAssert(rhi::Result(vkAllocateDescriptorSets(vkDevice, &allocInfo, &surfaceGBuffer.Sets[frame])));
        }
        const BoundResources bound{
            { "View", { viewBuffers[frame] } },
            { "Frame", { frameConstantsBuffers[frame] } },
            { "Displacement", { simulation->Displacement(), VK_IMAGE_LAYOUT_GENERAL } },
            { "Derivatives", { simulation->Derivatives(), VK_IMAGE_LAYOUT_GENERAL } },
            { "Whitecaps", { simulation->Whitecaps(), VK_IMAGE_LAYOUT_GENERAL } },
            { "FoamClipmap", { foam->Clipmap(), VK_IMAGE_LAYOUT_GENERAL } },
            { "FoamSurface", { foamMaterial->Surface() } },
            { "FoamNormal", { foamMaterial->Normal() } },
            { "OceanSampler", { sampler } },
            { "PatchVertices", { cdlod ? cdlod->GridVertexBuffer() : GraphicsResource{} } },
            { "SelectedPatches", { cdlod ? cdlod->SelectedPatchesBuffer() : GraphicsResource{} } },
            { "SkyDomeTexture", { environment ? environment->Image() : GraphicsResource{} } },
            { "SkyDomeSampler", { environment ? environment->Sampler() : sampler } },
            { "SeabedAlbedoTexture", { seabedMaterial ? seabedMaterial->AlbedoView : GraphicsResource{} } },
            { "SeabedOrdTexture", { seabedMaterial ? seabedMaterial->OrdView : GraphicsResource{} } },
            { "SeabedNormalTexture", { seabedMaterial ? seabedMaterial->NormalView : GraphicsResource{} } },
            { "SeabedSampler", { seabedMaterial ? seabedMaterial->Sampler : sampler } },
        };
        (void)WriteManifestDescriptors(vkDevice, surfaceGBuffer.Sets[frame], **renderIndex, gbufferVariant, k_GBufferEntryPoints, bound);
    }

    rhi::ShaderLibraryResult<const lodestone::ManifestIndex*> deferredIndex = library->ModuleIndex(k_DeferredModule);
    lodestone::ManifestQueryBuilder defQuery = (*deferredIndex)->Query();

    // 1. Specular AA
    const std::array<std::string_view, 1> specAaSpan{ k_SpecularAaEntry };
    const auto specAaVariant = *defQuery.SingleFor(specAaSpan);
    const VkDescriptorSetLayout specAaLayout = specularAaProgram->SetLayouts()[0].As<VkDescriptorSetLayout>();
    specularAaSets.resize(numFramebuffers, VK_NULL_HANDLE);
    for (uint32_t frame = 0; frame < numFramebuffers; ++frame)
    {
        if (specularAaSets[frame] == VK_NULL_HANDLE)
        {
            const VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, deferredDescriptorPool, 1, &specAaLayout };
            RhiAssert(rhi::Result(vkAllocateDescriptorSets(vkDevice, &allocInfo, &specularAaSets[frame])));
        }
        const BoundResources bound{
            { "Frame", { frameConstantsBuffers[frame] } },
            { "GBufferNormalSigmaIn", { gbufferNormalSigma, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } },
            { "GBufferNormalSigmaOut", { gbufferNormalSigmaFiltered, VK_IMAGE_LAYOUT_GENERAL } },
        };
        (void)WriteManifestDescriptors(vkDevice, specularAaSets[frame], **deferredIndex, specAaVariant, specAaSpan, bound);
    }

    // 2. Hi-Z
    const std::array<std::string_view, 1> hiZSpan{ k_HiZEntry };
    const auto hiZVariant = *defQuery.SingleFor(hiZSpan);
    const VkDescriptorSetLayout hiZLayout = hiZProgram->SetLayouts()[0].As<VkDescriptorSetLayout>();
    if (hiZSet == VK_NULL_HANDLE)
    {
        const VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, deferredDescriptorPool, 1, &hiZLayout };
        RhiAssert(rhi::Result(vkAllocateDescriptorSets(vkDevice, &allocInfo, &hiZSet)));
    }
    const BoundResources hiZBound{
        { "SrcDepth", { oceanDepthImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } },
        { "DstHiZMip", { hiZImage, VK_IMAGE_LAYOUT_GENERAL } },
    };
    (void)WriteManifestDescriptors(vkDevice, hiZSet, **deferredIndex, hiZVariant, hiZSpan, hiZBound);

    // 3. Deferred Lighting
    const std::array<std::string_view, 1> defLightSpan{ k_DeferredLightingEntry };
    const auto defLightVariant = *defQuery.SingleFor(defLightSpan);
    const VkDescriptorSetLayout defLightLayout = deferredLightingProgram->SetLayouts()[0].As<VkDescriptorSetLayout>();
    deferredLightingSets.resize(numFramebuffers, { VK_NULL_HANDLE, VK_NULL_HANDLE });
    for (uint32_t frame = 0; frame < numFramebuffers; ++frame)
    {
        for (uint32_t ping = 0; ping < 2; ++ping)
        {
            if (deferredLightingSets[frame][ping] == VK_NULL_HANDLE)
            {
                const VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, deferredDescriptorPool, 1, &defLightLayout };
                RhiAssert(rhi::Result(vkAllocateDescriptorSets(vkDevice, &allocInfo, &deferredLightingSets[frame][ping])));
            }
            const BoundResources bound{
                { "View", { viewBuffers[frame] } },
                { "Frame", { frameConstantsBuffers[frame] } },
                { "Atmosphere", { atmoParamsBuffer } },
                { "GBufferNormalSigmaFiltered", { gbufferNormalSigmaFiltered, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } },
                { "GBufferFoamSss", { gbufferFoamSss, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } },
                { "GBufferMotionFoam", { gbufferMotionFoam, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } },
                { "OceanDepthTexture", { oceanDepthImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } },
                { "SeabedColorTexture", { seabedColorImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } },
                { "SeabedDepthTexture", { seabedDepthImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } },
                { "SkyDomeTexture", { environment ? environment->Image() : GraphicsResource{} } },
                { "SkyDomeSampler", { environment ? environment->Sampler() : sampler } },
                { "HistoryColorTexture", { taaHistoryImages[ping], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } },
                { "LinearClampSampler", { linearClampSampler } },
                { "PointClampSampler", { pointClampSampler } },
                { "TransmittanceLut", { atmosphere ? atmosphere->TransmittanceLut() : GraphicsResource{} } },
                { "IrradianceLut", { atmosphere ? atmosphere->IrradianceLut() : GraphicsResource{} } },
                { "InscatterLut", { atmosphere ? atmosphere->InscatterLut() : GraphicsResource{} } },
                { "AtmosphereLinearSampler", { atmosphere ? atmosphere->Sampler() : linearClampSampler } },
                { "HdrLightingOutput", { hdrLightingImage, VK_IMAGE_LAYOUT_GENERAL } },
            };
            (void)WriteManifestDescriptors(vkDevice, deferredLightingSets[frame][ping], **deferredIndex, defLightVariant, defLightSpan, bound);
        }
    }

    // 4. TAA Resolve
    const std::array<std::string_view, 1> taaSpan{ k_TaaResolveEntry };
    const auto taaVariant = *defQuery.SingleFor(taaSpan);
    const VkDescriptorSetLayout taaLayout = taaResolveProgram->SetLayouts()[0].As<VkDescriptorSetLayout>();
    taaResolveSets.resize(numFramebuffers, { VK_NULL_HANDLE, VK_NULL_HANDLE });
    for (uint32_t frame = 0; frame < numFramebuffers; ++frame)
    {
        for (uint32_t ping = 0; ping < 2; ++ping)
        {
            if (taaResolveSets[frame][ping] == VK_NULL_HANDLE)
            {
                const VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, deferredDescriptorPool, 1, &taaLayout };
                RhiAssert(rhi::Result(vkAllocateDescriptorSets(vkDevice, &allocInfo, &taaResolveSets[frame][ping])));
            }
            const BoundResources bound{
                { "Frame", { frameConstantsBuffers[frame] } },
                { "GBufferMotionFoam", { gbufferMotionFoam, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } },
                { "HistoryColorTexture", { taaHistoryImages[ping], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } },
                { "LinearClampSampler", { linearClampSampler } },
                { "HdrLightingInput", { hdrLightingImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } },
                { "HdrResolvedOutput", { colorPipe ? colorPipe->SceneResource() : GraphicsResource{}, VK_IMAGE_LAYOUT_GENERAL } },
                { "NextHistoryOutput", { taaHistoryImages[1u - ping], VK_IMAGE_LAYOUT_GENERAL } },
            };
            (void)WriteManifestDescriptors(vkDevice, taaResolveSets[frame][ping], **deferredIndex, taaVariant, taaSpan, bound);
        }
    }

    // 5. Auto Exposure
    const std::array<std::string_view, 1> autoExpSpan{ k_AutoExposureEntry };
    const auto autoExpVariant = *defQuery.SingleFor(autoExpSpan);
    const VkDescriptorSetLayout autoExpLayout = autoExposureProgram->SetLayouts()[0].As<VkDescriptorSetLayout>();
    if (autoExposureSet == VK_NULL_HANDLE)
    {
        const VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, deferredDescriptorPool, 1, &autoExpLayout };
        RhiAssert(rhi::Result(vkAllocateDescriptorSets(vkDevice, &allocInfo, &autoExposureSet)));
    }
    const BoundResources autoExpBound{
        { "HdrResolvedInput", { colorPipe ? colorPipe->SceneResource() : GraphicsResource{}, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } },
        { "ExposureStateBuffer", { exposureStateBuffer } },
    };
    (void)WriteManifestDescriptors(vkDevice, autoExposureSet, **deferredIndex, autoExpVariant, autoExpSpan, autoExpBound);
}

void OceanScene::updateCdlodSettings() noexcept
{
    if (!cdlod)
    {
        return;
    }
    ocean::OceanCdlodSettings s = cdlod->Settings();
    s.LodDistanceScale = lodDistanceScale;
    s.MorphStartRatio = morphStartRatio;

    if (cdlodMaxLevelOverride.has_value())
    {
        s.MaxLevel = *cdlodMaxLevelOverride;
    }
    else
    {
        float finestCascadeLength = std::numeric_limits<float>::max();
        if (simulation)
        {
            const ocean::OceanSimulationSettings& simSettings = simulation->Settings();
            for (uint32_t c = 0u; c < ocean::k_CascadeCount; ++c)
            {
                if (cascadeEnabled[c] && simSettings.LengthScales[c] > 0.0f)
                {
                    finestCascadeLength = std::min(finestCascadeLength, simSettings.LengthScales[c]);
                }
            }
        }
        if (finestCascadeLength == std::numeric_limits<float>::max())
        {
            finestCascadeLength = 3.0f;
        }

        const uint32_t fftSize = simulation ? simulation->Settings().Size : 512u;
        s.MaxLevel = ocean::ComputeCdlodMaxLevel(s.WorldExtent, s.BaseGridCount, fftSize, finestCascadeLength, 13u);
    }
    cdlod->SetSettings(s);
}

void OceanScene::createRenderTargets()
{
    // the highest count up to k_MaxMsaaSamples that both color and depth attachments support
    if (msaaSamples == VK_SAMPLE_COUNT_1_BIT)
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(vkPhysicalDevice, &properties);
        const VkSampleCountFlags supported = properties.limits.framebufferColorSampleCounts & properties.limits.framebufferDepthSampleCounts;
        for (uint32_t samples = settings.MaxMsaaSamples; samples > 1u; samples >>= 1u)
        {
            if ((supported & samples) != 0u)
            {
                msaaSamples = static_cast<VkSampleCountFlagBits>(samples);
                break;
            }
        }
    }

    if (environment)
    {
        environment->SetSampleCount(msaaSamples);
    }

    const math::Float2 extent = swapchain->GetExtent();
    ImageCreateInfo depthInfo{};
    depthInfo.Domain = ResourceDomain::GPUOnly;
    depthInfo.Usage = ImageUsageBits::DepthRead | ImageUsageBits::DepthWrite;
    depthInfo.Type = ImageType::Type2D;
    depthInfo.Format = CommonFormats::Depth32;
    depthInfo.Width = static_cast<uint32_t>(extent.x);
    depthInfo.Height = static_cast<uint32_t>(extent.y);
    depthInfo.Depth = 1u;
    depthInfo.SampleCount = static_cast<uint32_t>(msaaSamples);
    depthInfo.ViewInfo = ImageViewCreateInfo{};
    depthInfo.ViewInfo->ViewType = ImageViewType::Type2D;
    depthInfo.ViewInfo->Range.AspectMask = ImageAspectFlags::Depth;
    depthImage = WaitForResource(resources->CreateImage(depthInfo, {}), "the depth image");

    if (msaaSamples == VK_SAMPLE_COUNT_1_BIT)
    {
        return;
    }

    // Resolves into ColorPipe::SceneTarget (RGBA16_Float)
    ImageCreateInfo colorInfo = depthInfo;
    colorInfo.Usage = ImageUsageBits::RenderTargetRead | ImageUsageBits::RenderTargetWrite;
    colorInfo.Format = CommonFormats::RGBA16_Float;
    colorInfo.Name = "Ocean_MsaaColorImage";
    colorInfo.ViewInfo->Range.AspectMask = ImageAspectFlags::Color;
    msaaColorImage = WaitForResource(resources->CreateImage(colorInfo, {}), "the multisampled color image");
    rhi::RhiSystem::SetObjectName(VK_OBJECT_TYPE_IMAGE, msaaColorImage.VkHandle, "Ocean_MsaaColorImage");
    rhi::RhiSystem::SetObjectName(VK_OBJECT_TYPE_IMAGE_VIEW, msaaColorImage.VkViewHandle, "Ocean_MsaaColorImageView");

    // Deferred Render Targets
    ImageCreateInfo hdrInfo{};
    hdrInfo.Domain = ResourceDomain::GPUOnly;
    hdrInfo.Type = ImageType::Type2D;
    hdrInfo.Format = CommonFormats::RGBA16_Float;
    hdrInfo.Width = static_cast<uint32_t>(extent.x);
    hdrInfo.Height = static_cast<uint32_t>(extent.y);
    hdrInfo.Depth = 1u;
    hdrInfo.SampleCount = 1u;
    hdrInfo.ViewInfo = ImageViewCreateInfo{};
    hdrInfo.ViewInfo->ViewType = ImageViewType::Type2D;
    hdrInfo.ViewInfo->Range.AspectMask = ImageAspectFlags::Color;

    hdrInfo.Usage = ImageUsageBits::RenderTargetRead | ImageUsageBits::RenderTargetWrite |
                    ImageUsageBits::UavComputeShader | ImageUsageBits::SrvComputeShader;
    hdrInfo.Name = "Ocean_GBufferNormalSigma";
    gbufferNormalSigma = WaitForResource(resources->CreateImage(hdrInfo, {}), "GBufferNormalSigma");

    hdrInfo.Usage = ImageUsageBits::UavComputeShader | ImageUsageBits::SrvComputeShader | ImageUsageBits::TransferDestination;
    hdrInfo.Name = "Ocean_GBufferNormalSigmaFiltered";
    gbufferNormalSigmaFiltered = WaitForResource(resources->CreateImage(hdrInfo, {}), "GBufferNormalSigmaFiltered");

    hdrInfo.Usage = ImageUsageBits::RenderTargetRead | ImageUsageBits::RenderTargetWrite | ImageUsageBits::SrvComputeShader;
    hdrInfo.Name = "Ocean_GBufferFoamSss";
    gbufferFoamSss = WaitForResource(resources->CreateImage(hdrInfo, {}), "GBufferFoamSss");

    hdrInfo.Usage = ImageUsageBits::RenderTargetRead | ImageUsageBits::RenderTargetWrite | ImageUsageBits::SrvComputeShader;
    hdrInfo.Name = "Ocean_GBufferMotionFoam";
    gbufferMotionFoam = WaitForResource(resources->CreateImage(hdrInfo, {}), "GBufferMotionFoam");

    hdrInfo.Usage = ImageUsageBits::RenderTargetRead | ImageUsageBits::RenderTargetWrite | ImageUsageBits::SrvComputeShader;
    hdrInfo.Name = "Ocean_SeabedColorImage";
    seabedColorImage = WaitForResource(resources->CreateImage(hdrInfo, {}), "SeabedColorImage");

    hdrInfo.Usage = ImageUsageBits::UavComputeShader | ImageUsageBits::SrvComputeShader | ImageUsageBits::TransferDestination;
    hdrInfo.Name = "Ocean_HdrLightingImage";
    hdrLightingImage = WaitForResource(resources->CreateImage(hdrInfo, {}), "HdrLightingImage");

    for (size_t i = 0; i < 2; ++i)
    {
        hdrInfo.Name = (i == 0) ? "Ocean_TaaHistory0" : "Ocean_TaaHistory1";
        taaHistoryImages[i] = WaitForResource(resources->CreateImage(hdrInfo, {}), "TaaHistoryImage");
    }

    ImageCreateInfo depthTargetInfo{};
    depthTargetInfo.Domain = ResourceDomain::GPUOnly;
    depthTargetInfo.Type = ImageType::Type2D;
    depthTargetInfo.Format = CommonFormats::Depth32;
    depthTargetInfo.Width = static_cast<uint32_t>(extent.x);
    depthTargetInfo.Height = static_cast<uint32_t>(extent.y);
    depthTargetInfo.Depth = 1u;
    depthTargetInfo.SampleCount = 1u;
    depthTargetInfo.Usage = ImageUsageBits::DepthRead | ImageUsageBits::DepthWrite | ImageUsageBits::SrvComputeShader;
    depthTargetInfo.ViewInfo = ImageViewCreateInfo{};
    depthTargetInfo.ViewInfo->ViewType = ImageViewType::Type2D;
    depthTargetInfo.ViewInfo->Range.AspectMask = ImageAspectFlags::Depth;

    depthTargetInfo.Name = "Ocean_SeabedDepthImage";
    seabedDepthImage = WaitForResource(resources->CreateImage(depthTargetInfo, {}), "SeabedDepthImage");

    depthTargetInfo.Name = "Ocean_OceanDepthImage";
    oceanDepthImage = WaitForResource(resources->CreateImage(depthTargetInfo, {}), "OceanDepthImage");

    ImageCreateInfo hiZInfo{};
    hiZInfo.Domain = ResourceDomain::GPUOnly;
    hiZInfo.Type = ImageType::Type2D;
    hiZInfo.Format = CommonFormats::R32_Float;
    hiZInfo.Width = (static_cast<uint32_t>(extent.x) + 1u) / 2u;
    hiZInfo.Height = (static_cast<uint32_t>(extent.y) + 1u) / 2u;
    hiZInfo.Depth = 1u;
    hiZInfo.SampleCount = 1u;
    hiZInfo.Usage = ImageUsageBits::UavComputeShader | ImageUsageBits::SrvComputeShader;
    hiZInfo.ViewInfo = ImageViewCreateInfo{};
    hiZInfo.ViewInfo->ViewType = ImageViewType::Type2D;
    hiZInfo.ViewInfo->Range.AspectMask = ImageAspectFlags::Color;
    hiZInfo.Name = "Ocean_HiZImage";
    hiZImage = WaitForResource(resources->CreateImage(hiZInfo, {}), "HiZImage");

    SamplerCreateInfo linearClampInfo{};
    linearClampInfo.MagFilter = ImageFilteringMode::Linear;
    linearClampInfo.MinFilter = ImageFilteringMode::Linear;
    linearClampInfo.MipFilter = ImageFilteringMode::Linear;
    linearClampInfo.AddressU = SamplerCreateInfo::AddressMode::ClampToEdge;
    linearClampInfo.AddressV = SamplerCreateInfo::AddressMode::ClampToEdge;
    linearClampInfo.AddressW = SamplerCreateInfo::AddressMode::ClampToEdge;
    linearClampSampler = WaitForResource(resources->CreateSampler(linearClampInfo), "linear clamp sampler");

    SamplerCreateInfo pointClampInfo = linearClampInfo;
    pointClampInfo.MagFilter = ImageFilteringMode::Nearest;
    pointClampInfo.MinFilter = ImageFilteringMode::Nearest;
    pointClampInfo.MipFilter = ImageFilteringMode::Nearest;
    pointClampSampler = WaitForResource(resources->CreateSampler(pointClampInfo), "point clamp sampler");

    BufferCreateInfo expInfo{};
    expInfo.Domain = ResourceDomain::GPUOnly;
    expInfo.Usage = BufferUsageBits::ComputeShaderUAV;
    expInfo.Size = sizeof(float) * 4;
    expInfo.Name = "Ocean_ExposureStateBuffer";
    exposureStateBuffer = WaitForResource(resources->CreateBuffer(expInfo, {}), "exposure state buffer");

    BufferCreateInfo atmoInfo{};
    atmoInfo.Domain = ResourceDomain::CPUToGPU;
    atmoInfo.Flags = ResourceCreationFlags::PersistentlyMapped;
    atmoInfo.Usage = BufferUsageBits::ComputeShaderUBO;
    atmoInfo.Size = sizeof(AtmospherePushConstants);
    atmoInfo.Name = "Ocean_AtmosphereParamsBuffer";
    atmoParamsBuffer = WaitForResource(resources->CreateBuffer(atmoInfo, {}), "atmosphere params buffer");
    auto map = resources->MapBuffer(atmoParamsBuffer, sizeof(AtmospherePushConstants), 0u);
    if (map->WaitForCompletion(k_TimeoutNs) == MessageReply::Status::Completed)
    {
        atmoParamsPointer = map->GetPointer();
    }

    setupDeferredDescriptors();
}

void OceanScene::destroyRenderTargets()
{
    for (GraphicsResource* target : { &depthImage, &msaaColorImage,
        &gbufferNormalSigma, &gbufferNormalSigmaFiltered, &gbufferFoamSss, &gbufferMotionFoam,
        &seabedColorImage, &seabedDepthImage, &oceanDepthImage, &hdrLightingImage,
        &taaHistoryImages[0], &taaHistoryImages[1], &hiZImage, &linearClampSampler, &pointClampSampler,
        &exposureStateBuffer, &atmoParamsBuffer })
    {
        if (*target)
        {
            (void)resources->DestroyResource(*target)->WaitForCompletion(k_TimeoutNs);
            *target = GraphicsResource{};
        }
    }

    if (deferredDescriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(vkDevice, deferredDescriptorPool, nullptr);
        deferredDescriptorPool = VK_NULL_HANDLE;
    }
    surfaceGBuffer.Sets.clear();
    specularAaSets.clear();
    hiZSet = VK_NULL_HANDLE;
    deferredLightingSets.clear();
    taaResolveSets.clear();
    autoExposureSet = VK_NULL_HANDLE;
    atmoParamsPointer = nullptr;
    taaHistoryInitialized = false;
}

void OceanScene::onSwapchainRecreated()
{
    // recreateSwapchain waited for the device, so nothing still uses the old targets
    destroyRenderTargets();
    createRenderTargets();
    if (colorPipe)
    {
        (void)colorPipe->OnSwapchainRecreated();
    }
    if (environment)
    {
        environment->SetSampleCount(msaaSamples);
    }
}

void OceanScene::Destroy()
{
    // main() calls this explicitly and the destructor calls it again, so the second call must be a no-op
    if (!setup)
    {
        return;
    }
    setup = false;

    device->WaitDeviceIdle();

    if (frameCapture != nullptr)
    {
        frameCapture->Flush();
        std::cout << std::format("OceanFft: wrote {} capture(s) to {}{}\n", frameCapture->WrittenCount(), frameCapture->Directory().string(),
            frameCapture->FailedCount() > 0u ? std::format(", {} failed", frameCapture->FailedCount()) : std::string{});
        frameCapture.reset();
    }

    if (seabedMaterial.has_value())
    {
        seabedMaterial->Destroy(*resources);
        seabedMaterial.reset();
    }

    environment.reset();
    cdlod.reset();
    colorPipe.reset();
    colorPipeLibrary.reset();

    for (size_t i = 0u; i < frameConstantsBuffers.size(); ++i)
    {
        if (frameConstantsBuffers[i])
        {
            (void)resources->UnmapBuffer(frameConstantsBuffers[i], 0u, sizeof(FrameConstants))->WaitForCompletion(k_TimeoutNs);
            (void)resources->DestroyResource(frameConstantsBuffers[i])->WaitForCompletion(k_TimeoutNs);
            frameConstantsBuffers[i] = GraphicsResource{};
        }
    }
    frameConstantsBuffers.clear();
    frameConstantsBufferPointers.clear();

    asyncTimeline.reset();
    asyncCommandPools.clear();
    asyncGpuTimers.reset();

    imgui.reset();
    gpuTimers.reset();
    foam.reset();
    surface = DrawProgram{};
    seabed = DrawProgram{};

    if (descriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(vkDevice, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }

    foamMaterial.reset();

    destroyRenderTargets();

    for (const GraphicsResource& buffer : viewBuffers)
    {
        (void)resources->UnmapBuffer(buffer, sizeof(OceanViewParameters), 0u)->WaitForCompletion(k_TimeoutNs);
        (void)resources->DestroyResource(buffer)->WaitForCompletion(k_TimeoutNs);
    }

    viewBuffers.clear();
    viewBufferPointers.clear();
    (void)resources->DestroyResource(sampler)->WaitForCompletion(k_TimeoutNs);
    simulation.reset();
    library.reset();
    resources.reset();
    destroyFrameSyncObjects();
    commandPools.clear();
}

void OceanScene::updateCamera()
{
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse)
    {
        return;
    }

    if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        camera.Rotate(io.MouseDelta.x, io.MouseDelta.y);
    }

    if (ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle))
    {
        camera.Pan(io.MouseDelta.x, io.MouseDelta.y, swapchain->GetExtent().y);
    }

    if (io.MouseWheel != 0.0f)
    {
        camera.Zoom(io.MouseWheel);
    }
}

void OceanScene::runCommand(const SceneCommand& command)
{
    ArcballCamera::State& state = camera.Current;
    constexpr float k_MaxPitch = 0.5f * std::numbers::pi_v<float> - 0.01f;
    const std::array<float, 6>& v = command.Values;
    switch (command.Type)
    {
    case SceneCommand::Kind::Camera:
        state.Yaw = v[0] * k_DegreesToRadians;
        state.Pitch = std::clamp(v[1] * k_DegreesToRadians, -k_MaxPitch, k_MaxPitch);
        state.Distance = std::clamp(v[2], 2.0f, ArcballCamera::MaxDistance);
        if (command.ValueCount == 6u)
        {
            state.Target = { v[3], v[4], v[5] };
        }
        break;
    case SceneCommand::Kind::Orbit:
    {
        float dYaw = v[0] * k_DegreesToRadians;
        float dPitch = v[1] * k_DegreesToRadians;
        if (std::abs(std::abs(v[0]) - 1.5708f) < 0.01f || std::abs(std::abs(v[0]) - 3.1416f) < 0.01f || std::abs(std::abs(v[0]) - std::numbers::pi_v<float>) < 0.01f)
        {
            dYaw = v[0];
        }
        if (std::abs(std::abs(v[1]) - 1.5708f) < 0.01f || std::abs(std::abs(v[1]) - 3.1416f) < 0.01f || std::abs(std::abs(v[1]) - std::numbers::pi_v<float>) < 0.01f)
        {
            dPitch = v[1];
        }
        state.Yaw += dYaw;
        state.Pitch = std::clamp(state.Pitch + dPitch, -k_MaxPitch, k_MaxPitch);
        break;
    }
    case SceneCommand::Kind::Pan:
    {
        const float height = swapchain->GetExtent().y;
        camera.Pan(v[0] * height, v[1] * height, height);
        break;
    }
    case SceneCommand::Kind::Move:
        state.Target = { state.Target[0] + v[0], state.Target[1] + v[1], state.Target[2] + v[2] };
        break;
    case SceneCommand::Kind::Zoom:
        camera.Zoom(v[0]);
        break;
    case SceneCommand::Kind::Time:
        simulationTime = v[0];
        break;
    case SceneCommand::Kind::TimeStep:
        settings.FixedTimeStep = std::max(v[0], 0.0f);
        break;
    case SceneCommand::Kind::Pause:
        paused = command.Text == "on";
        break;
    case SceneCommand::Kind::Choppiness:
        choppiness = std::max(v[0], 0.0f);
        break;
    case SceneCommand::Kind::Ui:
        showUi = command.Text == "on";
        break;
    case SceneCommand::Kind::Capture:
        captureRequest = command.Text.empty() ? std::format("capture_{:06}", frameNumber) : command.Text;
        break;
    case SceneCommand::Kind::Preset:
        setSeaStatePreset(command.Text);
        if (command.Text == "GaleFoam")
        {
            choppiness = 1.4f;
        }
        break;
    case SceneCommand::Kind::Sea:
        setSeaStateField(command.Text, v[0]);
        break;
    case SceneCommand::Kind::Cascade:
        setCascade(static_cast<uint32_t>(v[0]), command.Text == "on");
        break;
    case SceneCommand::Kind::FoamWeight:
        setFoamWeight(static_cast<uint32_t>(v[0]), v[1]);
        break;
    case SceneCommand::Kind::Foam:
        setFoamField(command.Text, v[0]);
        break;
    case SceneCommand::Kind::Shading:
        setShadingField(command.Text, v[0]);
        break;
    case SceneCommand::Kind::FoamReset:
        foamReplayPending = true;
        break;
    case SceneCommand::Kind::Rotate:
        rotateCascades = command.Text == "on";
        break;
    case SceneCommand::Kind::Print:
        printState();
        break;
    case SceneCommand::Kind::CameraReset:
        camera.Current = initialCameraState;
        break;
    case SceneCommand::Kind::LookAt:
    {
        const float ex = v[0];
        const float ey = v[1];
        const float ez = v[2];
        const float tx = v[3];
        const float ty = v[4];
        const float tz = v[5];
        const float dx = ex - tx;
        const float dy = ey - ty;
        const float dz = ez - tz;
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        state.Target = { tx, ty, tz };
        state.Distance = std::clamp(dist, 2.0f, ArcballCamera::MaxDistance);
        if (dist > 1e-4f)
        {
            state.Pitch = std::clamp(std::asin(std::clamp(dy / dist, -1.0f, 1.0f)), -k_MaxPitch, k_MaxPitch);
            state.Yaw = std::atan2(dx, dz);
        }
        break;
    }
    case SceneCommand::Kind::Deferred:
        useDeferredPath = command.Text == "on";
        break;
    case SceneCommand::Kind::SpecAa:
        useSpecularAa = command.Text == "on";
        if (command.ValueCount > 0u)
        {
            specularAaStrength = v[0];
        }
        break;
    case SceneCommand::Kind::Taa:
        useTaa = command.Text == "on";
        if (command.ValueCount > 0u)
        {
            taaBlendAlpha = v[0];
        }
        break;
    case SceneCommand::Kind::Ssr:
        if (command.ValueCount > 0u)
        {
            ssrIntensity = v[0];
        }
        break;
    case SceneCommand::Kind::AutoExposure:
        useAutoExposure = command.Text == "on";
        if (command.ValueCount > 0u)
        {
            autoExposureSpeed = v[0];
        }
        break;
    case SceneCommand::Kind::Sky:
        if (command.Text == "bruneton")
        {
            useBrunetonAtmosphere = true;
            atmosphereSettingsChanged = true;
        }
        else if (command.Text == "preetham")
        {
            useBrunetonAtmosphere = false;
            skyPreset = ocean::OceanSkyPreset::AnalyticPreetham;
        }
        else if (command.Text == "aurora") { useBrunetonAtmosphere = false; 
        {
            skyPreset = ocean::OceanSkyPreset::AuroralNight;;
        } }
        else if (command.Text == "day") { useBrunetonAtmosphere = false; 
        {
            skyPreset = ocean::OceanSkyPreset::BrightDay;;
        } }
        else if (command.Text == "night") { useBrunetonAtmosphere = false; 
        {
            skyPreset = ocean::OceanSkyPreset::StarryNight;;
        } }
        else if (command.Text == "overcast") { useBrunetonAtmosphere = false; 
        {
            skyPreset = ocean::OceanSkyPreset::OvercastEvening;;
        } }
        else if (command.Text == "twilight") { useBrunetonAtmosphere = false; 
        {
            skyPreset = ocean::OceanSkyPreset::Twilight;;
        } }
        if (environment)
        {
            device->WaitDeviceIdle();
            (void)environment->SetPreset(skyPreset);
            updateSurfaceDescriptors();
            const auto dir = environment->DominantLightDirection();
            const auto rad = environment->DominantLightRadiance();
            std::cout << "[ENV] Preset " << command.Text << " SunDir: [" << dir[0] << ", " << dir[1] << ", " << dir[2]
                      << "] Rad: [" << rad[0] << ", " << rad[1] << ", " << rad[2] << "]\n";
        }
        break;
    case SceneCommand::Kind::SkyRes:
        downsample8k = (command.Text == "8k");
        if (environment)
        {
            device->WaitDeviceIdle();
            environment->SetDownsample8K(downsample8k);
            updateSurfaceDescriptors();
        }
        break;
    case SceneCommand::Kind::SkyIntensity:
        skyIntensityScale = std::max(v[0], 0.0f);
        break;
    case SceneCommand::Kind::Tonemap:
        if (command.Text == "agx")
        {
            tonemapper = color::TonemapperKind::AgX;
        }
        else if (command.Text == "neutral")
        {
            tonemapper = color::TonemapperKind::KhronosNeutral;
        }
        else if (command.Text == "passthrough")
        {
            tonemapper = color::TonemapperKind::Passthrough;
        }
        else if (command.Text == "falsecolor")
        {
            tonemapper = color::TonemapperKind::FalseColorEv;
        }
        if (colorPipe)
        {
            (void)colorPipe->SetTonemapper(tonemapper);
        }
        break;
    case SceneCommand::Kind::Exposure:
        exposureEv = v[0];
        if (colorPipe)
        {
            colorPipe->SetExposureEv(exposureEv);
        }
        break;
    case SceneCommand::Kind::Bloom:
        if (command.Text == "on")
        {
            bloomSettings.Enabled = true;
        }
        else if (command.Text == "off")
        {
            bloomSettings.Enabled = false;
        }
        else
        {
            bloomSettings.Enabled = true;
            bloomSettings.Intensity = std::max(v[0], 0.0f);
            if (command.ValueCount > 1u)
            {
                bloomSettings.Scatter = std::clamp(v[1], 0.0f, 1.0f);
            }
        }
        if (colorPipe)
        {
            colorPipe->SetBloomSettings(bloomSettings);
        }
        break;
    case SceneCommand::Kind::Cdlod:
        useCdlod = (command.Text == "on");
        break;
    case SceneCommand::Kind::CdlodScale:
        lodDistanceScale = std::clamp(v[0], 0.2f, 10.0f);
        updateCdlodSettings();
        break;
    case SceneCommand::Kind::CdlodMorph:
        morphStartRatio = std::clamp(v[0], 0.1f, 0.95f);
        updateCdlodSettings();
        break;
    case SceneCommand::Kind::CdlodMaxLevel:
        if (v[0] <= 0.0f)
        {
            cdlodMaxLevelOverride.reset();
        }
        else
        {
            cdlodMaxLevelOverride = std::clamp(static_cast<uint32_t>(v[0]), 1u, 14u);
        }
        updateCdlodSettings();
        break;
    case SceneCommand::Kind::Wireframe:
        wireframe = (command.Text == "on");
        break;
    case SceneCommand::Kind::Brdf:
        if (command.Text == "sss")
        {
            sssStrength = std::max(v[0], 0.0f);
        }
        else if (command.Text == "leadr")
        {
            leadrSlopeVarianceScale = std::max(v[0], 0.0f);
        }
        else if (command.Text == "foam_roughness")
        {
            foamRoughnessScale = std::max(v[0], 0.0f);
        }
        else if (command.Text == "seabed_roughness")
        {
            seabedRoughnessScale = std::max(v[0], 0.0f);
        }
        break;
    case SceneCommand::Kind::AsyncCompute:
        useAsyncCompute = (command.Text == "on");
        break;
    case SceneCommand::Kind::Tab:
        if (command.Text == "timings" || command.Text == "timing")
        {
            currentTab = ocean::TelemetryTab::Timings;
        }
        else if (command.Text == "scene")
        {
            currentTab = ocean::TelemetryTab::Scene;
        }
        else if (command.Text == "surface" || command.Text == "terrain")
        {
            currentTab = ocean::TelemetryTab::Surface;
        }
        else if (command.Text == "foam")
        {
            currentTab = ocean::TelemetryTab::Foam;
        }
        else if (command.Text == "physics")
        {
            currentTab = ocean::TelemetryTab::Physics;
        }
        else if (command.Text == "sea" || command.Text == "seastate")
        {
            currentTab = ocean::TelemetryTab::SeaState;
        }
        break;
    case SceneCommand::Kind::Sync:
        device->WaitDeviceIdle();
        if (frameCapture != nullptr)
        {
            frameCapture->Flush();
        }
        if (command.Text.empty())
        {
            std::cout << std::format("READY frame={}", frameNumber) << std::endl;
        }
        else
        {
            std::cout << std::format("READY {} frame={}", command.Text, frameNumber) << std::endl;
        }
        break;
    case SceneCommand::Kind::Quit:
        quitRequested = true;
        break;
    case SceneCommand::Kind::Wait:
        break;
    }
}

void OceanScene::applySeaState()
{
    const ocean::SeaStateResult result = ocean::ApplySeaState(seaState, spectrum);
    spectrum = result.Spectrum;
    seaReadout = result.Readout;
}

void OceanScene::seaStateEdited()
{
    applySeaState();
    spectrumChanged = true;
}

void OceanScene::setSeaStatePreset(std::string_view _name)
{
    const ocean::SeaStatePreset* preset = FindSeaStatePreset(_name);
    if (preset == nullptr)
    {
        return;
    }

    seaState = preset->State;
    seabedDepth = seaState.Depth;
    seaStateEdited();
}

void OceanScene::setSeaStateField(std::string_view _name, float _value)
{
    const SeaStateField* field = FindSeaStateField(_name);
    if (field == nullptr)
    {
        return;
    }

    seaState.*(field->Member) = _value;
    if (_name == "Depth")
    {
        seabedDepth = _value;
    }
    seaStateEdited();
}

void OceanScene::setCascade(uint32_t _cascade, bool _enabled)
{
    cascadeEnabled[_cascade] = _enabled;
    updateCdlodSettings();
}

void OceanScene::setFoamWeight(uint32_t _cascade, float _weight)
{
    foamWeights[_cascade] = std::max(_weight, 0.0f);
}

void OceanScene::setFoamField(std::string_view _name, float _value)
{
    if (_name == "enable")
    {
        foamSettings.Enabled = _value > 0.5f;
    }
    else if (_name == "raise")
    {
        foamSettings.ThresholdRaise = std::clamp(_value, 0.0f, 0.5f);
    }
    else if (_name == "decay")
    {
        foamSettings.DecayTime = std::max(_value, 0.1f);
    }
    else if (_name == "gain")
    {
        foamSettings.PersistenceGain = std::max(_value, 0.0f);
    }
    else if (_name == "residual")
    {
        foamSettings.ResidualAmount = std::max(_value, 0.0f);
    }
    else if (_name == "softness")
    {
        foamSettings.ResidualSoftness = std::max(_value, 0.01f);
    }
    else if (_name == "scale")
    {
        foamSettings.TextureTileSize = std::max(_value, 0.05f);
    }
    else if (_name == "drift")
    {
        foamSettings.WindDrift = std::max(_value, 0.0f);
    }
}

void OceanScene::setShadingField(std::string_view _name, float _value)
{
    if (_name == "underwater")
    {
        underwaterFoam = std::max(_value, 0.0f);
    }
    else if (_name == "specular")
    {
        sunSpecular = std::max(_value, 0.0f);
    }
    else if (_name == "albedo")
    {
        foamAlbedoGain = std::max(_value, 0.0f);
    }
    else if (_name == "normal")
    {
        foamNormalStrength = std::max(_value, 0.0f);
    }
}

void OceanScene::printState() const
{
    const ArcballCamera::State& state = camera.Current;
    std::cout << std::format("frame {} time {:.4f} camera {:.2f} {:.2f} {:.2f} {:.2f} {:.2f} {:.2f}\n", frameNumber, simulationTime,
        state.Yaw / k_DegreesToRadians, state.Pitch / k_DegreesToRadians, state.Distance, state.Target[0], state.Target[1], state.Target[2]);

    std::cout << "sea";
    for (const SeaStateField& field : SeaStateFields())
    {
        std::cout << std::format(" {}={}", field.Name, seaState.*field.Member);
    }

    std::cout << "\ncascades enabled";
    for (uint32_t cascade = 0u; cascade < ocean::k_CascadeCount; ++cascade)
    {
        if (cascadeEnabled[cascade])
        {
            std::cout << ' ' << cascade;
        }
    }

    std::cout << "\nfoam weights";
    for (const float weight : foamWeights)
    {
        std::cout << ' ' << weight;
    }

    std::cout << std::format("\npersistent foam {} raise {} decay {} gain {} drift {} residual {} softness {} texture tile {}",
        foamSettings.Enabled ? "on" : "off", foamSettings.ThresholdRaise, foamSettings.DecayTime, foamSettings.PersistenceGain,
        foamSettings.WindDrift, foamSettings.ResidualAmount, foamSettings.ResidualSoftness, foamSettings.TextureTileSize);

    std::cout << std::format("\nshading underwater {} specular {} albedo {} normal {}", underwaterFoam, sunSpecular, foamAlbedoGain, foamNormalStrength);

    std::cout << "\nrotate cascades " << (rotateCascades ? "on" : "off");
    std::cout << std::format("\nsky {} res {} intensity {:.2f}",
        ocean::SkyPresetName(skyPreset), downsample8k ? "8k" : "16k", skyIntensityScale);
    std::string_view tonemapperName = "unknown";
    switch (tonemapper)
    {
    case color::TonemapperKind::AgX: tonemapperName = "agx"; break;
    case color::TonemapperKind::KhronosNeutral: tonemapperName = "neutral"; break;
    case color::TonemapperKind::Passthrough: tonemapperName = "passthrough"; break;
    case color::TonemapperKind::FalseColorEv: tonemapperName = "falsecolor"; break;
    default: break;
    }
    std::cout << std::format("\ntonemap {} exposure {:.2f} EV bloom {} intensity {:.4f} scatter {:.2f}",
        tonemapperName, exposureEv, bloomSettings.Enabled ? "on" : "off", bloomSettings.Intensity, bloomSettings.Scatter);
    std::cout << std::format("\ncdlod {} scale {:.2f} morph {:.2f} max_level {} ({}) wireframe {}",
        useCdlod ? "on" : "off", lodDistanceScale, morphStartRatio,
        cdlod ? cdlod->Settings().MaxLevel : 0u,
        cdlodMaxLevelOverride.has_value() ? "manual" : "auto",
        wireframe ? "on" : "off");
    std::cout << std::format("\nbrdf sss {:.2f} leadr {:.2f} foam_roughness {:.2f} seabed_roughness {:.2f}",
        sssStrength, leadrSlopeVarianceScale, foamRoughnessScale, seabedRoughnessScale);
    std::cout << std::format("\nasync compute {}", (useAsyncCompute && hasAsyncComputeQueue) ? "on" : "off");
    std::cout << '\n' << std::flush;
}

void OceanScene::drawPanel()
{
    const ImGuiFontSet& fonts = imgui->Fonts();
    const ocean::OceanSimulationSettings& simulationSettings = simulation->Settings();

    if (ocean::BeginTelemetryPanel("Ocean", fonts, headerFontMode, &showUi))
    {
        ocean::DrawTelemetryBanner(fonts, headerFontMode, simulationSettings, &showUi);
        ocean::DrawSeaReadoutCard(fonts, seaState, seaReadout);
        (void)ocean::DrawSimulationTransportBar(fonts, &paused, &timeScale, ocean::AccentTone::Amber);
        (void)ocean::DrawTelemetryTabBar(fonts, currentTab, ocean::AccentTone::Cyan);

        bool seaStateChanged = false;

        switch (currentTab)
        {
        case ocean::TelemetryTab::SeaState:
        {
            seaStateChanged |= ocean::DrawPresetGrid(fonts, seaState);

            seaStateChanged |= ocean::DrawArtistSlider(
                "##wind_force",
                "Wind force",
                seaReadout.BeaufortName,
                &seaState.WindForce,
                0.0f,
                12.0f,
                std::format("Bft {:.1f}  ({:.1f}", seaState.WindForce, seaReadout.WindSpeed),
                "m/s)",
                fonts,
                ocean::AccentTone::Cyan,
                false);

            seaStateChanged |= ocean::DrawArtistSlider(
                "##open_water",
                "Open water",
                "sheltered 5 km -> full sea",
                &seaState.Openness,
                0.0f,
                1.0f,
                std::format("{:.0f}%  ({:.0f}", seaState.Openness * 100.0f, spectrum.Fetch),
                "km)",
                fonts,
                ocean::AccentTone::Cyan,
                false);

            seaStateChanged |= ocean::DrawCenteredAngleSlider(
                "##wind_dir",
                "Wind direction",
                "travel heading",
                &seaState.WindDirection,
                -180.0f,
                180.0f,
                fonts,
                ocean::AccentTone::Cyan);

            seaStateChanged |= ocean::DrawArtistSlider(
                "##spread",
                "Directional spread",
                "aligned -> confused sea",
                &seaState.Spread,
                0.0f,
                1.0f,
                std::format("{:.2f}", seaState.Spread),
                "",
                fonts,
                ocean::AccentTone::Cyan,
                false);

            seaStateChanged |= ocean::DrawArtistSlider(
                "##regularity",
                "Wave regularity",
                "broad band -> clean groups",
                &seaState.Regularity,
                0.0f,
                3.0f,
                std::format("{:.2f}  (gamma {:.1f})", seaState.Regularity, spectrum.PeakingFactor),
                "",
                fonts,
                ocean::AccentTone::Cyan,
                false);

            seaStateChanged |= ocean::DrawArtistSlider(
                "##ripple_detail",
                "Ripple detail",
                "smooth -> fine capillary",
                &seaState.RippleDetail,
                0.0f,
                1.0f,
                std::format("{:.0f}%", seaState.RippleDetail * 100.0f),
                "",
                fonts,
                ocean::AccentTone::Cyan,
                false);

            seaStateChanged |= ocean::DrawArtistSlider(
                "##water_depth",
                "Water depth",
                "shoaling dispersion",
                &seaState.Depth,
                2.0f,
                500.0f,
                std::format("{:.0f}", seaState.Depth),
                "m",
                fonts,
                ocean::AccentTone::Cyan,
                true);

            ocean::DrawSubSectionDivider("DISTANT SWELL", fonts, headerFontMode, ocean::AccentTone::Amber);

            seaStateChanged |= ocean::DrawArtistSlider(
                "##swell_height",
                "Swell height",
                "0 turns swell off",
                &seaState.SwellHeight,
                0.0f,
                6.0f,
                std::format("{:.2f}", seaState.SwellHeight),
                "m",
                fonts,
                ocean::AccentTone::Amber,
                false);

            seaStateChanged |= ocean::DrawArtistSlider(
                "##swell_period",
                "Swell period",
                "wavelength dispersion",
                &seaState.SwellPeriod,
                5.0f,
                20.0f,
                std::format("{:.1f} s  ({:.0f}", seaState.SwellPeriod, seaReadout.SwellWavelength),
                "m)",
                fonts,
                ocean::AccentTone::Amber,
                false);

            seaStateChanged |= ocean::DrawCenteredAngleSlider(
                "##swell_dir",
                "Swell direction",
                "distant storm heading",
                &seaState.SwellDirection,
                -180.0f,
                180.0f,
                fonts,
                ocean::AccentTone::Amber);

            seaStateChanged |= ocean::DrawArtistSlider(
                "##swell_order",
                "Swell order",
                "loose -> parallel crests",
                &seaState.SwellOrder,
                0.0f,
                1.0f,
                std::format("{:.2f}", seaState.SwellOrder),
                "",
                fonts,
                ocean::AccentTone::Amber,
                false);

            ocean::DrawSubSectionDivider("AMPLITUDE ENVELOPE", fonts, headerFontMode, ocean::AccentTone::Ice);

            seaStateChanged |= ocean::DrawArtistSlider(
                "##height_scale",
                "Height scale",
                "1.0x physical reference",
                &seaState.HeightScale,
                0.0f,
                4.0f,
                std::format("{:.2f}", seaState.HeightScale),
                "x",
                fonts,
                ocean::AccentTone::Ice,
                false);
            break;
        }

        case ocean::TelemetryTab::Surface:
        {
            (void)ocean::DrawArtistSlider(
                "##choppiness",
                "Choppiness",
                "horizontal Gerstner lambda",
                &choppiness,
                0.0f,
                2.0f,
                std::format("{:.2f}", choppiness),
                "lambda",
                fonts,
                ocean::AccentTone::Cyan,
                false);

            (void)ocean::DrawArtistSlider(
                "##detail_distance",
                "Detail distance",
                "cascade fade multiplier",
                &lodScale,
                1.0f,
                60.0f,
                std::format("{:.1f}", lodScale),
                "x patch",
                fonts,
                ocean::AccentTone::Cyan,
                true);

            (void)ocean::DrawArtistSlider(
                "##time_scale",
                "Time scale",
                "simulation clock rate",
                &timeScale,
                0.0f,
                4.0f,
                std::format("{:.2f}", timeScale),
                "x",
                fonts,
                ocean::AccentTone::Cyan,
                false);

            ocean::DrawSubSectionDivider("CASCADE BANDS", fonts, headerFontMode, ocean::AccentTone::Cyan);
            ocean::DrawCascadeBandStrip(fonts, simulationSettings.LengthScales, cascadeEnabled);

            (void)ocean::DrawTelemetryToggle(
                "##rotate_cascades",
                "Rotate cascades (tiling A/B)",
                "ALIGNED",
                "ROTATED",
                &rotateCascades,
                fonts,
                ocean::AccentTone::Amber);

            ocean::DrawSubSectionDivider("CDLOD TERRAIN MESH", fonts, headerFontMode, ocean::AccentTone::Cyan);
            (void)ocean::DrawTelemetryToggle(
                "##use_cdlod",
                "CDLOD tessellation",
                "RING GRID",
                "CDLOD",
                &useCdlod,
                fonts,
                ocean::AccentTone::Cyan);

            if (useCdlod)
            {
                bool autoLevel = !cdlodMaxLevelOverride.has_value();
                if (ocean::DrawTelemetryToggle(
                        "##cdlod_auto_level",
                        "Auto level (finest cascade)",
                        "MANUAL",
                        "AUTO",
                        &autoLevel,
                        fonts,
                        ocean::AccentTone::Cyan))
                {
                    if (autoLevel)
                    {
                        cdlodMaxLevelOverride.reset();
                    }
                    else
                    {
                        cdlodMaxLevelOverride = cdlod ? cdlod->Settings().MaxLevel : 9u;
                    }
                    updateCdlodSettings();
                }

                float maxLevelFloat = static_cast<float>(cdlod ? cdlod->Settings().MaxLevel : 9u);
                if (ocean::DrawArtistSlider(
                        "##cdlod_max_level",
                        "Max level",
                        "maximum CDLOD quadtree subdivision depth (1 to 14)",
                        &maxLevelFloat,
                        1.0f,
                        14.0f,
                        std::format("{:.0f}", maxLevelFloat),
                        "",
                        fonts,
                        ocean::AccentTone::Cyan,
                        false))
                {
                    cdlodMaxLevelOverride = static_cast<uint32_t>(std::round(maxLevelFloat));
                    updateCdlodSettings();
                }

                if (ocean::DrawArtistSlider(
                        "##lod_distance_scale",
                        "Distance scale",
                        "range multiplier for LOD transitions",
                        &lodDistanceScale,
                        0.1f,
                        5.0f,
                        std::format("{:.2f}", lodDistanceScale),
                        "x",
                        fonts,
                        ocean::AccentTone::Cyan,
                        false))
                {
                    updateCdlodSettings();
                }

                if (ocean::DrawArtistSlider(
                        "##morph_start_ratio",
                        "Morph start ratio",
                        "fraction of range before continuous morphing",
                        &morphStartRatio,
                        0.1f,
                        0.95f,
                        std::format("{:.2f}", morphStartRatio),
                        "",
                        fonts,
                        ocean::AccentTone::Cyan,
                        false))
                {
                    updateCdlodSettings();
                }
            }

            (void)ocean::DrawTelemetryToggle(
                "##wireframe_mode",
                "Wireframe mode",
                "FILL",
                "LINES",
                &wireframe,
                fonts,
                ocean::AccentTone::Amber);
            break;
        }

        case ocean::TelemetryTab::Foam:
        {
            (void)ocean::DrawArtistSlider(
                "##whitecap_threshold",
                "Breaking threshold",
                "0: violent breaking only, 1: broad whitecaps",
                &seaState.WhitecapThreshold,
                0.0f,
                1.0f,
                std::format("{:.2f}", seaState.WhitecapThreshold),
                "",
                fonts,
                ocean::AccentTone::Ice,
                false);

            (void)ocean::DrawArtistSlider(
                "##foam_amount",
                "Foam amount",
                "0: none, 1: physical reference, 2+: heavy foam",
                &seaState.FoamAmount,
                0.0f,
                4.0f,
                std::format("{:.2f}", seaState.FoamAmount),
                "x",
                fonts,
                ocean::AccentTone::Ice,
                false);

            (void)ocean::DrawArtistSlider(
                "##crest_sharpness",
                "Crest sharpness",
                "0: blobs (determinant), 1: sharp crest lines (eigenvalue)",
                &seaState.CrestSharpness,
                0.0f,
                1.0f,
                std::format("{:.2f}", seaState.CrestSharpness),
                "",
                fonts,
                ocean::AccentTone::Ice,
                false);

            ocean::DrawSubSectionDivider("PER-CASCADE WEIGHTS", fonts, headerFontMode, ocean::AccentTone::Ice);
            for (uint32_t cascade = 0u; cascade < ocean::k_CascadeCount; ++cascade)
            {
                const std::string sliderId = std::format("##foam_weight_{}", cascade);
                const std::string sliderLabel = std::format("Foam weight {:.1f} m", simulationSettings.LengthScales[cascade]);
                (void)ocean::DrawArtistSlider(
                    sliderId.c_str(),
                    sliderLabel,
                    "0: cascade adds no foam, 1: as computed",
                    &foamWeights[cascade],
                    0.0f,
                    1.5f,
                    std::format("{:.2f}", foamWeights[cascade]),
                    "x",
                    fonts,
                    ocean::AccentTone::Ice,
                    false);
            }

            drawPersistentFoamControls();

            ocean::DrawSubSectionDivider("LOOKDEV", fonts, headerFontMode, ocean::AccentTone::Ice);

            (void)ocean::DrawArtistSlider(
                "##underwater_foam",
                "Underwater foam",
                "tints the water under the foam and removes its specular",
                &underwaterFoam,
                0.0f,
                2.0f,
                std::format("{:.2f}", underwaterFoam),
                "x",
                fonts,
                ocean::AccentTone::Ice,
                false);

            (void)ocean::DrawArtistSlider(
                "##sun_specular",
                "Sun specular",
                "0 turns the sun highlight off, for foam lookdev",
                &sunSpecular,
                0.0f,
                2.0f,
                std::format("{:.2f}", sunSpecular),
                "x",
                fonts,
                ocean::AccentTone::Ice,
                false);

            (void)ocean::DrawArtistSlider(
                "##foam_albedo",
                "Foam albedo",
                "gain on the brightness of the foam texture",
                &foamAlbedoGain,
                0.0f,
                4.0f,
                std::format("{:.2f}", foamAlbedoGain),
                "x",
                fonts,
                ocean::AccentTone::Ice,
                false);

            (void)ocean::DrawArtistSlider(
                "##foam_normal",
                "Foam normal",
                "how far the foam texture bends the surface normal",
                &foamNormalStrength,
                0.0f,
                2.0f,
                std::format("{:.2f}", foamNormalStrength),
                "x",
                fonts,
                ocean::AccentTone::Ice,
                false);
            break;
        }

        case ocean::TelemetryTab::Physics:
        {
            spectrumChanged |= ocean::DrawSpectrumPhysicsInspector(fonts, spectrum);
            break;
        }

        case ocean::TelemetryTab::Timings:
        {
            drawTimingsPanel();
            break;
        }

        case ocean::TelemetryTab::Scene:
        {
            (void)ocean::DrawArtistSlider(
                "##seabed_depth",
                "Seabed depth",
                "absorption floor",
                &seabedDepth,
                1.0f,
                200.0f,
                std::format("{:.1f}", seabedDepth),
                "m",
                fonts,
                ocean::AccentTone::Cyan,
                true);

            float sunElevationDeg = sunElevation / k_DegreesToRadians;
            if (ocean::DrawArtistSlider(
                    "##sun_elevation",
                    "Sun elevation",
                    "horizon -> zenith",
                    &sunElevationDeg,
                    1.0f,
                    90.0f,
                    std::format("{:.1f}", sunElevationDeg),
                    "deg",
                    fonts,
                    ocean::AccentTone::Amber,
                    false))
            {
                sunElevation = sunElevationDeg * k_DegreesToRadians;
            }

            float sunAzimuthDeg = sunAzimuth / k_DegreesToRadians;
            if (ocean::DrawCenteredAngleSlider(
                    "##sun_azimuth",
                    "Sun azimuth",
                    "solar heading",
                    &sunAzimuthDeg,
                    -180.0f,
                    180.0f,
                    fonts,
                    ocean::AccentTone::Amber))
            {
                sunAzimuth = sunAzimuthDeg * k_DegreesToRadians;
            }

            bool useMichroma = headerFontMode == ocean::HeaderFontMode::Michroma;
            if (ocean::DrawTelemetryToggle(
                    "##header_font_mode",
                    "Header typeface",
                    "BLINKER",
                    "MICHROMA",
                    &useMichroma,
                    fonts,
                    ocean::AccentTone::Ice))
            {
                headerFontMode = useMichroma ? ocean::HeaderFontMode::Michroma : ocean::HeaderFontMode::Blinker;
            }

            ocean::DrawSubSectionDivider("DEFERRED & TEMPORAL", fonts, headerFontMode, ocean::AccentTone::Cyan);
            (void)ocean::DrawTelemetryToggle("##use_deferred", "Deferred path", "OFF", "ON", &useDeferredPath, fonts, ocean::AccentTone::Cyan);
            (void)ocean::DrawTelemetryToggle("##use_bruneton", "Bruneton sky", "OFF", "ON", &useBrunetonAtmosphere, fonts, ocean::AccentTone::Amber);
            (void)ocean::DrawTelemetryToggle("##use_spec_aa", "Specular AA", "OFF", "ON", &useSpecularAa, fonts, ocean::AccentTone::Cyan);
            (void)ocean::DrawArtistSlider("##spec_aa_str", "Spec AA str", "variance filter", &specularAaStrength, 0.0f, 4.0f, std::format("{:.2f}", specularAaStrength), "", fonts, ocean::AccentTone::Cyan, false);
            (void)ocean::DrawTelemetryToggle("##use_taa", "Temporal AA", "OFF", "ON", &useTaa, fonts, ocean::AccentTone::Cyan);
            (void)ocean::DrawArtistSlider("##taa_alpha", "TAA alpha", "blend rate", &taaBlendAlpha, 0.01f, 0.5f, std::format("{:.2f}", taaBlendAlpha), "", fonts, ocean::AccentTone::Cyan, false);
            (void)ocean::DrawArtistSlider("##ssr_int", "SSR intensity", "crest reflections", &ssrIntensity, 0.0f, 2.0f, std::format("{:.2f}", ssrIntensity), "", fonts, ocean::AccentTone::Ice, false);
            (void)ocean::DrawTelemetryToggle("##auto_exp", "Auto exposure", "OFF", "ON", &useAutoExposure, fonts, ocean::AccentTone::Amber);

            ocean::DrawSubSectionDivider("SKY & ENVIRONMENT", fonts, headerFontMode, ocean::AccentTone::Amber);
            if (ocean::DrawSkyPresetGrid(fonts, skyPreset))
            {
                if (environment)
                {
                    device->WaitDeviceIdle();
                    (void)environment->SetPreset(skyPreset);
                    updateSurfaceDescriptors();
                }
            }

            if (ocean::DrawTelemetryToggle(
                    "##downsample_8k",
                    "HDRI resolution",
                    "16K FULL",
                    "8K DOWNSAMPLED",
                    &downsample8k,
                    fonts,
                    ocean::AccentTone::Amber))
            {
                if (environment)
                {
                    device->WaitDeviceIdle();
                    environment->SetDownsample8K(downsample8k);
                    updateSurfaceDescriptors();
                }
            }

            (void)ocean::DrawArtistSlider(
                "##sky_intensity",
                "Sky intensity",
                "radiance multiplier",
                &skyIntensityScale,
                0.0f,
                10.0f,
                std::format("{:.2f}", skyIntensityScale),
                "x",
                fonts,
                ocean::AccentTone::Amber,
                false);

            ocean::DrawSubSectionDivider("COLOR PIPE & TONEMAPPING", fonts, headerFontMode, ocean::AccentTone::Ice);
            if (ocean::DrawTonemapperGrid(fonts, tonemapper))
            {
                if (colorPipe)
                {
                    (void)colorPipe->SetTonemapper(tonemapper);
                }
            }

            if (ocean::DrawArtistSlider(
                    "##exposure_ev",
                    "Exposure EV",
                    "stops of exposure compensation",
                    &exposureEv,
                    -6.0f,
                    6.0f,
                    std::format("{:+.1f}", exposureEv),
                    "EV",
                    fonts,
                    ocean::AccentTone::Ice,
                    false))
            {
                if (colorPipe)
                {
                    colorPipe->SetExposureEv(exposureEv);
                }
            }

            if (ocean::DrawTelemetryToggle(
                    "##hdr_bloom",
                    "HDR bloom",
                    "OFF",
                    "ON",
                    &bloomSettings.Enabled,
                    fonts,
                    ocean::AccentTone::Ice))
            {
                if (colorPipe)
                {
                    colorPipe->SetBloomSettings(bloomSettings);
                }
            }

            if (bloomSettings.Enabled)
            {
                if (ocean::DrawArtistSlider(
                        "##bloom_intensity",
                        "Bloom intensity",
                        "energy scale",
                        &bloomSettings.Intensity,
                        0.0f,
                        0.2f,
                        std::format("{:.3f}", bloomSettings.Intensity),
                        "",
                        fonts,
                        ocean::AccentTone::Ice,
                        false))
                {
                    if (colorPipe)
                    {
                        colorPipe->SetBloomSettings(bloomSettings);
                    }
                }

                if (ocean::DrawArtistSlider(
                        "##bloom_scatter",
                        "Bloom scatter",
                        "upsample blur radius",
                        &bloomSettings.Scatter,
                        0.1f,
                        1.0f,
                        std::format("{:.2f}", bloomSettings.Scatter),
                        "",
                        fonts,
                        ocean::AccentTone::Ice,
                        false))
                {
                    if (colorPipe)
                    {
                        colorPipe->SetBloomSettings(bloomSettings);
                    }
                }
            }

            ocean::DrawSubSectionDivider("OCEAN BRDF & SHADING", fonts, headerFontMode, ocean::AccentTone::Cyan);
            (void)ocean::DrawArtistSlider(
                "##sss_strength",
                "Subsurface scatter",
                "forward scattering strength",
                &sssStrength,
                0.0f,
                2.0f,
                std::format("{:.2f}", sssStrength),
                "",
                fonts,
                ocean::AccentTone::Cyan,
                false);

            (void)ocean::DrawArtistSlider(
                "##leadr_variance",
                "LEADR slope scale",
                "geometric slope variance scale",
                &leadrSlopeVarianceScale,
                0.0f,
                3.0f,
                std::format("{:.2f}", leadrSlopeVarianceScale),
                "",
                fonts,
                ocean::AccentTone::Cyan,
                false);

            (void)ocean::DrawArtistSlider(
                "##foam_roughness",
                "Foam roughness",
                "whitecap microfacet roughness scale",
                &foamRoughnessScale,
                0.1f,
                3.0f,
                std::format("{:.2f}", foamRoughnessScale),
                "",
                fonts,
                ocean::AccentTone::Cyan,
                false);

            (void)ocean::DrawArtistSlider(
                "##seabed_roughness",
                "Seabed roughness",
                "sand microfacet roughness scale",
                &seabedRoughnessScale,
                0.1f,
                3.0f,
                std::format("{:.2f}", seabedRoughnessScale),
                "",
                fonts,
                ocean::AccentTone::Cyan,
                false);

            if (hasAsyncComputeQueue)
            {
                ocean::DrawSubSectionDivider("PIPELINE EXECUTION", fonts, headerFontMode, ocean::AccentTone::Amber);
                (void)ocean::DrawTelemetryToggle(
                    "##async_compute",
                    "Async Compute queue",
                    "GRAPHICS QUEUE",
                    "ASYNC COMPUTE",
                    &useAsyncCompute,
                    fonts,
                    ocean::AccentTone::Amber);
            }

            ImGui::PushFont(fonts.BodyItalic);
            ImGui::TextColored(ImVec4{ 0.48f, 0.60f, 0.72f, 1.0f }, "Left drag: orbit   |   Right/middle drag: pan   |   Wheel: zoom");
            ImGui::PopFont();
            break;
        }
        }

        if (seaStateChanged)
        {
            seaStateEdited();
        }

        ocean::EndTelemetryPanel();
    }
}

void OceanScene::drawPersistentFoamControls()
{
    const ImGuiFontSet& fonts = imgui->Fonts();

    (void)ocean::DrawTelemetryToggle(
        "##persistent_foam",
        "Persistent foam",
        "OFF",
        "ON",
        &foamSettings.Enabled,
        fonts,
        ocean::AccentTone::Ice);

    (void)ocean::DrawArtistSlider(
        "##foam_threshold_raise",
        "Threshold raise",
        "how far old foam raises the breaking threshold",
        &foamSettings.ThresholdRaise,
        0.0f,
        0.5f,
        std::format("{:.2f}", foamSettings.ThresholdRaise),
        "",
        fonts,
        ocean::AccentTone::Ice,
        false);

    (void)ocean::DrawArtistSlider(
        "##foam_decay",
        "Decay time",
        "seconds for foam to fall to 1/e",
        &foamSettings.DecayTime,
        0.1f,
        20.0f,
        std::format("{:.2f}", foamSettings.DecayTime),
        "s",
        fonts,
        ocean::AccentTone::Ice,
        false);

    (void)ocean::DrawArtistSlider(
        "##foam_gain",
        "Persistence gain",
        "1: stored foam is the coverage averaged over the decay time, higher saturates",
        &foamSettings.PersistenceGain,
        0.0f,
        8.0f,
        std::format("{:.2f}", foamSettings.PersistenceGain),
        "x",
        fonts,
        ocean::AccentTone::Ice,
        false);

    (void)ocean::DrawArtistSlider(
        "##foam_residual",
        "Residual amount",
        "area of old foam per unit of stored foam: 4 draws 40 % where 0.1 is stored",
        &foamSettings.ResidualAmount,
        0.0f,
        8.0f,
        std::format("{:.2f}", foamSettings.ResidualAmount),
        "x",
        fonts,
        ocean::AccentTone::Ice,
        false);

    (void)ocean::DrawArtistSlider(
        "##foam_residual_softness",
        "Dissolve softness",
        "edge width of the residual foam as it dissolves",
        &foamSettings.ResidualSoftness,
        0.01f,
        1.0f,
        std::format("{:.2f}", foamSettings.ResidualSoftness),
        "",
        fonts,
        ocean::AccentTone::Ice,
        false);

    (void)ocean::DrawArtistSlider(
        "##foam_texture_scale",
        "Foam texture scale",
        "meters per tile of the foam texture",
        &foamSettings.TextureTileSize,
        0.5f,
        20.0f,
        std::format("{:.2f}", foamSettings.TextureTileSize),
        "m",
        fonts,
        ocean::AccentTone::Ice,
        false);

    (void)ocean::DrawArtistSlider(
        "##foam_wind_drift",
        "Wind drift",
        "foam speed as a fraction of wind speed",
        &foamSettings.WindDrift,
        0.0f,
        0.2f,
        std::format("{:.3f}", foamSettings.WindDrift),
        "",
        fonts,
        ocean::AccentTone::Ice,
        false);

    if (ImGui::Button("Rebuild foam"))
    {
        foamReplayPending = true;
    }
}

void OceanScene::collectGpuTimings()
{
    // beginFrame waited on this slot's fence, which frame frameNumber - numFramebuffers signaled: it and every
    // earlier frame have finished
    const uint64_t framesInFlight = numFramebuffers;
    const uint64_t completedBefore = frameNumber >= framesInFlight ? frameNumber - framesInFlight + 1u : 0u;
    gpuTimers->Collect(completedBefore);

    // every finished frame feeds the averages, so none is skipped when two finish between updates
    for (const rhi::GpuTimingBlock& block : gpuTimers->TakeCompleted())
    {
        // a scope opened more than once in a frame counts once, with its total
        std::unordered_map<uint64_t, double> totals;

        for (const rhi::GpuTiming& timing : block.Timings)
        {
            totals[timing.Hash] += timing.Nanoseconds;
        }

        for (const auto& [hash, nanoseconds] : totals)
        {
            const auto [average, inserted] = gpuTimingAverages.try_emplace(hash, nanoseconds);

            if (!inserted)
            {
                average->second += k_GpuTimingSmoothing * (nanoseconds - average->second);
            }
        }
    }

    if (asyncGpuTimers && asyncTimeline)
    {
        uint64_t completedValue = 0;
        if (asyncTimeline->GetValue(completedValue).IsSuccess())
        {
            asyncGpuTimers->Collect(completedValue + 1u);
            for (const rhi::GpuTimingBlock& block : asyncGpuTimers->TakeCompleted())
            {
                std::unordered_map<uint64_t, double> totals;
                for (const rhi::GpuTiming& timing : block.Timings)
                {
                    totals[timing.Hash] += timing.Nanoseconds;
                }
                for (const auto& [hash, nanoseconds] : totals)
                {
                    const auto [average, inserted] = asyncGpuTimingAverages.try_emplace(hash, nanoseconds);
                    if (!inserted)
                    {
                        average->second += k_GpuTimingSmoothing * (nanoseconds - average->second);
                    }
                }
            }
        }
    }
}

void OceanScene::drawTimingsPanel()
{
    ocean::DrawGpuTimingsSection(imgui->Fonts(), headerFontMode, *gpuTimers, gpuTimingAverages,
                                 asyncGpuTimers ? asyncGpuTimers.get() : nullptr,
                                 asyncGpuTimers ? &asyncGpuTimingAverages : nullptr);
}

void OceanScene::update()
{
    imgui->BeginFrame();
    updateCamera();

    if (settings.Commands != nullptr)
    {
        for (const SceneCommand& command : settings.Commands->TakeForFrame())
        {
            runCommand(command);
        }
    }

    collectGpuTimings();

    if (showUi)
    {
        drawPanel();
    }
    else
    {
        (void)ocean::DrawRestoreMenuButton(imgui->Fonts(), &showUi);
    }

    // the foam advances by the same step as the simulation time, so a paused sea holds its foam
    double foamStep = 0.0;

    if (!paused)
    {
        const float step = settings.FixedTimeStep > 0.0f ? settings.FixedTimeStep : ImGui::GetIO().DeltaTime;
        simulationTime += step * timeScale;
        foamStep = static_cast<double>(step) * static_cast<double>(timeScale);
    }

    if (spectrumChanged)
    {
        // the init passes read the spectrum buffer, and earlier frames may still be reading their outputs
        device->WaitDeviceIdle();
        if (!simulation->SetSpectrum(spectrum))
        {
            Fail("OceanScene: writing the spectrum parameters failed");
        }
        needsInitialize = true;
        spectrumChanged = false;
        foamReplayPending = true;
    }

    simulation->SetChoppiness(choppiness);
    advanceFoam(foamStep);
}

void OceanScene::advanceFoam(double delta_seconds)
{
    foam->SetSettings(foamSettings);
    // centered on the orbit target, not the eye: zoom and orbit then leave the window where it is, so a paused
    // camera move does not scroll it and replay the foam
    const ArcballCamera::Vector3 target = camera.Current.Target;
    foam->Advance(delta_seconds, { target[0], target[2] }, spectrum.LocalWindDirection, spectrum.WindSpeed);

    FoamReplayInputs inputs{};
    inputs.Foam = foam->Settings();
    inputs.Whitecaps = { seaState.WhitecapThreshold, seaState.FoamAmount, seaState.CrestSharpness, 0.0f };
    inputs.FoamWeights = foamWeights;
    inputs.CascadeEnabled = cascadeEnabled;
    inputs.Choppiness = choppiness;
    inputs.WindowOrigins = foam->WindowOrigins();

    // a running sea fills a new foam in by itself, but a paused one would show nothing new until time passes. The
    // foam also has no history from before it was switched on
    const bool switchedOn = inputs.Foam.Enabled && !lastReplayInputs.Foam.Enabled;

    if (switchedOn || (paused && inputs != lastReplayInputs))
    {
        foamReplayPending = true;
    }

    lastReplayInputs = inputs;
}

void OceanScene::writeViewParameters()
{
    const math::Float2 extent = swapchain->GetExtent();
    const ArcballCamera::Vector3 position = camera.Position();
    const ocean::OceanSimulationSettings& simulationSettings = simulation->Settings();
    const float aspect = extent.x / std::max(extent.y, 1.0f);
    const ArcballCamera::Matrix4 baseViewProj = camera.ViewProjection(aspect);
    ArcballCamera::Matrix4 jitteredViewProj = baseViewProj;

    constexpr std::array<std::array<float, 2>, 8> k_Halton8 = {{
        { 0.0f, -0.33333334f },
        { -0.5f, 0.33333334f },
        { 0.5f, -0.7777778f },
        { -0.75f, -0.11111111f },
        { 0.25f, 0.5555556f },
        { -0.25f, -0.5555556f },
        { 0.75f, 0.11111111f },
        { -0.875f, 0.7777778f }
    }};

    if (useDeferredPath && useTaa)
    {
        const auto [jx, jy] = k_Halton8[frameNumber % 8];
        jitterOffsetNdc[0] = (jx / extent.x) * 2.0f;
        jitterOffsetNdc[1] = (jy / extent.y) * 2.0f;
        for (int col = 0; col < 4; ++col)
        {
            jitteredViewProj[0][col] += jitteredViewProj[3][col] * jitterOffsetNdc[0];
            jitteredViewProj[1][col] += jitteredViewProj[3][col] * jitterOffsetNdc[1];
        }
    }
    else
    {
        jitterOffsetNdc = { 0.0f, 0.0f };
    }

    OceanViewParameters view{};
    view.ViewProjectionRows = jitteredViewProj;
    view.CameraPositionTime = { position[0], position[1], position[2], simulationTime };

    for (uint32_t cascade = 0; cascade < ocean::k_CascadeCount; ++cascade)
    {
        view.LengthScales[cascade] = simulationSettings.LengthScales[cascade];
        view.CascadeWeights[cascade] = cascadeEnabled[cascade] ? 1.0f : 0.0f;
        view.FoamWeights[cascade] = foamWeights[cascade];
    }

    const float cosElevation = std::cos(sunElevation);
    std::array<float, 3> sunDir{ std::sin(sunAzimuth) * cosElevation, std::sin(sunElevation), std::cos(sunAzimuth) * cosElevation };
    if (environment && skyPreset != ocean::OceanSkyPreset::AnalyticPreetham)
    {
        sunDir = environment->DominantLightDirection();
    }
    view.SunDirectionSeabedDepth = { sunDir[0], sunDir[1], sunDir[2], seabedDepth };
    view.LodParameters = { lodScale, static_cast<float>(simulation->Settings().Size), rotateCascades ? 1.0f : 0.0f, 0.0f };
    view.WhitecapParameters = { seaState.WhitecapThreshold, seaState.FoamAmount, seaState.CrestSharpness, 0.0f };
    view.ShadingParameters = { underwaterFoam, sunSpecular, 0.0f, 0.0f };
    const std::array<float, 3> foamTint = foamMaterial->Tint();
    view.FoamTextureParameters = { foamAlbedoGain, foamNormalStrength, 0.0f, 0.0f };
    view.FoamTextureTint = { foamTint[0], foamTint[1], foamTint[2], 0.0f };
    foam->FillView(view.PersistentFoamParameters, view.FoamDrift, view.FoamResidualParameters);

    // beginFrame waited on this frame's fence, so the GPU is done with this frame's buffer
    std::memcpy(viewBufferPointers[currentFrame], &view, sizeof(view));

    // Update FrameConstants
    FrameConstants frameConstants{};
    if (environment)
    {
        frameConstants.SkyShCoeffs = environment->SphericalHarmonics();
        frameConstants.SunRadianceAp1 = environment->DominantLightRadiance();
    }
    frameConstants.FoamRoughnessScale = foamRoughnessScale;
    frameConstants.WaterScatterColorAp1 = { 0.02f, 0.15f, 0.2f };
    frameConstants.SssStrength = sssStrength;
    frameConstants.SeabedTintAp1 = { 0.6f, 0.5f, 0.4f };
    frameConstants.SeabedRoughnessScale = seabedRoughnessScale;
    frameConstants.UseCdlod = useCdlod ? 1u : 0u;
    frameConstants.UseSkyTexture = 1u;
    frameConstants.SkyIntensityScale = skyIntensityScale;
    frameConstants.LeadrSlopeVarianceScale = leadrSlopeVarianceScale;
    frameConstants.PrevViewProjectionRows = (prevViewProj[3][3] != 0.0f) ? prevViewProj : jitteredViewProj;
    frameConstants.JitterOffsetNdc = jitterOffsetNdc;
    frameConstants.PrevJitterOffsetNdc = prevJitterOffsetNdc;
    frameConstants.UseBrunetonAtmosphere = useBrunetonAtmosphere ? 1u : 0u;
    frameConstants.UseDeferredPath = useDeferredPath ? 1u : 0u;
    frameConstants.UseSpecularAa = useSpecularAa ? 1u : 0u;
    frameConstants.UseTaa = useTaa ? 1u : 0u;
    frameConstants.SpecularAaStrength = specularAaStrength;
    frameConstants.SsrIntensity = ssrIntensity;
    frameConstants.TaaBlendAlpha = taaBlendAlpha;
    frameConstants.AutoExposureSpeed = autoExposureSpeed;

    std::memcpy(frameConstantsBufferPointers[currentFrame], &frameConstants, sizeof(FrameConstants));

    if (atmoParamsPointer != nullptr && atmosphere)
    {
        const auto& atmo = atmosphere->Settings();
        AtmospherePushConstants params{};
        params.BottomRadiusKm = atmo.BottomRadiusKm;
        params.TopRadiusKm = atmo.TopRadiusKm;
        params.RayleighScaleHeightKm = atmo.RayleighScaleHeightKm;
        params.MieScaleHeightKm = atmo.MieScaleHeightKm;
        params.RayleighScattering = atmo.RayleighScattering;
        params.MieAnisotropy = atmo.MieAnisotropy;
        params.MieScattering = atmo.MieScattering;
        params.MieExtinction = atmo.MieExtinction.x;
        params.OzoneAbsorption = atmo.OzoneAbsorption;
        params.OzoneCenterKm = atmo.OzoneCenterKm;
        params.OzoneWidthKm = atmo.OzoneWidthKm;
        params.GroundAlbedo = atmo.GroundAlbedo;
        params.SolarIrradiance = atmo.SolarIrradiance;
        params.ScatteringOrder = atmo.ScatteringOrders;
        std::memcpy(atmoParamsPointer, &params, sizeof(params));
    }

    prevViewProj = jitteredViewProj;
    prevJitterOffsetNdc = jitterOffsetNdc;
}

uint32_t GridRingResolution(uint32_t grid_resolution, uint32_t min_resolution, uint32_t ring) noexcept
{
    // halved per ring, so neighbouring rings are a power of two apart and their edges stitch exactly
    const uint32_t shifted = ring < 31u ? (grid_resolution >> ring) : 0u;
    return std::max(shifted, min_resolution);
}

bool GridStitches(uint32_t grid_resolution, uint32_t rings, uint32_t min_resolution) noexcept
{
    // draws ring r with the resolution of ring r + 1 as its outer edge, so the pair from 0 to rings + 1 must divide evenly
    const auto stitches = [grid_resolution, min_resolution](uint32_t ring)
    {
        const uint32_t inner = GridRingResolution(grid_resolution, min_resolution, ring);
        const uint32_t outer = GridRingResolution(grid_resolution, min_resolution, ring + 1u);
        return outer > 0u && inner % outer == 0u && std::has_single_bit(inner / outer);
    };

    return grid_resolution >= min_resolution && std::ranges::all_of(std::views::iota(0u, rings + 1u), stitches);
}

uint32_t OceanScene::ringResolution(uint32_t ring) const noexcept
{
    return GridRingResolution(settings.GridResolution, settings.GridMinResolution, ring);
}

void OceanScene::recordDraw(rhi::CommandBuffer& command_buffer, const DrawProgram& program, const std::array<float, 2>& origin, float tile_size,
                            uint32_t resolution, uint32_t ring, uint32_t outer_resolution, uint32_t instance_count, bool alpha_blend) const
{
    RhiAssert(command_buffer.BindShaders(program.Program->Program()));
    SetShaderObjectDrawState(command_buffer.Handle().As<VkCommandBuffer>(), ShaderObjectDrawState{
        .DepthTest = true,
        .DepthWrite = !alpha_blend,
        .AlphaBlend = alpha_blend,
        .CullMode = VK_CULL_MODE_NONE,
        .PolygonMode = wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL,
        .Samples = msaaSamples
    });
    const rhi::DescriptorSetHandle set{ reinterpret_cast<uint64_t>(program.Sets[currentFrame]) };
    command_buffer.BindDescriptorSets(rhi::PipelineBindPoint::Graphics, program.Program->PipelineLayout(), 0, std::span{ &set, 1 });

    const OceanGridParameters grid{ origin[0], origin[1], tile_size, resolution, ring, outer_resolution };
    if (const std::optional<rhi::PushConstantRange> range = program.Program->PushConstants(); range.has_value())
    {
        command_buffer.PushConstants(program.Program->PipelineLayout(), range->StageFlags, 0u, std::as_bytes(std::span{ &grid, 1 }));
    }

    command_buffer.Draw(6u * resolution * resolution, instance_count, 0u, 0u);
}

void OceanScene::recordDeferredPipeline(
    rhi::CommandBuffer& cmd,
    rhi::GpuTimerBlock& timerBlock,
    const math::Float2& extent,
    float aspect,
    const ArcballCamera::Vector3& cameraPosition)
{
    const uint32_t width = static_cast<uint32_t>(extent.x);
    const uint32_t height = static_cast<uint32_t>(extent.y);
    constexpr rhi::ImageSubresourceRange k_ColorRange{ rhi::ImageAspect::Color, 0, 1, 0, 1 };
    constexpr rhi::ImageSubresourceRange k_DepthRange{ rhi::ImageAspect::Depth, 0, 1, 0, 1 };

    const float tileSize = settings.GridSize;
    const std::array<float, 2> centerTile{
        (std::round(cameraPosition[0] / tileSize) - 0.5f) * tileSize,
        (std::round(cameraPosition[2] / tileSize) - 0.5f) * tileSize
    };
    const float seabedSize = cdlod ? cdlod->Settings().WorldExtent : 131072.0f;
    const std::array<float, 2> seabedOrigin{ centerTile[0] - 0.5f * seabedSize, centerTile[1] - 0.5f * seabedSize };
    const uint32_t seabedResolution = 64u;

    if (!taaHistoryInitialized)
    {
        const std::array<rhi::ImageBarrier, 2> toTransferDst{{
            rhi::ImageBarrier{
                rhi::ImageHandle{ taaHistoryImages[0].VkHandle },
                rhi::PipelineStage::None, rhi::Access::None,
                rhi::PipelineStage::AllTransfer, rhi::Access::TransferWrite,
                rhi::ImageLayout::Undefined, rhi::ImageLayout::TransferDestination,
                k_ColorRange
            },
            rhi::ImageBarrier{
                rhi::ImageHandle{ taaHistoryImages[1].VkHandle },
                rhi::PipelineStage::None, rhi::Access::None,
                rhi::PipelineStage::AllTransfer, rhi::Access::TransferWrite,
                rhi::ImageLayout::Undefined, rhi::ImageLayout::TransferDestination,
                k_ColorRange
            }
        }};
        cmd.Barrier(rhi::DependencyInfo{ {}, {}, toTransferDst });

        const VkClearColorValue clearColor{ .float32 = { 0.0f, 0.0f, 0.0f, 1.0f } };
        const VkImageSubresourceRange subresourceRange{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        };
        vkCmdClearColorImage(
            cmd.Handle().As<VkCommandBuffer>(),
            reinterpret_cast<VkImage>(taaHistoryImages[0].VkHandle),
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            &clearColor,
            1,
            &subresourceRange
        );
        vkCmdClearColorImage(
            cmd.Handle().As<VkCommandBuffer>(),
            reinterpret_cast<VkImage>(taaHistoryImages[1].VkHandle),
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            &clearColor,
            1,
            &subresourceRange
        );

        const std::array<rhi::ImageBarrier, 2> toShaderReadOnly{{
            rhi::ImageBarrier{
                rhi::ImageHandle{ taaHistoryImages[0].VkHandle },
                rhi::PipelineStage::AllTransfer, rhi::Access::TransferWrite,
                rhi::PipelineStage::ComputeShader, rhi::Access::ShaderSampledRead,
                rhi::ImageLayout::TransferDestination, rhi::ImageLayout::ShaderReadOnly,
                k_ColorRange
            },
            rhi::ImageBarrier{
                rhi::ImageHandle{ taaHistoryImages[1].VkHandle },
                rhi::PipelineStage::AllTransfer, rhi::Access::TransferWrite,
                rhi::PipelineStage::ComputeShader, rhi::Access::ShaderSampledRead,
                rhi::ImageLayout::TransferDestination, rhi::ImageLayout::ShaderReadOnly,
                k_ColorRange
            }
        }};
        cmd.Barrier(rhi::DependencyInfo{ {}, {}, toShaderReadOnly });
        taaHistoryInitialized = true;
    }

    // 1. Seabed Pass
    {
        const rhi::ScopedGpuTimer timer(timerBlock, "Seabed Deferred");
        const std::array<rhi::ImageBarrier, 2> toSeabedAttachments{{
            rhi::ImageBarrier{
                rhi::ImageHandle{ seabedColorImage.VkHandle },
                rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::None,
                rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::ColorAttachmentRead | rhi::Access::ColorAttachmentWrite,
                rhi::ImageLayout::Undefined, rhi::ImageLayout::ColorAttachment,
                k_ColorRange
            },
            rhi::ImageBarrier{
                rhi::ImageHandle{ seabedDepthImage.VkHandle },
                rhi::PipelineStage::LateFragmentTests, rhi::Access::DepthStencilAttachmentWrite,
                rhi::PipelineStage::EarlyFragmentTests | rhi::PipelineStage::LateFragmentTests, rhi::Access::DepthStencilAttachmentRead | rhi::Access::DepthStencilAttachmentWrite,
                rhi::ImageLayout::Undefined, rhi::ImageLayout::DepthStencilAttachment,
                k_DepthRange
            }
        }};
        cmd.Barrier(rhi::DependencyInfo{ {}, {}, toSeabedAttachments });

        rhi::ColorAttachment colorAtt{};
        colorAtt.ImageView = rhi::ImageViewHandle{ seabedColorImage.VkViewHandle };
        colorAtt.LoadOp = rhi::LoadOp::Clear;
        colorAtt.StoreOp = rhi::StoreOp::Store;
        colorAtt.ClearValue = rhi::ClearColorValue{ 0.0f, 0.0f, 0.0f, 0.0f };

        rhi::DepthStencilAttachment depthAtt{};
        depthAtt.ImageView = rhi::ImageViewHandle{ seabedDepthImage.VkViewHandle };
        depthAtt.DepthLoadOp = rhi::LoadOp::Clear;
        depthAtt.DepthStoreOp = rhi::StoreOp::Store;
        depthAtt.ClearValue = rhi::ClearDepthStencilValue{ 0.0f, 0 };

        rhi::RenderingInfo renderingInfo{};
        renderingInfo.ColorAttachments = std::span{ &colorAtt, 1 };
        renderingInfo.DepthStencilAttachment = depthAtt;
        renderingInfo.RenderArea = rhi::Rect2D{ 0, 0, width, height };
        cmd.BeginRendering(renderingInfo);

        const rhi::Viewport viewport{ 0.0f, 0.0f, extent.x, extent.y, 0.0f, 1.0f };
        cmd.SetViewports(std::span{ &viewport, 1 });
        cmd.SetScissors(std::span{ &renderingInfo.RenderArea, 1 });

        if (!useBrunetonAtmosphere && environment)
        {
            const ArcballCamera::Matrix4 rotViewProj = camera.ViewRotationProjection(aspect);
            const ArcballCamera::Matrix4 invRotViewProj = math::FromMatrix<ArcballCamera::Matrix4>(math::ToMatrix(rotViewProj).Inverse());
            environment->RecordSkyBackground(cmd, &timerBlock, invRotViewProj, ArcballCamera::Vector3{ 0.0f, 0.0f, 0.0f }, 0.0f, VK_SAMPLE_COUNT_1_BIT);
        }

        RhiAssert(cmd.BindShaders(seabed.Program->Program()));
        SetShaderObjectDrawState(cmd.Handle().As<VkCommandBuffer>(), ShaderObjectDrawState{
            .DepthTest = true,
            .DepthWrite = true,
            .AlphaBlend = false,
            .CullMode = VK_CULL_MODE_NONE,
            .PolygonMode = wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL,
            .Samples = VK_SAMPLE_COUNT_1_BIT,
            .ColorAttachmentCount = 1u
        });
        const rhi::DescriptorSetHandle seabedSet{ reinterpret_cast<uint64_t>(seabed.Sets[currentFrame]) };
        cmd.BindDescriptorSets(rhi::PipelineBindPoint::Graphics, seabed.Program->PipelineLayout(), 0, std::span{ &seabedSet, 1 });

        const OceanGridParameters seabedGrid{ seabedOrigin[0], seabedOrigin[1], seabedSize, seabedResolution, 0u, seabedResolution };
        if (const std::optional<rhi::PushConstantRange> range = seabed.Program->PushConstants(); range.has_value())
        {
            cmd.PushConstants(seabed.Program->PipelineLayout(), range->StageFlags, 0u, std::as_bytes(std::span{ &seabedGrid, 1 }));
        }
        cmd.Draw(6u * seabedResolution * seabedResolution, 1u, 0u, 0u);

        cmd.EndRendering();
    }

    // 2. Ocean Surface G-Buffer Pass
    {
        const rhi::ScopedGpuTimer timer(timerBlock, "Surface GBuffer");
        const std::array<rhi::ImageBarrier, 4> toGBufferAttachments{{
            rhi::ImageBarrier{
                rhi::ImageHandle{ gbufferNormalSigma.VkHandle },
                rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::None,
                rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::ColorAttachmentRead | rhi::Access::ColorAttachmentWrite,
                rhi::ImageLayout::Undefined, rhi::ImageLayout::ColorAttachment,
                k_ColorRange
            },
            rhi::ImageBarrier{
                rhi::ImageHandle{ gbufferFoamSss.VkHandle },
                rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::None,
                rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::ColorAttachmentRead | rhi::Access::ColorAttachmentWrite,
                rhi::ImageLayout::Undefined, rhi::ImageLayout::ColorAttachment,
                k_ColorRange
            },
            rhi::ImageBarrier{
                rhi::ImageHandle{ gbufferMotionFoam.VkHandle },
                rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::None,
                rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::ColorAttachmentRead | rhi::Access::ColorAttachmentWrite,
                rhi::ImageLayout::Undefined, rhi::ImageLayout::ColorAttachment,
                k_ColorRange
            },
            rhi::ImageBarrier{
                rhi::ImageHandle{ oceanDepthImage.VkHandle },
                rhi::PipelineStage::LateFragmentTests, rhi::Access::DepthStencilAttachmentWrite,
                rhi::PipelineStage::EarlyFragmentTests | rhi::PipelineStage::LateFragmentTests, rhi::Access::DepthStencilAttachmentRead | rhi::Access::DepthStencilAttachmentWrite,
                rhi::ImageLayout::Undefined, rhi::ImageLayout::DepthStencilAttachment,
                k_DepthRange
            }
        }};
        cmd.Barrier(rhi::DependencyInfo{ {}, {}, toGBufferAttachments });

        std::array<rhi::ColorAttachment, 3> gbufferColors{};
        gbufferColors[0].ImageView = rhi::ImageViewHandle{ gbufferNormalSigma.VkViewHandle };
        gbufferColors[0].LoadOp = rhi::LoadOp::Clear;
        gbufferColors[0].StoreOp = rhi::StoreOp::Store;
        gbufferColors[0].ClearValue = rhi::ClearColorValue{ 0.0f, 0.0f, 0.0f, 0.0f };

        gbufferColors[1].ImageView = rhi::ImageViewHandle{ gbufferFoamSss.VkViewHandle };
        gbufferColors[1].LoadOp = rhi::LoadOp::Clear;
        gbufferColors[1].StoreOp = rhi::StoreOp::Store;
        gbufferColors[1].ClearValue = rhi::ClearColorValue{ 0.0f, 0.0f, 0.0f, 0.0f };

        gbufferColors[2].ImageView = rhi::ImageViewHandle{ gbufferMotionFoam.VkViewHandle };
        gbufferColors[2].LoadOp = rhi::LoadOp::Clear;
        gbufferColors[2].StoreOp = rhi::StoreOp::Store;
        gbufferColors[2].ClearValue = rhi::ClearColorValue{ 0.0f, 0.0f, 0.0f, 0.0f };

        rhi::DepthStencilAttachment depthAtt{};
        depthAtt.ImageView = rhi::ImageViewHandle{ oceanDepthImage.VkViewHandle };
        depthAtt.DepthLoadOp = rhi::LoadOp::Clear;
        depthAtt.DepthStoreOp = rhi::StoreOp::Store;
        depthAtt.ClearValue = rhi::ClearDepthStencilValue{ 0.0f, 0 };

        rhi::RenderingInfo renderingInfo{};
        renderingInfo.ColorAttachments = gbufferColors;
        renderingInfo.DepthStencilAttachment = depthAtt;
        renderingInfo.RenderArea = rhi::Rect2D{ 0, 0, width, height };
        cmd.BeginRendering(renderingInfo);

        const rhi::Viewport viewport{ 0.0f, 0.0f, extent.x, extent.y, 0.0f, 1.0f };
        cmd.SetViewports(std::span{ &viewport, 1 });
        cmd.SetScissors(std::span{ &renderingInfo.RenderArea, 1 });

        RhiAssert(cmd.BindShaders(surfaceGBuffer.Program->Program()));
        SetShaderObjectDrawState(cmd.Handle().As<VkCommandBuffer>(), ShaderObjectDrawState{
            .DepthTest = true,
            .DepthWrite = true,
            .AlphaBlend = false,
            .CullMode = VK_CULL_MODE_NONE,
            .PolygonMode = wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL,
            .Samples = VK_SAMPLE_COUNT_1_BIT,
            .ColorAttachmentCount = 3u
        });

        const rhi::DescriptorSetHandle gbufferSet{ reinterpret_cast<uint64_t>(surfaceGBuffer.Sets[currentFrame]) };
        cmd.BindDescriptorSets(rhi::PipelineBindPoint::Graphics, surfaceGBuffer.Program->PipelineLayout(), 0, std::span{ &gbufferSet, 1 });

        if (useCdlod && cdlod)
        {
            const OceanGridParameters grid{ centerTile[0], centerTile[1], tileSize, 64u, 0u, 64u };
            if (const std::optional<rhi::PushConstantRange> range = surfaceGBuffer.Program->PushConstants(); range.has_value())
            {
                cmd.PushConstants(surfaceGBuffer.Program->PipelineLayout(), range->StageFlags, 0u, std::as_bytes(std::span{ &grid, 1 }));
            }
            cmd.BindIndexBuffer(rhi::BufferHandle{ cdlod->GridIndexBuffer().VkHandle }, 0u, rhi::IndexType::Uint32);
            vkCmdDrawIndexedIndirect(
                cmd.Handle().As<VkCommandBuffer>(),
                reinterpret_cast<VkBuffer>(cdlod->IndirectDrawBuffer().VkHandle),
                0u,
                1u,
                sizeof(VkDrawIndexedIndirectCommand));
        }
        else
        {
            for (uint32_t ring = 0; ring <= settings.GridRings; ++ring)
            {
                const uint32_t tiles = ring == 0u ? 1u : 8u * ring;
                const OceanGridParameters ringGrid{ centerTile[0], centerTile[1], tileSize, ringResolution(ring), ring, ringResolution(ring + 1u) };
                if (const std::optional<rhi::PushConstantRange> range = surfaceGBuffer.Program->PushConstants(); range.has_value())
                {
                    cmd.PushConstants(surfaceGBuffer.Program->PipelineLayout(), range->StageFlags, 0u, std::as_bytes(std::span{ &ringGrid, 1 }));
                }
                cmd.Draw(6u * ringResolution(ring) * ringResolution(ring), tiles, 0u, 0u);
            }
        }

        cmd.EndRendering();
    }

    // 3. Specular AA Compute Pass
    {
        const rhi::ScopedGpuTimer timer(timerBlock, "Specular AA");
        const std::array<rhi::ImageBarrier, 2> specAaBarriers{{
            rhi::ImageBarrier{
                rhi::ImageHandle{ gbufferNormalSigma.VkHandle },
                rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::ColorAttachmentWrite,
                rhi::PipelineStage::ComputeShader, rhi::Access::ShaderSampledRead,
                rhi::ImageLayout::ColorAttachment, rhi::ImageLayout::ShaderReadOnly,
                k_ColorRange
            },
            rhi::ImageBarrier{
                rhi::ImageHandle{ gbufferNormalSigmaFiltered.VkHandle },
                rhi::PipelineStage::ComputeShader, rhi::Access::None,
                rhi::PipelineStage::ComputeShader, rhi::Access::ShaderStorageWrite,
                rhi::ImageLayout::Undefined, rhi::ImageLayout::General,
                k_ColorRange
            }
        }};
        cmd.Barrier(rhi::DependencyInfo{ {}, {}, specAaBarriers });

        RhiAssert(cmd.BindShaders(specularAaProgram->Program()));
        const rhi::DescriptorSetHandle specSet{ reinterpret_cast<uint64_t>(specularAaSets[currentFrame]) };
        cmd.BindDescriptorSets(rhi::PipelineBindPoint::Compute, specularAaProgram->PipelineLayout(), 0, std::span{ &specSet, 1 });

        const SpecularAaPushConstants push{
            .Dimensions = { width, height },
            .SpecularAaStrength = specularAaStrength,
            .Padding = 0.0f
        };
        PushProgramConstants(cmd, *specularAaProgram, push);
        cmd.Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1u);
    }

    // 4. Hi-Z Downsample Pass
    {
        const rhi::ScopedGpuTimer timer(timerBlock, "Hi-Z Downsample");
        const std::array<rhi::ImageBarrier, 2> hiZBarriers{{
            rhi::ImageBarrier{
                rhi::ImageHandle{ oceanDepthImage.VkHandle },
                rhi::PipelineStage::LateFragmentTests, rhi::Access::DepthStencilAttachmentWrite,
                rhi::PipelineStage::ComputeShader, rhi::Access::ShaderSampledRead,
                rhi::ImageLayout::DepthStencilAttachment, rhi::ImageLayout::ShaderReadOnly,
                k_DepthRange
            },
            rhi::ImageBarrier{
                rhi::ImageHandle{ hiZImage.VkHandle },
                rhi::PipelineStage::ComputeShader, rhi::Access::None,
                rhi::PipelineStage::ComputeShader, rhi::Access::ShaderStorageWrite,
                rhi::ImageLayout::Undefined, rhi::ImageLayout::General,
                k_ColorRange
            }
        }};
        cmd.Barrier(rhi::DependencyInfo{ {}, {}, hiZBarriers });

        RhiAssert(cmd.BindShaders(hiZProgram->Program()));
        const rhi::DescriptorSetHandle setHandle{ reinterpret_cast<uint64_t>(hiZSet) };
        cmd.BindDescriptorSets(rhi::PipelineBindPoint::Compute, hiZProgram->PipelineLayout(), 0, std::span{ &setHandle, 1 });

        const HiZPushConstants push{
            .DstDimensions = { (width + 1u) / 2u, (height + 1u) / 2u },
            .SrcDimensions = { width, height }
        };
        PushProgramConstants(cmd, *hiZProgram, push);
        cmd.Dispatch(((width + 1u) / 2u + 7u) / 8u, ((height + 1u) / 2u + 7u) / 8u, 1u);
    }

    // 5. Deferred Lighting Compute Pass
    {
        const rhi::ScopedGpuTimer timer(timerBlock, "Deferred Lighting");
        const std::array<rhi::ImageBarrier, 6> lightBarriers{{
            rhi::ImageBarrier{
                rhi::ImageHandle{ gbufferNormalSigmaFiltered.VkHandle },
                rhi::PipelineStage::ComputeShader, rhi::Access::ShaderStorageWrite,
                rhi::PipelineStage::ComputeShader, rhi::Access::ShaderSampledRead,
                rhi::ImageLayout::General, rhi::ImageLayout::ShaderReadOnly,
                k_ColorRange
            },
            rhi::ImageBarrier{
                rhi::ImageHandle{ gbufferFoamSss.VkHandle },
                rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::ColorAttachmentWrite,
                rhi::PipelineStage::ComputeShader, rhi::Access::ShaderSampledRead,
                rhi::ImageLayout::ColorAttachment, rhi::ImageLayout::ShaderReadOnly,
                k_ColorRange
            },
            rhi::ImageBarrier{
                rhi::ImageHandle{ gbufferMotionFoam.VkHandle },
                rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::ColorAttachmentWrite,
                rhi::PipelineStage::ComputeShader, rhi::Access::ShaderSampledRead,
                rhi::ImageLayout::ColorAttachment, rhi::ImageLayout::ShaderReadOnly,
                k_ColorRange
            },
            rhi::ImageBarrier{
                rhi::ImageHandle{ seabedColorImage.VkHandle },
                rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::ColorAttachmentWrite,
                rhi::PipelineStage::ComputeShader, rhi::Access::ShaderSampledRead,
                rhi::ImageLayout::ColorAttachment, rhi::ImageLayout::ShaderReadOnly,
                k_ColorRange
            },
            rhi::ImageBarrier{
                rhi::ImageHandle{ seabedDepthImage.VkHandle },
                rhi::PipelineStage::LateFragmentTests, rhi::Access::DepthStencilAttachmentWrite,
                rhi::PipelineStage::ComputeShader, rhi::Access::ShaderSampledRead,
                rhi::ImageLayout::DepthStencilAttachment, rhi::ImageLayout::ShaderReadOnly,
                k_DepthRange
            },
            rhi::ImageBarrier{
                rhi::ImageHandle{ hdrLightingImage.VkHandle },
                rhi::PipelineStage::ComputeShader, rhi::Access::None,
                rhi::PipelineStage::ComputeShader, rhi::Access::ShaderStorageWrite,
                rhi::ImageLayout::Undefined, rhi::ImageLayout::General,
                k_ColorRange
            }
        }};
        cmd.Barrier(rhi::DependencyInfo{ {}, {}, lightBarriers });

        RhiAssert(cmd.BindShaders(deferredLightingProgram->Program()));
        const rhi::DescriptorSetHandle setHandle{ reinterpret_cast<uint64_t>(deferredLightingSets[currentFrame][historyPingPong]) };
        cmd.BindDescriptorSets(rhi::PipelineBindPoint::Compute, deferredLightingProgram->PipelineLayout(), 0, std::span{ &setHandle, 1 });

        const ArcballCamera::Matrix4 viewProj = camera.ViewProjection(aspect);
        DeferredLightingPushConstants push{};
        push.InvViewProjection = math::FromMatrix<ArcballCamera::Matrix4>(math::ToMatrix(viewProj).Inverse());
        push.Dimensions = { width, height };
        push.InvDimensions = { 1.0f / extent.x, 1.0f / extent.y };

        PushProgramConstants(cmd, *deferredLightingProgram, push);
        cmd.Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1u);
    }

    // 6. TAA Resolve Compute Pass
    const uint32_t nextPingPong = 1u - historyPingPong;
    {
        const rhi::ScopedGpuTimer timer(timerBlock, "TAA Resolve");
        const color::ColorTargetView sceneTarget = colorPipe->SceneTarget();
        const std::array<rhi::ImageBarrier, 3> taaBarriers{{
            rhi::ImageBarrier{
                rhi::ImageHandle{ hdrLightingImage.VkHandle },
                rhi::PipelineStage::ComputeShader, rhi::Access::ShaderStorageWrite,
                rhi::PipelineStage::ComputeShader, rhi::Access::ShaderSampledRead,
                rhi::ImageLayout::General, rhi::ImageLayout::ShaderReadOnly,
                k_ColorRange
            },
            rhi::ImageBarrier{
                rhi::ImageHandle{ taaHistoryImages[nextPingPong].VkHandle },
                rhi::PipelineStage::ComputeShader, rhi::Access::None,
                rhi::PipelineStage::ComputeShader, rhi::Access::ShaderStorageWrite,
                rhi::ImageLayout::Undefined, rhi::ImageLayout::General,
                k_ColorRange
            },
            rhi::ImageBarrier{
                sceneTarget.Image,
                rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::None,
                rhi::PipelineStage::ComputeShader, rhi::Access::ShaderStorageWrite,
                rhi::ImageLayout::Undefined, rhi::ImageLayout::General,
                k_ColorRange
            }
        }};
        cmd.Barrier(rhi::DependencyInfo{ {}, {}, taaBarriers });

        RhiAssert(cmd.BindShaders(taaResolveProgram->Program()));
        const rhi::DescriptorSetHandle setHandle{ reinterpret_cast<uint64_t>(taaResolveSets[currentFrame][historyPingPong]) };
        cmd.BindDescriptorSets(rhi::PipelineBindPoint::Compute, taaResolveProgram->PipelineLayout(), 0, std::span{ &setHandle, 1 });

        const TaaPushConstants push{
            .Dimensions = { width, height },
            .InvDimensions = { 1.0f / extent.x, 1.0f / extent.y }
        };
        PushProgramConstants(cmd, *taaResolveProgram, push);
        cmd.Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1u);

        const rhi::ImageBarrier histBarrier{
            rhi::ImageHandle{ taaHistoryImages[nextPingPong].VkHandle },
            rhi::PipelineStage::ComputeShader, rhi::Access::ShaderStorageWrite,
            rhi::PipelineStage::ComputeShader, rhi::Access::ShaderSampledRead,
            rhi::ImageLayout::General, rhi::ImageLayout::ShaderReadOnly,
            k_ColorRange
        };
        cmd.Barrier(rhi::DependencyInfo{ {}, {}, std::span{ &histBarrier, 1 } });
    }
    historyPingPong = nextPingPong;

    // 7. Auto Exposure Compute Pass
    const color::ColorTargetView sceneTarget = colorPipe->SceneTarget();
    if (useAutoExposure)
    {
        const rhi::ScopedGpuTimer timer(timerBlock, "Auto Exposure");
        const rhi::ImageBarrier sceneToSampled{
            sceneTarget.Image,
            rhi::PipelineStage::ComputeShader, rhi::Access::ShaderStorageWrite,
            rhi::PipelineStage::ComputeShader, rhi::Access::ShaderSampledRead,
            rhi::ImageLayout::General, rhi::ImageLayout::ShaderReadOnly,
            k_ColorRange
        };
        cmd.Barrier(rhi::DependencyInfo{ {}, {}, std::span{ &sceneToSampled, 1 } });

        RhiAssert(cmd.BindShaders(autoExposureProgram->Program()));
        const rhi::DescriptorSetHandle setHandle{ reinterpret_cast<uint64_t>(autoExposureSet) };
        cmd.BindDescriptorSets(rhi::PipelineBindPoint::Compute, autoExposureProgram->PipelineLayout(), 0, std::span{ &setHandle, 1 });

        const AutoExposurePushConstants push{
            .Dimensions = { width, height },
            .DeltaTime = 0.016f,
            .AdaptationSpeed = autoExposureSpeed,
            .MinLogLum = -8.0f,
            .MaxLogLum = 4.0f,
            .Padding = { 0.0f, 0.0f }
        };
        PushProgramConstants(cmd, *autoExposureProgram, push);
        cmd.Dispatch(1u, 1u, 1u);

        const rhi::ImageBarrier sceneToColorPipe{
            sceneTarget.Image,
            rhi::PipelineStage::ComputeShader, rhi::Access::ShaderSampledRead,
            rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::ColorAttachmentWrite,
            rhi::ImageLayout::ShaderReadOnly, rhi::ImageLayout::ColorAttachment,
            k_ColorRange
        };
        cmd.Barrier(rhi::DependencyInfo{ {}, {}, std::span{ &sceneToColorPipe, 1 } });
    }
    else
    {
        const rhi::ImageBarrier sceneToColorPipe{
            sceneTarget.Image,
            rhi::PipelineStage::ComputeShader, rhi::Access::ShaderStorageWrite,
            rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::ColorAttachmentWrite,
            rhi::ImageLayout::General, rhi::ImageLayout::ColorAttachment,
            k_ColorRange
        };
        cmd.Barrier(rhi::DependencyInfo{ {}, {}, std::span{ &sceneToColorPipe, 1 } });
    }
}

void OceanScene::recordCommands()
{
    writeViewParameters();

    const math::Float2 extent = swapchain->GetExtent();
    const ArcballCamera::Vector3 cameraPosition = camera.Position();
    const float aspect = extent.x / std::max(extent.y, 1.0f);
    const ArcballCamera::Matrix4 viewProj = camera.ViewProjection(aspect);
    const ocean::OceanCdlodCameraState cdlodCamera = ComputeCdlodCamera(viewProj, cameraPosition);

    const bool replayFoam = foamReplayPending && foam->Settings().Enabled;
    foamReplayPending = false;

    // --- Async Compute or Graphics Queue Compute ---
    if (useAsyncCompute && hasAsyncComputeQueue)
    {
        rhi::CommandPool& computePool = *asyncCommandPools[currentFrame];
        RhiAssert(computePool.Reset());
        rhi::CommandBuffer computeCmd(computePool.GetCommandBuffer(0));
        RhiAssert(computeCmd.Begin());
        rhi::GpuTimerBlock computeTimerBlock(*asyncGpuTimers, computeCmd, frameNumber);

        if (needsInitialize)
        {
            const rhi::ScopedGpuTimer timer(computeTimerBlock, "Ocean initialize");
            simulation->RecordInitialize(computeCmd);
            needsInitialize = false;
        }

        if (replayFoam)
        {
            const rhi::ScopedGpuTimer timer(computeTimerBlock, "Foam replay");
            const ArcballCamera::Vector3 target = camera.Current.Target;
            ocean::RecordFoamReplay(computeCmd, *simulation, *foam, currentFrame, static_cast<double>(simulationTime),
                { target[0], target[2] }, spectrum.LocalWindDirection, spectrum.WindSpeed);
        }
        else
        {
            {
                const rhi::ScopedGpuTimer timer(computeTimerBlock, "Ocean simulation");
                simulation->RecordTick(computeCmd, computeTimerBlock, simulationTime);
            }

            {
                const rhi::ScopedGpuTimer timer(computeTimerBlock, "Foam");
                foam->RecordStep(computeCmd, currentFrame, &computeTimerBlock);
            }
        }
        RhiAssert(computeCmd.End());

        const uint64_t signalVal = ++asyncTimelineValue;
        const rhi::SemaphoreSubmitInfo signalInfo{ asyncTimeline->Handle(), signalVal, rhi::PipelineStage::ComputeShader };
        const rhi::CommandBufferHandle computeCmdHandle = computePool.GetCommandBuffer(0);
        const rhi::SubmitInfo computeSubmit{ {}, std::span{ &computeCmdHandle, 1 }, std::span{ &signalInfo, 1 } };
        RhiAssert(rhi::QueueSubmit(device->GetComputeQueue(0), computeSubmit));
    }

    // --- Graphics Queue Recording ---
    rhi::CommandPool& pool = *commandPools[currentFrame];
    RhiAssert(pool.Reset());
    rhi::CommandBuffer cmd(pool.GetCommandBuffer(0));
    RhiAssert(cmd.Begin());
    rhi::GpuTimerBlock timerBlock(*gpuTimers, cmd, frameNumber);

    if (!useAsyncCompute || !hasAsyncComputeQueue)
    {
        if (needsInitialize)
        {
            const rhi::ScopedGpuTimer timer(timerBlock, "Ocean initialize");
            simulation->RecordInitialize(cmd);
            needsInitialize = false;
        }

        const rhi::GlobalBarrier drawsToCompute
        {
            rhi::PipelineStage::VertexShader | rhi::PipelineStage::FragmentShader | ocean::OceanSimulation::k_TickStages,
            ocean::OceanSimulation::k_TickWriteAccess,
            ocean::OceanSimulation::k_TickStages,
            ocean::OceanSimulation::k_TickWriteAccess
        };
        cmd.Barrier(rhi::DependencyInfo{ std::span{ &drawsToCompute, 1 }, {}, {} });

        if (replayFoam)
        {
            const rhi::ScopedGpuTimer timer(timerBlock, "Foam replay");
            const ArcballCamera::Vector3 target = camera.Current.Target;
            ocean::RecordFoamReplay(cmd, *simulation, *foam, currentFrame, static_cast<double>(simulationTime),
                { target[0], target[2] }, spectrum.LocalWindDirection, spectrum.WindSpeed);
        }
        else
        {
            {
                const rhi::ScopedGpuTimer timer(timerBlock, "Ocean simulation");
                simulation->RecordTick(cmd, timerBlock, simulationTime);
            }

            {
                const rhi::ScopedGpuTimer timer(timerBlock, "Foam");
                foam->RecordStep(cmd, currentFrame, &timerBlock);
            }
        }
    }

    // Environment mip generation and CDLOD quadtree update (compute passes before render pass)
    if (atmosphere && (!atmosphere->IsPrecomputed() || atmosphereSettingsChanged))
    {
        atmosphere->RecordPrecompute(cmd, &timerBlock);
        atmosphereSettingsChanged = false;
    }

    if (environment)
    {
        environment->RecordMipGen(cmd, &timerBlock);
    }

    if (cdlod && useCdlod)
    {
        cdlod->RecordUpdate(cmd, currentFrame, cdlodCamera, &timerBlock);
    }

    const rhi::GlobalBarrier computeToDraws
    {
        ocean::OceanSimulation::k_TickStages | rhi::PipelineStage::ComputeShader,
        ocean::OceanSimulation::k_TickWriteAccess | rhi::Access::ShaderStorageWrite,
        rhi::PipelineStage::VertexShader | rhi::PipelineStage::FragmentShader | rhi::PipelineStage::DrawIndirect,
        rhi::Access::ShaderSampledRead | rhi::Access::ShaderStorageRead | rhi::Access::IndirectCommandRead
    };
    cmd.Barrier(rhi::DependencyInfo{ std::span{ &computeToDraws, 1 }, {}, {} });

    std::optional<rhi::ScopedGpuTimer> passTimer;
    passTimer.emplace(timerBlock, "Ocean scene pass");

    const rhi::ImageHandle image{ reinterpret_cast<uint64_t>(swapchain->ImageHandle(currentAcquiredImage)) };
    const rhi::ImageViewHandle swapchainView{ reinterpret_cast<uint64_t>(swapchain->ImageViewHandle(currentAcquiredImage)) };
    constexpr rhi::ImageSubresourceRange k_ColorRange{ rhi::ImageAspect::Color, 0, 1, 0, 1 };
    constexpr rhi::ImageSubresourceRange k_DepthRange{ rhi::ImageAspect::Depth, 0, 1, 0, 1 };
    const bool multisampled = static_cast<bool>(msaaColorImage);

    const rhi::ImageBarrier swapchainInitBarrier{
        image,
        rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::None,
        rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::ColorAttachmentRead | rhi::Access::ColorAttachmentWrite,
        rhi::ImageLayout::Undefined, rhi::ImageLayout::ColorAttachment,
        k_ColorRange
    };
    cmd.Barrier(rhi::DependencyInfo{ {}, {}, std::span{ &swapchainInitBarrier, 1 } });

    if (useDeferredPath)
    {
        recordDeferredPipeline(cmd, timerBlock, extent, aspect, cameraPosition);
        passTimer.reset();
    }
    else
    {
        std::vector<rhi::ImageBarrier> toAttachments{
            rhi::ImageBarrier{
                rhi::ImageHandle{ depthImage.VkHandle },
                rhi::PipelineStage::LateFragmentTests, rhi::Access::DepthStencilAttachmentWrite,
                rhi::PipelineStage::EarlyFragmentTests | rhi::PipelineStage::LateFragmentTests, rhi::Access::DepthStencilAttachmentRead | rhi::Access::DepthStencilAttachmentWrite,
                rhi::ImageLayout::Undefined, rhi::ImageLayout::DepthStencilAttachment,
                k_DepthRange
            }
        };

        if (multisampled)
        {
            toAttachments.push_back(rhi::ImageBarrier{
                rhi::ImageHandle{ msaaColorImage.VkHandle },
                rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::ColorAttachmentWrite,
                rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::ColorAttachmentRead | rhi::Access::ColorAttachmentWrite,
                rhi::ImageLayout::Undefined, rhi::ImageLayout::ColorAttachment,
                k_ColorRange
            });
        }

        cmd.Barrier(rhi::DependencyInfo{ {}, {}, toAttachments });

        const color::ColorTargetView sceneTarget = colorPipe->SceneTarget();
        rhi::ColorAttachment colorAttachment{};
        colorAttachment.ClearValue = rhi::ClearColorValue{ 0.0f, 0.0f, 0.0f, 1.0f };
        colorAttachment.LoadOp = rhi::LoadOp::Clear;
        if (multisampled)
        {
            colorAttachment.ImageView = rhi::ImageViewHandle{ msaaColorImage.VkViewHandle };
            colorAttachment.StoreOp = rhi::StoreOp::DontCare;
            colorAttachment.ResolveImageView = sceneTarget.View;
        }
        else
        {
            colorAttachment.ImageView = sceneTarget.View;
            colorAttachment.StoreOp = rhi::StoreOp::Store;
        }

        rhi::DepthStencilAttachment depthAttachment{};
        depthAttachment.ImageView = rhi::ImageViewHandle{ depthImage.VkViewHandle };
        depthAttachment.DepthLoadOp = rhi::LoadOp::Clear;
        depthAttachment.DepthStoreOp = rhi::StoreOp::DontCare;
        depthAttachment.ClearValue = rhi::ClearDepthStencilValue{ 0.0f, 0 };

        rhi::RenderingInfo renderingInfo{};
        renderingInfo.ColorAttachments = std::span{ &colorAttachment, 1 };
        renderingInfo.DepthStencilAttachment = depthAttachment;
        renderingInfo.RenderArea = rhi::Rect2D{ 0, 0, static_cast<uint32_t>(extent.x), static_cast<uint32_t>(extent.y) };
        cmd.BeginRendering(renderingInfo);

        const rhi::Viewport viewport{ 0.0f, 0.0f, extent.x, extent.y, 0.0f, 1.0f };
        cmd.SetViewports(std::span{ &viewport, 1 });
        cmd.SetScissors(std::span{ &renderingInfo.RenderArea, 1 });

        // 1. Fullscreen Sky Background
        if (environment)
        {
            const ArcballCamera::Matrix4 rotViewProj = camera.ViewRotationProjection(aspect);
            const ArcballCamera::Matrix4 invRotViewProj = math::FromMatrix<ArcballCamera::Matrix4>(math::ToMatrix(rotViewProj).Inverse());
            environment->RecordSkyBackground(cmd, &timerBlock, invRotViewProj, ArcballCamera::Vector3{ 0.0f, 0.0f, 0.0f }, 0.0f);
        }

        // 2. Seabed Draw
        const float tileSize = settings.GridSize;
        const std::array<float, 2> centerTile{
            (std::round(cameraPosition[0] / tileSize) - 0.5f) * tileSize,
            (std::round(cameraPosition[2] / tileSize) - 0.5f) * tileSize
        };
        const float rings = static_cast<float>(settings.GridRings);
        const float seabedSize = cdlod ? cdlod->Settings().WorldExtent : (2.0f * rings + 1.0f) * tileSize;
        const std::array<float, 2> seabedOrigin{ centerTile[0] - 0.5f * seabedSize, centerTile[1] - 0.5f * seabedSize };
        const uint32_t seabedResolution = cdlod ? 64u : 1u;

        {
            const rhi::ScopedGpuTimer timer(timerBlock, "Seabed");
            recordDraw(cmd, seabed, seabedOrigin, seabedSize, seabedResolution, 0u, seabedResolution, 1u, false);
        }

        // 3. Ocean Surface Draw
        if (useCdlod && cdlod)
        {
            const rhi::ScopedGpuTimer timer(timerBlock, "Surface CDLOD");
            RhiAssert(cmd.BindShaders(surface.Program->Program()));
            SetShaderObjectDrawState(cmd.Handle().As<VkCommandBuffer>(), ShaderObjectDrawState{
                .DepthTest = true,
                .DepthWrite = false,
                .AlphaBlend = true,
                .CullMode = VK_CULL_MODE_NONE,
                .PolygonMode = wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL,
                .Samples = msaaSamples
            });

            const rhi::DescriptorSetHandle setHandle{ reinterpret_cast<uint64_t>(surface.Sets[currentFrame]) };
            cmd.BindDescriptorSets(rhi::PipelineBindPoint::Graphics, surface.Program->PipelineLayout(), 0, std::span{ &setHandle, 1 });

            const OceanGridParameters grid{ centerTile[0], centerTile[1], tileSize, 64u, 0u, 64u };
            if (const std::optional<rhi::PushConstantRange> range = surface.Program->PushConstants(); range.has_value())
            {
                cmd.PushConstants(surface.Program->PipelineLayout(), range->StageFlags, 0u, std::as_bytes(std::span{ &grid, 1 }));
            }

            cmd.BindIndexBuffer(rhi::BufferHandle{ cdlod->GridIndexBuffer().VkHandle }, 0u, rhi::IndexType::Uint32);
            vkCmdDrawIndexedIndirect(
                cmd.Handle().As<VkCommandBuffer>(),
                reinterpret_cast<VkBuffer>(cdlod->IndirectDrawBuffer().VkHandle),
                0u,
                1u,
                sizeof(VkDrawIndexedIndirectCommand));
        }
        else
        {
            const rhi::ScopedGpuTimer timer(timerBlock, "Surface Grid");
            for (uint32_t ring = 0; ring <= settings.GridRings; ++ring)
            {
                const uint32_t tiles = ring == 0u ? 1u : 8u * ring;
                recordDraw(cmd, surface, centerTile, tileSize, ringResolution(ring), ring, ringResolution(ring + 1u), tiles, true);
            }
        }

        cmd.EndRendering();
        passTimer.reset();
    }

    // 4. ImGui Render Pass into ColorPipe::UiTarget (RGBA8_UNORM)
    const color::ColorTargetView uiTarget = colorPipe->UiTarget();
    {
        const rhi::ScopedGpuTimer timer(timerBlock, "UI");
        rhi::ColorAttachment uiAttachment{};
        uiAttachment.ImageView = uiTarget.View;
        uiAttachment.LoadOp = rhi::LoadOp::Clear;
        uiAttachment.ClearValue = rhi::ClearColorValue{ 0.0f, 0.0f, 0.0f, 0.0f };
        uiAttachment.StoreOp = rhi::StoreOp::Store;

        rhi::RenderingInfo uiRenderingInfo{};
        uiRenderingInfo.ColorAttachments = std::span{ &uiAttachment, 1 };
        uiRenderingInfo.RenderArea = rhi::Rect2D{ 0, 0, static_cast<uint32_t>(extent.x), static_cast<uint32_t>(extent.y) };
        cmd.BeginRendering(uiRenderingInfo);
        imgui->Render(cmd.Handle());
        cmd.EndRendering();
    }

    // 5. ColorPipe HDR Bloom & Tonemapping Composite into Swapchain
    colorPipe->RecordComposite(cmd, swapchainView, &timerBlock);

    // 6. Present or Capture
    {
        const rhi::ScopedGpuTimer timer(timerBlock, "Present or capture");
        recordPresentOrCapture(cmd, image);
    }

    RhiAssert(cmd.End());
    ++frameNumber;
}

void OceanScene::recordPresentOrCapture(rhi::CommandBuffer& command_buffer, rhi::ImageHandle image)
{
    std::optional<std::string> name = std::move(captureRequest);
    captureRequest.reset();
    if (!name.has_value() && settings.Capture.Due(frameNumber))
    {
        name = std::format("frame_{:06}", frameNumber);
    }

    if (name.has_value() && frameCapture != nullptr)
    {
        const std::optional<capture::CaptureEncoding> encoding = CaptureEncodingFor(swapchain->ImageFormat(), swapchain->ColorSpace());
        const math::Float2 extent = swapchain->GetExtent();
        capture::CaptureSource source{};
        source.Image = image;
        source.Width = static_cast<uint32_t>(extent.x);
        source.Height = static_cast<uint32_t>(extent.y);
        source.Format = swapchain->ImageFormat();
        source.Encoding = encoding.value_or(capture::CaptureEncoding::sRGB);
        source.LinearScale = colorPipe ? (k_ScRgbRefNits / colorPipe->Target().PaperWhiteNits) : (k_ScRgbRefNits / k_PaperWhiteNits);

        std::string captureName = std::move(*name);
        std::filesystem::path target = captureName;
        if (target.extension() != ".png")
        {
            target += ".png";
        }
        const std::filesystem::path fullPath = target.is_absolute() ? target : (frameCapture->Directory() / target);

        // the copy barrier also moves the image on to PresentSource
        if (encoding.has_value() && frameCapture->RecordCopy(command_buffer, currentFrame, source, captureName))
        {
            std::cout << "saved " << fullPath.string() << std::endl;
            return;
        }
        std::cerr << "OceanFft: this swapchain format cannot be captured\n";
    }

    constexpr rhi::ImageSubresourceRange k_ColorRange{ rhi::ImageAspect::Color, 0, 1, 0, 1 };
    const rhi::ImageBarrier toPresent
    {
        image,
        rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::ColorAttachmentWrite,
        rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::None,
        rhi::ImageLayout::ColorAttachment, rhi::ImageLayout::PresentSource,
        k_ColorRange
    };
    command_buffer.Barrier(rhi::DependencyInfo{ {}, {}, std::span{ &toPresent, 1 } });
}

void OceanScene::draw()
{
    if (useAsyncCompute && hasAsyncComputeQueue)
    {
        const rhi::SemaphoreSubmitInfo imageAvailable{ imageAvailableSemaphores[currentFrame].Handle(), rhi::PipelineStage::ColorAttachmentOutput };
        const rhi::SemaphoreSubmitInfo computeFinished{ asyncTimeline->Handle(), asyncTimelineValue, rhi::PipelineStage::VertexShader | rhi::PipelineStage::FragmentShader };
        const rhi::SemaphoreSubmitInfo renderFinished{ renderFinishedSemaphores[currentAcquiredImage].Handle(), rhi::PipelineStage::AllGraphics };

        const std::array<rhi::SemaphoreSubmitInfo, 2> waits{ imageAvailable, computeFinished };
        const rhi::CommandBufferHandle gfxCmdHandle = commandPools[currentFrame]->GetCommandBuffer(0);
        const rhi::SubmitInfo submit{ waits, std::span{ &gfxCmdHandle, 1 }, std::span{ &renderFinished, 1 } };
        RhiAssert(rhi::QueueSubmit(device->GetGraphicsQueue(0), submit, inFlightFences[currentFrame].Handle()));
    }
    else
    {
        submitFrame(commandPools[currentFrame]->GetCommandBuffer(0));
    }
}

namespace
{
    void Fail(std::string_view message)
    {
        std::cerr << message << '\n' << std::flush;
        throw std::runtime_error(std::string{ message });
    }
}
