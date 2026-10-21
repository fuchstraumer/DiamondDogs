#include "OceanScene.hpp"
#include "ArcballCamera.hpp"
#include "CommandBuffer.hpp"
#include "CommandPool.hpp"
#include "Device.hpp"
#include "FrameCapture.hpp"
#include "ImGuiModule.hpp"
#include "ImageDataFormats.hpp"
#include "ManifestDescriptors.hpp"
#include "Math.hpp"
#include "OceanSimulation.hpp"
#include "PlatformSystem.hpp"
#include "ResourceContext.hpp"
#include "ResourceFlags.hpp"
#include "ResourceMessageReply.hpp"
#include "ResourceTypes.hpp"
#include "SceneCommands.hpp"
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
#include "vulkan/vulkan_core.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <expected>
#include <format>
#include <iostream>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    constexpr uint64_t k_TimeoutNs = 10'000'000'000ull;
    constexpr std::string_view k_RenderModule{ "OceanRender" };
    constexpr std::array<std::string_view, 2> k_SurfaceEntryPoints{ "OceanSurfaceVS", "OceanSurfaceFS" };
    constexpr std::array<std::string_view, 2> k_SeabedEntryPoints{ "SeabedVS", "SeabedFS" };
    // the seabed reaches past the surface patch, so its edge is not right under the camera
    constexpr float k_SeabedSize = 2048.0f;
    // hardware MSAA for the ocean pass: lower when the device supports less (a render setting, eventually)
    constexpr uint32_t k_MaxMsaaSamples = 8u;

    // OceanViewParameters in OceanRenderCommon.slang (std140, all float4)
    struct OceanViewParameters
    {
        std::array<std::array<float, 4>, 4> ViewProjectionRows{};
        std::array<float, 4> CameraPositionTime{};
        std::array<float, 4> LengthScales{};
        std::array<float, 4> CascadeWeights{};
        std::array<float, 4> SunDirectionSeabedDepth{};
    };
    static_assert(sizeof(OceanViewParameters) == 128u);

    // OceanGridParameters, the push constants of both vertex stages
    struct OceanGridParameters
    {
        float OriginX{ 0.0f };
        float OriginZ{ 0.0f };
        float Size{ 0.0f };
        uint32_t Resolution{ 1u };
    };

    constexpr float k_DegreesToRadians = std::numbers::pi_v<float> / 180.0f;

    /** @brief How the swapchain encodes color, or nothing when FrameCapture cannot convert it */
    std::optional<capture::CaptureEncoding> CaptureEncodingFor(ImageFormat format, ColorSpace color_space)
    {
        std::optional<capture::CaptureEncoding> encoding;
        switch (color_space)
        {
        case ColorSpace::sRGB_Nonlinear:
            encoding = capture::CaptureEncoding::sRGB;
            break;
        case ColorSpace::Extended_sRGB_Linear:
            encoding = capture::CaptureEncoding::LinearExtendedSRGB;
            break;
        case ColorSpace::HDR10_ST2084:
            encoding = capture::CaptureEncoding::HDR10_PQ;
            break;
        default:
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
            throw std::runtime_error(std::format("OceanScene: creating {} failed", what));
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
    camera.Current.Distance = 0.6f * settings.GridSize;

    if (settings.InitialCamera.has_value())
    {
        camera.Current = *settings.InitialCamera;
    }
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
    LoadShaderObjectStateFunctions(vkDevice);

    resources = std::make_unique<ResourceContext>();
    resources->Initialize(ResourceContextCreateInfo{ rhiSystem->GetInstance(), rhiSystem->GetDevice(), rhiSystem->GetVulkanApiVersion(), true });

    rhi::ShaderLibraryResult<std::unique_ptr<rhi::ShaderLibrary>> opened = rhi::ShaderLibrary::Open(OCEAN_FFT_MANIFEST);
    if (!opened.has_value())
    {
        throw std::runtime_error(std::format("OceanScene: opening {} failed: {}", OCEAN_FFT_MANIFEST, rhi::ToString(opened.error())));
    }

    library = std::move(*opened);

    auto created = ocean::OceanSimulation::Create(*device, *resources, *library, settings.Simulation);
    if (!created.has_value())
    {
        throw std::runtime_error(std::format("OceanScene: {}", created.error()));
    }

    simulation = std::move(*created);

    // linear and repeating: each cascade tiles with its own patch size
    sampler = WaitForResource(resources->CreateSampler(SamplerCreateInfo{}), "the ocean sampler");

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
            throw std::runtime_error("OceanScene: mapping a view buffer failed");
        }

        viewBufferPointers.push_back(map->GetPointer());
    }

    createRenderTargets();
    createRenderPrograms();

    const ImGuiModuleCreateInfo imguiInfo
    {
        platformSystem->GetWindowHandle(),
        rhiSystem->GetInstance(),
        device,
        rhiSystem->GetVulkanApiVersion(),
        numFramebuffers,
        numFramebuffers,
        swapchain->ImageFormat(),
        true
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
        throw std::runtime_error(std::format("OceanScene: module {}: {}", k_RenderModule, rhi::ToString(index.error())));
    }

    const std::array<VkDescriptorPoolSize, 3> poolSizes
    {
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2u * numFramebuffers },
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4u * numFramebuffers },
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLER, 2u * numFramebuffers },
    };
    const VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0,
        2u * numFramebuffers, static_cast<uint32_t>(poolSizes.size()), poolSizes.data() };
    RhiAssert(rhi::Result(vkCreateDescriptorPool(vkDevice, &poolInfo, nullptr, &descriptorPool)));

    for (auto& [program, entryPoints] : { std::pair<DrawProgram*, std::span<const std::string_view>>{ &surface, k_SurfaceEntryPoints },
                                          std::pair<DrawProgram*, std::span<const std::string_view>>{ &seabed, k_SeabedEntryPoints } })
    {
        std::expected<lodestone::VariantKey, lodestone::QueryError> variant = (*index)->Query().SingleFor(entryPoints);
        if (!variant.has_value())
        {
            throw std::runtime_error(std::format("OceanScene: no {} variant for {}", k_RenderModule, entryPoints[0]));
        }

        rhi::ShaderLibraryResult<std::unique_ptr<rhi::ManifestProgram>> created = library->CreateProgram(device->Handle(), k_RenderModule, *variant, entryPoints);
        if (!created.has_value())
        {
            throw std::runtime_error(std::format("OceanScene: {}: {}", entryPoints[0], rhi::ToString(created.error())));
        }

        program->Program = std::move(*created);

        const VkDescriptorSetLayout setLayout = program->Program->SetLayouts()[0].As<VkDescriptorSetLayout>();
        for (uint32_t frame = 0; frame < numFramebuffers; ++frame)
        {
            VkDescriptorSet& set = program->Sets.emplace_back(VK_NULL_HANDLE);
            const VkDescriptorSetAllocateInfo setInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, descriptorPool, 1, &setLayout };
            RhiAssert(rhi::Result(vkAllocateDescriptorSets(vkDevice, &setInfo, &set)));
            // the simulation's textures stay in General: the compute passes write them as storage images
            const ocean::BoundResources bound
            {
                { "View", { viewBuffers[frame] } },
                { "Displacement", { simulation->Displacement(), VK_IMAGE_LAYOUT_GENERAL } },
                { "Derivatives", { simulation->Derivatives(), VK_IMAGE_LAYOUT_GENERAL } },
                { "OceanSampler", { sampler } },
            };

            if (std::optional<std::string> error = ocean::WriteManifestDescriptors(vkDevice, set, **index, *variant, entryPoints, bound); error.has_value())
            {
                throw std::runtime_error(std::format("OceanScene: {}", *error));
            }
        }
    }
}

