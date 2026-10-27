#pragma once
#ifndef DIAMOND_DOGS_OCEAN_SCENE_HPP
#define DIAMOND_DOGS_OCEAN_SCENE_HPP
#include "ArcballCamera.hpp"
#include "FrameCapture.hpp"
#include "GpuTimers.hpp"
#include "OceanSimulation.hpp"
#include "OceanUi.hpp"
#include "SeaState.hpp"
#include "ResourceTypes.hpp"
#include "VulkanScene.hpp"
#include <vulkan/vulkan_core.h>
#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class ImGuiModule;
class ResourceContext;
class SceneCommands;
struct SceneCommand;

namespace rhi
{
    class CommandPool;
    class ManifestProgram;
    class ShaderLibrary;
}

struct OceanSceneSettings
{
    ocean::OceanSimulationSettings Simulation{};
    /** @brief The cooked shader bundle (OceanFft.ldmanifest) */
    std::filesystem::path ManifestPath;
    /** @brief Root folder of the UI fonts (assets/fonts or the copy beside the executable) */
    std::filesystem::path FontDirectory;
    /** @brief Whether the instance has the validation layer, so debug object names can be set */
    bool ValidationEnabled{ false };
    /** @brief The surface: tiles of GridSize meters in GridRings rings around the camera's tile. The center tile has
     *  GridResolution x GridResolution quads; each ring out halves that, down to GridMinResolution (powers of two
     *  apart, so the rings stitch without cracks) */
    float GridSize{ 256.0f };
    uint32_t GridResolution{ 384u };
    uint32_t GridRings{ 12u };
    uint32_t GridMinResolution{ 6u };
    /** @brief The highest MSAA sample count: 1, 2, 4 or 8. The scene uses the highest count up to this one that the color
     *  and depth attachments both support. 1 turns MSAA off */
    uint32_t MaxMsaaSamples{ 4u };
    /** @brief The anisotropic filtering of the surface sampler: 1, 2, 4, 8 or 16. 1 turns it off. The device limit caps it */
    float MaxAnisotropy{ 4.0f };
    std::optional<ArcballCamera::State> InitialCamera;
    /** @brief Captures go here, from the schedule and from capture commands */
    std::filesystem::path CaptureDirectory{ "captures" };
    capture::CaptureSchedule Capture{};
    /** @brief A fixed simulation step per frame in seconds, so captures N frames apart are N steps apart. 0 uses real time */
    float FixedTimeStep{ 0.0f };
    /** @brief Commands from a script or stdin, run between frames. May be null */
    SceneCommands* Commands{ nullptr };
};

/** @brief Returns the quads per edge of one ring. Each ring halves the one inside it, down to min_resolution */
[[nodiscard]] uint32_t GridRingResolution(uint32_t grid_resolution, uint32_t min_resolution, uint32_t ring) noexcept;
/** @brief True when every ring's resolution is a power of two times the resolution of the ring outside it, so the rings
 *  stitch without cracks. grid_resolution must also be at least min_resolution */
