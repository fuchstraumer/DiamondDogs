#pragma once
#ifndef DIAMOND_DOGS_OCEAN_SCENE_HPP
#define DIAMOND_DOGS_OCEAN_SCENE_HPP
#include "ArcballCamera.hpp"
#include "OceanSimulation.hpp"
#include "ResourceTypes.hpp"
#include "VariantKey.hpp"
#include "VulkanScene.hpp"
#include <vulkan/vulkan_core.h>
#include <array>
#include <memory>
#include <vector>

class ImGuiModule;
class ResourceContext;

namespace rhi
{
    class CommandPool;
    class ManifestProgram;
    class ShaderLibrary;
}

struct OceanSceneSettings
{
    ocean::OceanSimulationSettings Simulation{};
    /** @brief The surface grid: Resolution x Resolution quads over Size meters, centered on the origin */
    float GridSize{ 256.0f };
    uint32_t GridResolution{ 384u };
};

/** A square of ocean over a flat seabed, with an orbit camera (left drag rotates, right or middle drag pans, the
 *  wheel zooms) and an ImGui panel for the spectrum and the cascades. Compute runs on the graphics queue each frame:
 *  tick, IFFT, resolve, then the seabed and the surface draws. */
class OceanScene : public VulkanScene
{
public:
    OceanScene(rhi::RhiSystem* rhi_system, PlatformWindowSystem* platform_system, OceanSceneSettings settings);
    ~OceanScene();

    void Initialize(void* user_data) final;
    void Destroy() final;

protected:
    void update() final;
    void recordCommands() final;
    void draw() final;
    void onSwapchainRecreated() final;

private:
    struct DrawProgram
    {
        std::unique_ptr<rhi::ManifestProgram> Program;
        // one per frame in flight, because each frame has its own view buffer
        std::vector<VkDescriptorSet> Sets;
    };

    void createRenderPrograms();
    void createDepthImage();
    void destroyDepthImage();
    void updateCamera();
    void drawPanel();
    void writeViewParameters();
    void recordDraw(rhi::CommandBuffer& command_buffer, const DrawProgram& program, float grid_size, uint32_t grid_resolution, bool alpha_blend) const;

    OceanSceneSettings settings;
    std::unique_ptr<ResourceContext> resources;
    std::unique_ptr<rhi::ShaderLibrary> library;
    std::unique_ptr<ocean::OceanSimulation> simulation;
    std::unique_ptr<ImGuiModule> imgui;
    std::vector<std::unique_ptr<rhi::CommandPool>> commandPools;

    VkDescriptorPool descriptorPool{ VK_NULL_HANDLE };
    DrawProgram surface;
    DrawProgram seabed;
    GraphicsResource sampler;
    GraphicsResource depthImage;
    std::vector<GraphicsResource> viewBuffers;
    std::vector<void*> viewBufferPointers;

    ArcballCamera camera;
    ocean::SpectrumParameters spectrum{};
    std::array<bool, ocean::k_CascadeCount> cascadeEnabled{ true, true, true, true };
    float choppiness{ 1.0f };
    float timeScale{ 1.0f };
    float simulationTime{ 0.0f };
    float seabedDepth{ 30.0f };
    float sunElevation{ 0.5f };
    float sunAzimuth{ 2.2f };
    bool paused{ false };
    bool spectrumChanged{ false };
    bool needsInitialize{ true };
    bool setup{ false };
};

#endif //!DIAMOND_DOGS_OCEAN_SCENE_HPP