void OceanScene::createRenderTargets()
{
    // the highest count up to k_MaxMsaaSamples that both color and depth attachments support
    if (msaaSamples == VK_SAMPLE_COUNT_1_BIT)
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(vkPhysicalDevice, &properties);
        const VkSampleCountFlags supported = properties.limits.framebufferColorSampleCounts & properties.limits.framebufferDepthSampleCounts;
        for (uint32_t samples = k_MaxMsaaSamples; samples > 1u; samples >>= 1u)
        {
            if ((supported & samples) != 0u)
            {
                msaaSamples = static_cast<VkSampleCountFlagBits>(samples);
                break;
            }
        }
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

    // the same format as the swapchain, since it resolves straight into the swapchain image
    ImageCreateInfo colorInfo = depthInfo;
    colorInfo.Usage = ImageUsageBits::RenderTargetRead | ImageUsageBits::RenderTargetWrite;
    colorInfo.Format = swapchain->ImageFormat();
    colorInfo.ViewInfo->Range.AspectMask = ImageAspectFlags::Color;
    msaaColorImage = WaitForResource(resources->CreateImage(colorInfo, {}), "the multisampled color image");
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

    imgui.reset();
    surface = DrawProgram{};
    seabed = DrawProgram{};

    if (descriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(vkDevice, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }

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
        state.Distance = std::clamp(v[2], 2.0f, 2000.0f);
        if (command.ValueCount == 6u)
        {
            state.Target = { v[3], v[4], v[5] };
        }
        break;
    case SceneCommand::Kind::Orbit:
        state.Yaw += v[0] * k_DegreesToRadians;
        state.Pitch = std::clamp(state.Pitch + v[1] * k_DegreesToRadians, -k_MaxPitch, k_MaxPitch);
        break;
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
    case SceneCommand::Kind::Capture:
        captureRequest = command.Text.empty() ? std::format("capture_{:06}", frameNumber) : command.Text;
        break;
    case SceneCommand::Kind::Print:
        std::cout << std::format("frame {} time {:.4f} camera {:.2f} {:.2f} {:.2f} {:.2f} {:.2f} {:.2f}\n", frameNumber, simulationTime,
            state.Yaw / k_DegreesToRadians, state.Pitch / k_DegreesToRadians, state.Distance, state.Target[0], state.Target[1], state.Target[2])
            << std::flush;
        break;
    case SceneCommand::Kind::Quit:
        quitRequested = true;
        break;
    case SceneCommand::Kind::Wait:
        break;
    }
}

