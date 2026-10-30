#pragma once
#ifndef DIAMOND_DOGS_OCEAN_SCENE_HPP
#define DIAMOND_DOGS_OCEAN_SCENE_HPP
#include "ArcballCamera.hpp"
#include "ColorPipe.hpp"
#include "FoamMaterial.hpp"
#include "OceanAtmosphere.hpp"
#include "OceanCdlod.hpp"
#include "OceanEnvironment.hpp"
#include "SeabedMaterial.hpp"
#include "Semaphore.hpp"
#include "FrameCapture.hpp"
#include "GpuTimers.hpp"
#include "OceanFoam.hpp"
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
    /** @brief The cooked ColorPipe shader bundle (ColorPipe.ldmanifest) */
    std::filesystem::path ColorPipeManifestPath;
    /** @brief Root folder of the UI fonts (assets/fonts or the copy beside the executable) */
    std::filesystem::path FontDirectory;
    /** @brief The folder of the packed foam textures, and the material name that prefixes their file names */
    std::filesystem::path FoamMaterialDirectory;
    std::string FoamMaterialName;
    /** @brief The folder of the packed seabed textures, and the material name that prefixes their file names */
    std::filesystem::path SeabedMaterialDirectory;
    std::string SeabedMaterialName;
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
    void createDeferredPrograms();
    void setupDeferredDescriptors();
    void updateDeferredDescriptors();
    void recordDeferredPipeline(rhi::CommandBuffer& cmd, rhi::GpuTimerBlock& timerBlock, const math::Float2& extent, float aspect, const math::Float3& cameraPosition);
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
    /** @brief Sets one lookdev shading field by the name that the shading command uses */
    void setShadingField(std::string_view _name, float _value);
    void drawPersistentFoamControls();
    /** @brief Moves the foam by this frame's step, and requests a replay when the foam's inputs changed */
    void advanceFoam(double delta_seconds);
    /** @brief Prints the camera, the time, the sea state and the enabled cascades, for the print command */
    void printState() const;
    void writeViewParameters();
    /** @brief Binds program and draws instance_count tiles of one ring (see OceanGridParameters in OceanRenderCommon.slang) */
    void recordDraw(rhi::CommandBuffer& command_buffer, const DrawProgram& program, const std::array<float, 2>& origin, float tile_size,
                    uint32_t resolution, uint32_t ring, uint32_t outer_resolution, uint32_t instance_count, bool alpha_blend) const;
    void updateSurfaceDescriptors();
    void updateCdlodSettings() noexcept;
    [[nodiscard]] uint32_t ringResolution(uint32_t ring) const noexcept;

    OceanSceneSettings settings;
    std::unique_ptr<ResourceContext> resources;
    std::unique_ptr<rhi::ShaderLibrary> library;
    std::unique_ptr<ocean::OceanSimulation> simulation;
    std::unique_ptr<ocean::OceanFoam> foam;
    std::unique_ptr<ocean::FoamMaterial> foamMaterial;
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
    ArcballCamera::State initialCameraState{};
    // the editor values, and what they measure. spectrum is the sea state run through ApplySeaState
    ocean::SeaState seaState{};
    ocean::SeaStateReadout seaReadout{};
    ocean::SpectrumParameters spectrum{};
    std::array<bool, ocean::k_CascadeCount> cascadeEnabled{ true, true, true, true, true };
    // per-cascade weight on the whitecap model's foam, 1 is as computed. Scene state, not part of the sea state
    std::array<float, ocean::k_CascadeCount> foamWeights{ 1.0f, 1.0f, 1.0f, 0.7f, 0.35f };
    // the persistent foam (OceanFoam.slang). Off by default, so the default image is the one without it
    ocean::FoamSettings foamSettings{};
    // lookdev scales for the foam. Scene state, not part of the sea state, and not an input of the foam replay
    float underwaterFoam{ 1.0f };
    float sunSpecular{ 1.0f };
    // the foam texture's albedo gain, and how far its normal bends the surface normal
    float foamAlbedoGain{ 2.3f };
    float foamNormalStrength{ 0.5f };
    // true rebuilds the foam on the GPU in the next frame (RecordFoamReplay), at most once per frame
    bool foamReplayPending{ true };

    // everything the foam's history depends on. While paused, a change to any of these rebuilds the foam, because
    // the foam of the new values would otherwise show only as time passes
    struct FoamReplayInputs
    {
        ocean::FoamSettings Foam{};
        std::array<float, 4> Whitecaps{};
        std::array<float, ocean::k_CascadeCount> FoamWeights{};
        std::array<bool, ocean::k_CascadeCount> CascadeEnabled{};
        float Choppiness{ 0.0f };
        std::array<int32_t, 4> WindowOrigins{};

        [[nodiscard]] bool operator==(const FoamReplayInputs& other) const = default;
    };
    FoamReplayInputs lastReplayInputs{};
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
    ocean::TelemetryTab currentTab{ ocean::TelemetryTab::SeaState };
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

    std::unique_ptr<rhi::ShaderLibrary> colorPipeLibrary;
    std::unique_ptr<color::ColorPipe> colorPipe;
    std::unique_ptr<ocean::OceanCdlod> cdlod;
    std::unique_ptr<ocean::OceanEnvironment> environment;
    std::optional<ocean::SeabedMaterial> seabedMaterial;

    std::vector<GraphicsResource> frameConstantsBuffers;
    std::vector<void*> frameConstantsBufferPointers;

    // Async Compute queue execution
    bool useAsyncCompute{ false };
    bool hasAsyncComputeQueue{ false };
    std::vector<std::unique_ptr<rhi::CommandPool>> asyncCommandPools;
    std::unique_ptr<rhi::TimelineSemaphore> asyncTimeline;
    uint64_t asyncTimelineValue{ 0ull };
    std::unique_ptr<rhi::GpuTimers> asyncGpuTimers;
    std::unordered_map<uint64_t, double> asyncGpuTimingAverages;

    // CDLOD controls
    bool useCdlod{ true };
    float lodDistanceScale{ 1.4f };
    float morphStartRatio{ 0.65f };
    std::optional<uint32_t> cdlodMaxLevelOverride;
    bool wireframe{ false };

    // Ocean Environment controls
    ocean::OceanSkyPreset skyPreset{ ocean::OceanSkyPreset::BrightDay };
    bool downsample8k{ true };
    float skyIntensityScale{ 1.0f };

    // Shading & BRDF controls
    float sssStrength{ 0.5f };
    float leadrSlopeVarianceScale{ 1.0f };
    float foamRoughnessScale{ 1.0f };
    float seabedRoughnessScale{ 1.0f };

    // ColorPipe controls
    color::TonemapperKind tonemapper{ color::TonemapperKind::AgX };
    float exposureEv{ 0.0f };
    color::BloomSettings bloomSettings{ .Enabled = true, .Intensity = 0.04f, .Scatter = 0.7f };

    // Bruneton Atmosphere (Phase 4B)
    std::unique_ptr<ocean::OceanAtmosphere> atmosphere;
    bool useBrunetonAtmosphere{ false };
    bool atmosphereSettingsChanged{ false };

    // Deferred & Temporal Path (Phases 4C & 4D)
    bool useDeferredPath{ false };
    bool useSpecularAa{ true };
    float specularAaStrength{ 1.0f };
    bool useTaa{ true };
    float taaBlendAlpha{ 0.1f };
    float ssrIntensity{ 1.0f };
    bool useAutoExposure{ true };
    float autoExposureSpeed{ 1.0f };
    uint32_t historyPingPong{ 0u };
    bool taaHistoryInitialized{ false };

    // Temporal tracking
    math::Float4x4 prevViewProj{};
    std::array<float, 2> jitterOffsetNdc{ 0.0f, 0.0f };
    std::array<float, 2> prevJitterOffsetNdc{ 0.0f, 0.0f };

    // Deferred Programs & Descriptors
    DrawProgram surfaceGBuffer;
    std::unique_ptr<rhi::ManifestProgram> specularAaProgram;
    std::unique_ptr<rhi::ManifestProgram> hiZProgram;
    std::unique_ptr<rhi::ManifestProgram> deferredLightingProgram;
    std::unique_ptr<rhi::ManifestProgram> taaResolveProgram;
    std::unique_ptr<rhi::ManifestProgram> autoExposureProgram;

    VkDescriptorPool deferredDescriptorPool{ VK_NULL_HANDLE };
    std::vector<VkDescriptorSet> specularAaSets;
    VkDescriptorSet hiZSet{ VK_NULL_HANDLE };
    std::vector<std::array<VkDescriptorSet, 2>> deferredLightingSets;
    std::vector<std::array<VkDescriptorSet, 2>> taaResolveSets;
    VkDescriptorSet autoExposureSet{ VK_NULL_HANDLE };

    // Deferred Render Targets & Resources
    GraphicsResource gbufferNormalSigma;
    GraphicsResource gbufferNormalSigmaFiltered;
    GraphicsResource gbufferFoamSss;
    GraphicsResource gbufferMotionFoam;
    GraphicsResource seabedColorImage;
    GraphicsResource seabedDepthImage;
    GraphicsResource oceanDepthImage;
    GraphicsResource hdrLightingImage;
    std::array<GraphicsResource, 2> taaHistoryImages;
    GraphicsResource hiZImage;
    GraphicsResource linearClampSampler;
    GraphicsResource pointClampSampler;
    GraphicsResource exposureStateBuffer;
    GraphicsResource atmoParamsBuffer;
    void* atmoParamsPointer{ nullptr };
};

#endif //!DIAMOND_DOGS_OCEAN_SCENE_HPP
