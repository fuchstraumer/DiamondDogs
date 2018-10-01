#pragma once
#ifndef VTF_SCENE_HPP
#define VTF_SCENE_HPP
#include "VulkanScene.hpp"
#include "ForwardDecl.hpp"
#include <memory>
#include <vector>
#include <future>
#include <atomic>
#include "glm/vec4.hpp"
#include "glm/vec3.hpp"
#include "glm/mat4x4.hpp"

struct VulkanResource;
struct ComputePipelineState;

namespace st {
    class ShaderPack;
}

struct SceneConfig_t {
    bool EnableMSAA{ true };
    uint32_t MSAA_SampleCount{ 8u };
    uint32_t MaxLights{ 2048u };
    glm::vec3 LightsMinBounds;
    glm::vec3 LightsMaxBounds;
    float MinSpotAngle;
    float MaxSpotAngle;
    float MinRange;
    float MaxRange;
};

inline static SceneConfig_t SceneConfig;

struct alignas(16) PointLight {
    glm::vec4 positionWS{ 0.0f, 0.0f, 0.0f, 1.0f };
    glm::vec4 positionVS{ 0.0f, 0.0f, 0.0f, 1.0f };
    glm::vec3 color{ 1.0f, 1.0f, 1.0f };
    float range{ 100.0f };
    float intensity{ 1.0f };
    uint32_t enabled{ 1u };
    uint32_t selected{ 0u };
    float padding{ 0.0f };
};

struct alignas(16) SpotLight {
    glm::vec4 positionWS{ 0.0f, 0.0f, 0.0f, 1.0f };
    glm::vec4 positionVS{ 0.0f, 0.0f, 0.0f, 1.0f };
    glm::vec4 directionWS{ 0.0f, 0.0f, 1.0f, 0.0f };
    glm::vec4 directionVS{ 0.0f, 0.0f,-1.0f, 0.0f };
    glm::vec3 color{ 1.0f, 1.0f, 1.0f };
    float spotlightAngle{ 45.0f };
    float range{ 100.0f };
    float intensity{ 1.0f };
    uint32_t enabled{ 1u };
    uint32_t selected{ 0u };
};

struct alignas(16) DirectionalLight {
    glm::vec4 directionWS{};
    glm::vec4 directionVS{};
    glm::vec3 color{ 1.0f, 1.0f, 1.0f };
    float intensity{ 1.0f };
    uint32_t enabled{ 1u };
    uint32_t selected{ 0u };
    float padding[2]{ 0.0f, 0.0f };
};

struct SceneState {
    std::vector<PointLight> PointLights;
    std::vector<SpotLight> SpotLights;
    std::vector<DirectionalLight> DirectionalLights;
};

class VTF_Scene : public VulkanScene {
public:

    static VTF_Scene& Get();

    void Construct(RequiredVprObjects objects, void* user_data) final;
    void Destroy() final;

    static uint32_t GetNumLevelsBVH(uint32_t num_leaves);
    static uint32_t GetNumNodesBVH(uint32_t num_leaves);

    void GenerateSceneLights();

    inline static PointLight* SelectedPointLight{ nullptr };
    inline static SpotLight* SelectedSpotLight{ nullptr };
    inline static DirectionalLight* SelectedDirectionalLight{ nullptr };

    inline static bool RenderLights = false;
    inline static bool RenderDebugTexture = false;
    inline static bool RenderDebugClusters = false;
    /*
        Debug and GUI-related variables
        Have to be static for ImGui's sake usually. 
        Works better with input handling.
    */
    inline static glm::mat4 previousViewMatrix = glm::mat4(1.0f);
    inline static bool UpdateUniqueClusters = true;
    inline static bool TrackSelectedLight = false;
    inline static float FocusDistance = std::numeric_limits<float>::max();

    inline static bool ShowStatistics = true;
    inline static bool ShowTestWindow = false;
    inline static bool ShowProfiler = false;
    inline static bool ShowGenerateLights = false;
    inline static bool ShowLightsHeirarchy = false;
    inline static bool ShowOptionsWindow = false;
    inline static bool ShowNotification = false;

    inline static SceneState State{};

    std::future<bool> LoadingTask;
    std::atomic<bool> isLoading{ true };

private:

    void update() final;
    void recordCommands() final;
    void draw() final;
    void endFrame() final;