void OceanScene::drawPanel()
{
    ImGui::Begin("Ocean");
    ImGui::Text("%.2f ms (%.0f fps)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
    const ocean::OceanSimulationSettings& simulationSettings = simulation->Settings();
    ImGui::Text("IFFT %s %u, %s", simulationSettings.Kernel == ocean::IfftKernel::Radix2 ? "radix-2" : "radix-4", simulationSettings.Size,
        simulationSettings.WaveSize == 0u ? "no wave ops" : std::format("wave {}, full subgroups", simulationSettings.WaveSize).c_str());

    if (ImGui::CollapsingHeader("Spectrum", ImGuiTreeNodeFlags_DefaultOpen))
    {
        // every change recomputes the initial spectrum (cheap, but it waits for the device)
        spectrumChanged |= ImGui::SliderFloat("Wind speed (m/s)", &spectrum.WindSpeed, 0.5f, 30.0f);
        spectrumChanged |= ImGui::SliderFloat("Fetch (km)", &spectrum.Fetch, 0.5f, 500.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
        spectrumChanged |= ImGui::SliderAngle("Wind direction", &spectrum.LocalWindDirection, -180.0f, 180.0f);
        spectrumChanged |= ImGui::SliderFloat("Swell", &spectrum.SwellAmount, 0.0f, 1.0f);
        spectrumChanged |= ImGui::SliderFloat("Alignment", &spectrum.Alignment, 0.0f, 1.0f);
        spectrumChanged |= ImGui::SliderFloat("Peaking (gamma)", &spectrum.PeakingFactor, 1.0f, 7.0f);
        spectrumChanged |= ImGui::SliderFloat("Short wave fade (m)", &spectrum.FadeLength, 0.0f, 0.1f);
        spectrumChanged |= ImGui::SliderFloat("Depth (m)", &spectrum.Depth, 1.0f, 500.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
        int mode = static_cast<int>(spectrum.DirectionalSpreadingMode);
        spectrumChanged |= ImGui::Combo("Spreading", &mode, "Mitsuyasu\0Hasselmann\0Donelan-Banner\0");
        spectrum.DirectionalSpreadingMode = static_cast<uint32_t>(mode);
        int seed = static_cast<int>(spectrum.NoiseSeed);
        spectrumChanged |= ImGui::InputInt("Seed", &seed);
        spectrum.NoiseSeed = static_cast<uint32_t>(seed);
    }

    if (ImGui::CollapsingHeader("Surface", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SliderFloat("Choppiness", &choppiness, 0.0f, 2.0f);
        ImGui::SliderFloat("Time scale", &timeScale, 0.0f, 4.0f);
        ImGui::Checkbox("Paused", &paused);
        for (uint32_t cascade = 0; cascade < ocean::k_CascadeCount; ++cascade)
        {
            ImGui::Checkbox(std::format("Cascade {} ({:.0f} m)", cascade, simulationSettings.LengthScales[cascade]).c_str(), &cascadeEnabled[cascade]);
        }
    }

    if (ImGui::CollapsingHeader("Scene"))
    {
        ImGui::SliderFloat("Seabed depth (m)", &seabedDepth, 1.0f, 200.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderAngle("Sun elevation", &sunElevation, 1.0f, 90.0f);
        ImGui::SliderAngle("Sun azimuth", &sunAzimuth, -180.0f, 180.0f);
        ImGui::TextUnformatted("Left drag: orbit. Right or middle drag: pan. Wheel: zoom");
    }

    ImGui::End();
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

    drawPanel();

    if (!paused)
    {
        const float step = settings.FixedTimeStep > 0.0f ? settings.FixedTimeStep : ImGui::GetIO().DeltaTime;
        simulationTime += step * timeScale;
    }

    if (spectrumChanged)
    {
        // the init passes read the spectrum buffer, and earlier frames may still be reading their outputs
        device->WaitDeviceIdle();
        if (!simulation->SetSpectrum(spectrum))
        {
            throw std::runtime_error("OceanScene: writing the spectrum parameters failed");
        }
        needsInitialize = true;
        spectrumChanged = false;
    }

    simulation->SetChoppiness(choppiness);
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
    }

    const float cosElevation = std::cos(sunElevation);
    view.SunDirectionSeabedDepth = { std::sin(sunAzimuth) * cosElevation, std::sin(sunElevation), std::cos(sunAzimuth) * cosElevation, seabedDepth };

    // beginFrame waited on this frame's fence, so the GPU is done with this frame's buffer
    std::memcpy(viewBufferPointers[currentFrame], &view, sizeof(view));
}

void OceanScene::recordDraw(rhi::CommandBuffer& command_buffer, const DrawProgram& program, float grid_size, uint32_t grid_resolution, bool alpha_blend) const
{
    RhiAssert(command_buffer.BindShaders(program.Program->Program()));
    SetShaderObjectDrawState(command_buffer.Handle().As<VkCommandBuffer>(), ShaderObjectDrawState{ true, true, alpha_blend, VK_CULL_MODE_NONE, msaaSamples });
    const rhi::DescriptorSetHandle set{ reinterpret_cast<uint64_t>(program.Sets[currentFrame]) };
    command_buffer.BindDescriptorSets(rhi::PipelineBindPoint::Graphics, program.Program->PipelineLayout(), 0, std::span{ &set, 1 });
    const OceanGridParameters grid{ -0.5f * grid_size, -0.5f * grid_size, grid_size, grid_resolution };

    if (const std::optional<rhi::PushConstantRange> range = program.Program->PushConstants(); range.has_value())
    {
        command_buffer.PushConstants(program.Program->PipelineLayout(), range->StageFlags, 0u, std::as_bytes(std::span{ &grid, 1 }));
    }

    command_buffer.Draw(6u * grid_resolution * grid_resolution, 1u, 0u, 0u);
}

void OceanScene::recordCommands()
{
    writeViewParameters();

    rhi::CommandPool& pool = *commandPools[currentFrame];
    RhiAssert(pool.Reset());
    rhi::CommandBuffer cmd(pool.GetCommandBuffer(0));
    RhiAssert(cmd.Begin());

    if (needsInitialize)
    {
        simulation->RecordInitialize(cmd);
        needsInitialize = false;
    }

    // the previous frame's draws sampled the textures this tick rewrites
    const rhi::GlobalBarrier drawsToCompute
    {
        rhi::PipelineStage::VertexShader | rhi::PipelineStage::FragmentShader, rhi::Access::None,
        rhi::PipelineStage::ComputeShader, rhi::Access::None
    };
    cmd.Barrier(rhi::DependencyInfo{ std::span{ &drawsToCompute, 1 }, {}, {} });

    simulation->RecordTick(cmd, simulationTime);
    
    const rhi::GlobalBarrier computeToDraws
    {
        rhi::PipelineStage::ComputeShader, rhi::Access::ShaderStorageWrite,
        rhi::PipelineStage::VertexShader | rhi::PipelineStage::FragmentShader, rhi::Access::ShaderSampledRead
    };
    cmd.Barrier(rhi::DependencyInfo{ std::span{ &computeToDraws, 1 }, {}, {} });

    const rhi::ImageHandle image{ reinterpret_cast<uint64_t>(swapchain->ImageHandle(currentAcquiredImage)) };
    const math::Float2 extent = swapchain->GetExtent();
    constexpr rhi::ImageSubresourceRange k_ColorRange{ rhi::ImageAspect::Color, 0, 1, 0, 1 };
    constexpr rhi::ImageSubresourceRange k_DepthRange{ rhi::ImageAspect::Depth, 0, 1, 0, 1 };
    // every attachment is cleared or fully overwritten by the resolve, so UNDEFINED is always a valid old layout. One
    // depth image and one multisampled color image serve every frame in flight, so the previous frame's writes to
    // them must finish first
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

    const rhi::ImageViewHandle swapchainView{ reinterpret_cast<uint64_t>(swapchain->ImageViewHandle(currentAcquiredImage)) };
    rhi::ColorAttachment colorAttachment{};
    colorAttachment.ClearValue = rhi::ClearColorValue{ 0.30f, 0.45f, 0.65f, 1.0f };
    if (multisampled)
    {
        // the samples are only needed until the resolve, so they are not stored
        colorAttachment.ImageView = rhi::ImageViewHandle{ msaaColorImage.VkViewHandle };
        colorAttachment.StoreOp = rhi::StoreOp::DontCare;
        colorAttachment.ResolveImageView = swapchainView;
    }
    else
    {
        colorAttachment.ImageView = swapchainView;
    }

    rhi::DepthStencilAttachment depthAttachment{};
    depthAttachment.ImageView = rhi::ImageViewHandle{ depthImage.VkViewHandle };
    depthAttachment.ClearValue = rhi::ClearDepthStencilValue{ 1.0f, 0 };
    rhi::RenderingInfo renderingInfo{};
    renderingInfo.ColorAttachments = std::span{ &colorAttachment, 1 };
    renderingInfo.DepthStencilAttachment = depthAttachment;
    renderingInfo.RenderArea = rhi::Rect2D{ 0, 0, static_cast<uint32_t>(extent.x), static_cast<uint32_t>(extent.y) };
    cmd.BeginRendering(renderingInfo);

    const rhi::Viewport viewport{ 0.0f, 0.0f, extent.x, extent.y, 0.0f, 1.0f };
    cmd.SetViewports(std::span{ &viewport, 1 });
    cmd.SetScissors(std::span{ &renderingInfo.RenderArea, 1 });
    recordDraw(cmd, seabed, k_SeabedSize, 1u, false);
    recordDraw(cmd, surface, settings.GridSize, settings.GridResolution, true);
    cmd.EndRendering();

    // ImGui's pipeline was created for a color attachment only, so it gets its own pass, which keeps the ocean's pixels.
    // The barrier orders the ocean pass's color writes before the UI pass loads and blends over them
    const rhi::GlobalBarrier oceanToUi
    {
        rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::ColorAttachmentWrite,
        rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::ColorAttachmentRead | rhi::Access::ColorAttachmentWrite
    };
    cmd.Barrier(rhi::DependencyInfo{ std::span{ &oceanToUi, 1 }, {}, {} });

    // the UI draws single-sampled, straight onto the resolved swapchain image
    rhi::ColorAttachment uiAttachment{};
    uiAttachment.ImageView = swapchainView;
    uiAttachment.LoadOp = rhi::LoadOp::Load;
    rhi::RenderingInfo uiRenderingInfo{};
    uiRenderingInfo.ColorAttachments = std::span{ &uiAttachment, 1 };
    uiRenderingInfo.RenderArea = renderingInfo.RenderArea;
    cmd.BeginRendering(uiRenderingInfo);
    imgui->Render(cmd.Handle());
    cmd.EndRendering();

    recordPresentOrCapture(cmd, image);
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
        // the copy barrier also moves the image on to PresentSource
        if (encoding.has_value() && frameCapture->RecordCopy(command_buffer, currentFrame, source, std::move(*name)))
        {
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
    submitFrame(commandPools[currentFrame]->GetCommandBuffer(0));
}