[[nodiscard]] bool GridStitches(uint32_t grid_resolution, uint32_t rings, uint32_t min_resolution) noexcept;

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
    /** @brief True after a quit command */
    [[nodiscard]] bool QuitRequested() const noexcept { return quitRequested; }
    /** @brief The frames the scene has rendered */
    [[nodiscard]] uint64_t FrameNumber() const noexcept { return frameNumber; }

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
    void createRenderTargets();
    void destroyRenderTargets();
    void updateCamera();
    void runCommand(const SceneCommand& command);
    void recordPresentOrCapture(rhi::CommandBuffer& command_buffer, rhi::ImageHandle image);
    void drawPanel();
    void drawTimingsPanel();
    /** @brief The frame's coalesce step for the GPU timers: collects finished frames into the rolling averages */
    void collectGpuTimings();
    /** @brief Runs the sea state pipe into spectrum and seaReadout */
    void applySeaState();
    /** @brief Runs the sea state pipe and marks the spectrum for the next update. The panel and the commands both call it */
    void seaStateEdited();
    void setSeaStatePreset(std::string_view _name);
    void setSeaStateField(std::string_view _name, float _value);
    void setCascade(uint32_t _cascade, bool _enabled);
    void setFoamWeight(uint32_t _cascade, float _weight);
    /** @brief Sets one persistent foam field by the name that the foam command uses */
    void setFoamField(std::string_view _name, float _value);
    void drawPersistentFoamControls();
    /** @brief Prints the camera, the time, the sea state and the enabled cascades, for the print command */
    void printState() const;
    void writeViewParameters();
    /** @brief Binds program and draws instance_count tiles of one ring (see OceanGridParameters in OceanRenderCommon.slang) */
    void recordDraw(rhi::CommandBuffer& command_buffer, const DrawProgram& program, const std::array<float, 2>& origin, float tile_size,
                    uint32_t resolution, uint32_t ring, uint32_t outer_resolution, uint32_t instance_count, bool alpha_blend) const;
    [[nodiscard]] uint32_t ringResolution(uint32_t ring) const noexcept;

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
    // the scene renders at msaaSamples into msaaColorImage and depthImage, and resolves into the swapchain image
    GraphicsResource msaaColorImage;
    VkSampleCountFlagBits msaaSamples{ VK_SAMPLE_COUNT_1_BIT };
    // the anisotropy the surface sampler uses, 1 when filtering is off
    float anisotropy{ 1.0f };
    std::vector<GraphicsResource> viewBuffers;
    std::vector<void*> viewBufferPointers;

    ArcballCamera camera;
    // the editor values, and what they measure. spectrum is the sea state run through ApplySeaState
    ocean::SeaState seaState{};
    ocean::SeaStateReadout seaReadout{};
    ocean::SpectrumParameters spectrum{};
    std::array<bool, ocean::k_CascadeCount> cascadeEnabled{ true, true, true, true, true };
    // per-cascade weight on the whitecap model's foam, 1 is as computed. Scene state, not part of the sea state
    std::array<float, ocean::k_CascadeCount> foamWeights{ 1.0f, 1.0f, 1.0f, 0.7f, 0.35f };
    // the persistent foam (OceanFoamSim.slang). Off by default, so the default image is the one without it
    ocean::FoamSettings foamSettings{};
    bool persistentFoamEnabled{ false };
    float persistentFoamAmount{ 1.0f };
    bool foamResetRequested{ false };
    // tiling A/B: sample each cascade through its own rotation and offset (see CascadeUv in OceanRenderCommon.slang)
    bool rotateCascades{ false };
    float choppiness{ 1.0f };
    float timeScale{ 1.0f };
    float simulationTime{ 0.0f };
    float seabedDepth{ 30.0f };
    // cascade c fades out between lodScale and 2 lodScale times its patch length from the camera
    float lodScale{ 15.0f };
    float sunElevation{ 0.5f };
    float sunAzimuth{ 2.2f };
    ocean::HeaderFontMode headerFontMode{ ocean::HeaderFontMode::Blinker };
    bool paused{ false };
    bool showUi{ true };
    bool spectrumChanged{ false };
    bool needsInitialize{ true };
    bool setup{ false };
    bool quitRequested{ false };
    uint64_t frameNumber{ 0u };
    std::optional<std::string> captureRequest;
    std::unique_ptr<capture::FrameCapture> frameCapture;

    // GPU timer scopes on the graphics queue, one block per frame command buffer (inactive in Release). Averages are
    // exponential, per scope hash, in nanoseconds
    std::unique_ptr<rhi::GpuTimers> gpuTimers;
    std::unordered_map<uint64_t, double> gpuTimingAverages;
};

#endif //!DIAMOND_DOGS_OCEAN_SCENE_HPP