    void createComputePools();
    void createReadbackBuffers();
    // uses already made sort buffers to create copies
    void createSortingOutputBuffers();


    // Used for debugging
    VulkanResource* pointLightsReadbackBuffer;
    VulkanResource* spotLightsReadbackBuffer;
    VulkanResource* directionalLightsReadbackBuffer;

    // Cluster stuff
    VulkanResource* assignLightsToClustersArgumentBuffer;
    VulkanResource* debugClustersDrawIndirectArgumentBuffer;
    VulkanResource* previousUniqueClusters;

    // Sorting stuff
    VulkanResource* pointLightMortonCodes_OUT;
    VulkanResource* pointLightIndices_OUT;
    VulkanResource* spotLightMortonCodes_OUT;
    VulkanResource* spotLightIndices_OUT;

    constexpr static uint32_t SORT_NUM_THREADS_PER_THREAD_GROUP = 256u;
    constexpr static uint32_t SORT_ELEMENTS_PER_THREAD = 8u;
    constexpr static uint32_t BVH_NUM_THREADS = 32u * 16u;
    constexpr static uint32_t AVERAGE_OVERLAPPING_LIGHTS_PER_CLUSTER = 20u;

    // Used to debug sample clusters
    VulkanResource* clusterColors;
    
    VulkanResource* pointLightGrid[2];
    VulkanResource* spotLightGrid[2];
    VulkanResource* lightCullingDebugTexture;
    VulkanResource* clusterSamplesDebugTexture;
    VulkanResource* clusterSamplesRenderTarget;

    /*
        Compute pipelines
    */
    std::unique_ptr<ComputePipelineState> updateLightsPipeline;
    std::unique_ptr<ComputePipelineState> reduceLightsAABB0;
    std::unique_ptr<ComputePipelineState> reduceLightsAABB1;
    std::unique_ptr<ComputePipelineState> computeLightMortonCodesPipeline;
    std::unique_ptr<ComputePipelineState> radixSortPipeline;
    std::unique_ptr<ComputePipelineState> mergePathPartitionsPipeline;
    std::unique_ptr<ComputePipelineState> mergeSortPipeline;
    std::unique_ptr<ComputePipelineState> buildBVHBottomPipeline;
    std::unique_ptr<ComputePipelineState> buildBVHTopPipeline;
    std::unique_ptr<ComputePipelineState> computeGridFrustumsPipeline;
    std::unique_ptr<ComputePipelineState> computeClusterAABBsPipeline;
    std::unique_ptr<ComputePipelineState> findUniqueClustersPipeline;
    std::unique_ptr<ComputePipelineState> updateIndirectArgsPipeline;
    std::unique_ptr<ComputePipelineState> assignLightsToClustersPipeline;
    /*
        graphics and debug pipelines follow
    */
    std::unique_ptr<vpr::GraphicsPipeline> opaquePassPipeline;
    std::unique_ptr<vpr::GraphicsPipeline> transparentPassPipeline;
    std::unique_ptr<vpr::GraphicsPipeline> depthPrePassPipeline;
    std::unique_ptr<vpr::GraphicsPipeline> debugPointLightsPipeline;
    std::unique_ptr<vpr::GraphicsPipeline> debugSpotLightsPipeline;
    std::unique_ptr<vpr::GraphicsPipeline> debugDepthTexturePipeline;
    std::unique_ptr<vpr::GraphicsPipeline> loadingScreenPipeline;
    std::unique_ptr<vpr::GraphicsPipeline> clusterSamplesPipeline;
    std::unique_ptr<vpr::GraphicsPipeline> debugClustersPipeline;

    std::unique_ptr<vpr::Renderpass> loadingScreenPass;
    std::unique_ptr<vpr::Renderpass> clusteredFinalPass;
    std::unique_ptr<vpr::Renderpass> depthPrePass;
    std::unique_ptr<vpr::Renderpass> debugLightsPass;
    std::unique_ptr<vpr::Renderpass> renderDebugTexturePass;
    std::unique_ptr<vpr::Renderpass> debugClustersPass;
    std::unique_ptr<vpr::Renderpass> debugLightCountsPass;

    std::unique_ptr<vpr::CommandPool> computePools[2];

};

#endif // !VTF_SCENE_HPP
