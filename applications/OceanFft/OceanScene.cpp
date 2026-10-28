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
    };
    static_assert(sizeof(FrameConstants) == 208u);

    bool InvertMatrix4(const std::array<std::array<float, 4>, 4>& m, std::array<std::array<float, 4>, 4>& out) noexcept
    {
        float a[16];
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                a[r * 4 + c] = m[r][c];

        float inv[16];

        inv[0] = a[5]  * a[10] * a[15] - a[5]  * a[11] * a[14] - a[9]  * a[6]  * a[15] + a[9]  * a[7]  * a[14] + a[13] * a[6]  * a[11] - a[13] * a[7]  * a[10];
        inv[4] = -a[4]  * a[10] * a[15] + a[4]  * a[11] * a[14] + a[8]  * a[6]  * a[15] - a[8]  * a[7]  * a[14] - a[12] * a[6]  * a[11] + a[12] * a[7]  * a[10];
        inv[8] = a[4]  * a[9] * a[15] - a[4]  * a[11] * a[13] - a[8]  * a[5] * a[15] + a[8]  * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
        inv[12] = -a[4]  * a[9] * a[14] + a[4]  * a[10] * a[13] + a[8]  * a[5] * a[14] - a[8]  * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
        inv[1] = -a[1]  * a[10] * a[15] + a[1]  * a[11] * a[14] + a[9]  * a[2] * a[15] - a[9]  * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
        inv[5] = a[0]  * a[10] * a[15] - a[0]  * a[11] * a[14] - a[8]  * a[2] * a[15] + a[8]  * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
        inv[9] = -a[0]  * a[9] * a[15] + a[0]  * a[11] * a[13] + a[8]  * a[1] * a[15] - a[8]  * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
        inv[13] = a[0]  * a[9] * a[14] - a[0]  * a[10] * a[13] - a[8]  * a[1] * a[14] + a[8]  * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
        inv[2] = a[1]  * a[6] * a[15] - a[1]  * a[7] * a[14] - a[5]  * a[2] * a[15] + a[5]  * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
        inv[6] = -a[0]  * a[6] * a[15] + a[0]  * a[7] * a[14] + a[4]  * a[2] * a[15] - a[4]  * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
        inv[10] = a[0]  * a[5] * a[15] - a[0]  * a[7] * a[13] - a[4]  * a[1] * a[15] + a[4]  * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
        inv[14] = -a[0]  * a[5] * a[14] + a[0]  * a[6] * a[13] + a[4]  * a[1] * a[14] - a[4]  * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
        inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
        inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
        inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
        inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];

        float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
        if (std::abs(det) < 1e-8f)
            return false;

        float invDet = 1.0f / det;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                out[r][c] = inv[r * 4 + c] * invDet;

        return true;
    }

    ocean::OceanCdlodCameraState ComputeCdlodCamera(const ArcballCamera::Matrix4& vp, const ArcballCamera::Vector3& camPos) noexcept
    {
        ocean::OceanCdlodCameraState state{};
        state.CameraPosition = camPos;

        const auto& r0 = vp[0];
        const auto& r1 = vp[1];
        const auto& r2 = vp[2];
        const auto& r3 = vp[3];

        state.FrustumPlanes[0] = { r3[0] + r0[0], r3[1] + r0[1], r3[2] + r0[2], r3[3] + r0[3] };
        state.FrustumPlanes[1] = { r3[0] - r0[0], r3[1] - r0[1], r3[2] - r0[2], r3[3] - r0[3] };
        state.FrustumPlanes[2] = { r3[0] + r1[0], r3[1] + r1[1], r3[2] + r1[2], r3[3] + r1[3] };
        state.FrustumPlanes[3] = { r3[0] - r1[0], r3[1] - r1[1], r3[2] - r1[2], r3[3] - r1[3] };
        state.FrustumPlanes[4] = { r2[0], r2[1], r2[2], r2[3] };
        state.FrustumPlanes[5] = { r3[0] - r2[0], r3[1] - r2[1], r3[2] - r2[2], r3[3] - r2[3] };

        for (auto& plane : state.FrustumPlanes)
        {
            const float len = std::sqrt(plane[0] * plane[0] + plane[1] * plane[1] + plane[2] * plane[2]);
            if (len > 1e-7f)
            {
                const float invLen = 1.0f / len;
                plane[0] *= invLen;
                plane[1] *= invLen;
                plane[2] *= invLen;
                plane[3] *= invLen;
            }
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
    cdlodSettings.WorldExtent = settings.GridSize * static_cast<float>(2u * settings.GridRings + 1u);
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
        ocean::ComputeCdlodMaxLevel(cdlodSettings.WorldExtent, cdlodSettings.BaseGridCount, simulation->Settings().Size, initFinestCascadeLength, 9u));
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

    createRenderTargets();
    createRenderPrograms();
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
        s.MaxLevel = ocean::ComputeCdlodMaxLevel(s.WorldExtent, s.BaseGridCount, fftSize, finestCascadeLength, 9u);
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
}

