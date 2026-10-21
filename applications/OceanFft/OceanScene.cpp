#include "OceanScene.hpp"
#include "ArcballCamera.hpp"
#include "CommandBuffer.hpp"
#include "CommandPool.hpp"
#include "Device.hpp"
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
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace
{
    constexpr uint64_t k_TimeoutNs = 10'000'000'000ull;
    constexpr std::string_view k_RenderModule{ "OceanRender" };
    constexpr std::array<std::string_view, 2> k_SurfaceEntryPoints{ "OceanSurfaceVS", "OceanSurfaceFS" };
    constexpr std::array<std::string_view, 2> k_SeabedEntryPoints{ "SeabedVS", "SeabedFS" };
    // the seabed reaches past the surface patch, so its edge is not right under the camera
    constexpr float k_SeabedSize = 2048.0f;

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

    createDepthImage();
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

void OceanScene::createDepthImage()
{
    const math::Float2 extent = swapchain->GetExtent();
    ImageCreateInfo info{};
    info.Domain = ResourceDomain::GPUOnly;
    info.Usage = ImageUsageBits::DepthRead | ImageUsageBits::DepthWrite;
    info.Type = ImageType::Type2D;
    info.Format = CommonFormats::Depth32;
    info.Width = static_cast<uint32_t>(extent.x);
    info.Height = static_cast<uint32_t>(extent.y);
    info.Depth = 1u;
    info.ViewInfo = ImageViewCreateInfo{};
    info.ViewInfo->ViewType = ImageViewType::Type2D;
    info.ViewInfo->Range.AspectMask = ImageAspectFlags::Depth;
    depthImage = WaitForResource(resources->CreateImage(info, {}), "the depth image");
}

void OceanScene::destroyDepthImage()
{
    if (depthImage)
    {
        (void)resources->DestroyResource(depthImage)->WaitForCompletion(k_TimeoutNs);
        depthImage = GraphicsResource{};
    }
}

void OceanScene::onSwapchainRecreated()
{
    // recreateSwapchain waited for the device, so nothing still uses the old depth image
    destroyDepthImage();
    createDepthImage();
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
    imgui.reset();
    surface = DrawProgram{};
    seabed = DrawProgram{};
    if (descriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(vkDevice, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }
    destroyDepthImage();
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
        // Donelan-Banner needs the normalization LUT from assets/scripts, which isn't generated in this build yet
        spectrumChanged |= ImGui::Combo("Spreading", &mode, "Mitsuyasu\0Hasselmann\0");
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
    drawPanel();
    if (!paused)
    {
        simulationTime += ImGui::GetIO().DeltaTime * timeScale;
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
    SetShaderObjectDrawState(command_buffer.Handle().As<VkCommandBuffer>(), ShaderObjectDrawState{ true, true, alpha_blend, VK_CULL_MODE_NONE });
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
    // both attachments are cleared, so UNDEFINED is always a valid old layout. One depth image serves every frame
    // in flight, so the previous frame's depth writes must finish first
    const std::array<rhi::ImageBarrier, 2> toAttachments
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
    cmd.Barrier(rhi::DependencyInfo{ {}, {}, toAttachments });

    rhi::ColorAttachment colorAttachment{};
    colorAttachment.ImageView = rhi::ImageViewHandle{ reinterpret_cast<uint64_t>(swapchain->ImageViewHandle(currentAcquiredImage)) };
    colorAttachment.ClearValue = rhi::ClearColorValue{ 0.30f, 0.45f, 0.65f, 1.0f };
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
    colorAttachment.LoadOp = rhi::LoadOp::Load;
    rhi::RenderingInfo uiRenderingInfo{};
    uiRenderingInfo.ColorAttachments = std::span{ &colorAttachment, 1 };
    uiRenderingInfo.RenderArea = renderingInfo.RenderArea;
    cmd.BeginRendering(uiRenderingInfo);
    imgui->Render(cmd.Handle());
    cmd.EndRendering();

    const rhi::ImageBarrier toPresent
    {
        image,
        rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::ColorAttachmentWrite,
        rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::None,
        rhi::ImageLayout::ColorAttachment, rhi::ImageLayout::PresentSource,
        k_ColorRange
    };
    cmd.Barrier(rhi::DependencyInfo{ {}, {}, std::span{ &toPresent, 1 } });
    RhiAssert(cmd.End());
}

void OceanScene::draw()
{
    submitFrame(commandPools[currentFrame]->GetCommandBuffer(0));
}