void OceanScene::destroyRenderTargets()
{
    for (GraphicsResource* target : { &depthImage, &msaaColorImage })
    {
        if (*target)
        {
            (void)resources->DestroyResource(*target)->WaitForCompletion(k_TimeoutNs);
            *target = GraphicsResource{};
        }
    }
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
    case SceneCommand::Kind::Sky:
        if (command.Text == "preetham")
        {
            skyPreset = ocean::OceanSkyPreset::AnalyticPreetham;
        }
        else if (command.Text == "aurora")
        {
            skyPreset = ocean::OceanSkyPreset::AuroralNight;
        }
        else if (command.Text == "day")
        {
            skyPreset = ocean::OceanSkyPreset::BrightDay;
        }
        else if (command.Text == "night")
        {
            skyPreset = ocean::OceanSkyPreset::StarryNight;
        }
        else if (command.Text == "overcast")
        {
            skyPreset = ocean::OceanSkyPreset::OvercastEvening;
        }
        else if (command.Text == "twilight")
        {
            skyPreset = ocean::OceanSkyPreset::Twilight;
        }
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

    OceanViewParameters view{};
    view.ViewProjectionRows = camera.ViewProjection(extent.x / std::max(extent.y, 1.0f));
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
    frameConstants.UseSkyTexture = (skyPreset == ocean::OceanSkyPreset::AnalyticPreetham) ? 0u : 1u;
    frameConstants.SkyIntensityScale = skyIntensityScale;
    frameConstants.LeadrSlopeVarianceScale = leadrSlopeVarianceScale;

    std::memcpy(frameConstantsBufferPointers[currentFrame], &frameConstants, sizeof(FrameConstants));
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

void OceanScene::recordCommands()
{
    writeViewParameters();

    const math::Float2 extent = swapchain->GetExtent();
    const ArcballCamera::Vector3 cameraPosition = camera.Position();
    const ArcballCamera::Matrix4 viewProj = camera.ViewProjection(extent.x / std::max(extent.y, 1.0f));
    ArcballCamera::Matrix4 invViewProj{};
    InvertMatrix4(viewProj, invViewProj);
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

    std::vector<rhi::ImageBarrier> toAttachments
    {
        rhi::ImageBarrier
        {
            image,
            rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::None,
            rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::ColorAttachmentRead | rhi::Access::ColorAttachmentWrite,
            rhi::ImageLayout::Undefined, rhi::ImageLayout::ColorAttachment,
            k_ColorRange
        },
        rhi::ImageBarrier
        {
            rhi::ImageHandle{ depthImage.VkHandle },
            rhi::PipelineStage::LateFragmentTests, rhi::Access::DepthStencilAttachmentWrite,
            rhi::PipelineStage::EarlyFragmentTests | rhi::PipelineStage::LateFragmentTests, rhi::Access::DepthStencilAttachmentRead | rhi::Access::DepthStencilAttachmentWrite,
            rhi::ImageLayout::Undefined, rhi::ImageLayout::DepthStencilAttachment,
            k_DepthRange
        },
    };

    if (multisampled)
    {
        toAttachments.push_back(rhi::ImageBarrier
        {
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
    depthAttachment.ClearValue = rhi::ClearDepthStencilValue{ 1.0f, 0 };

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
        environment->RecordSkyBackground(cmd, &timerBlock, invViewProj, cameraPosition, 0.0f);
    }

    // 2. Seabed Draw
    const float tileSize = settings.GridSize;
    const std::array<float, 2> centerTile
    {
        (std::round(cameraPosition[0] / tileSize) - 0.5f) * tileSize,
        (std::round(cameraPosition[2] / tileSize) - 0.5f) * tileSize
    };
    const float rings = static_cast<float>(settings.GridRings);
    const float seabedSize = (2.0f * rings + 1.0f) * tileSize;
    const std::array<float, 2> seabedOrigin{ centerTile[0] - rings * tileSize, centerTile[1] - rings * tileSize };

    {
        const rhi::ScopedGpuTimer timer(timerBlock, "Seabed");
        recordDraw(cmd, seabed, seabedOrigin, seabedSize, 1u, 0u, 1u, 1u, false);
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
        uiRenderingInfo.RenderArea = renderingInfo.RenderArea;
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
