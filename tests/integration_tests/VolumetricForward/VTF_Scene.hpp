#pragma once
#ifndef VTF_SCENE_HPP
#define VTF_SCENE_HPP
#include "VulkanScene.hpp"
#include "ForwardDecl.hpp"
#include <memory>
#include <vector>
#include <future>
#include <atomic>
#include <unordered_map>
#include "common/ShaderStage.hpp"
#include "glm/vec4.hpp"
#include "glm/vec3.hpp"
#include "glm/mat4x4.hpp"
#include <array>

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

    void MergeSort(VkCommandBuffer cmd, VulkanResource* src_keys, VulkanResource* src_values, VulkanResource* dst_keys, VulkanResource* dst_values,
        uint32_t total_values, uint32_t chunk_size);

private:

    void update() final;
    void recordCommands() final;
    void draw() final;
    void endFrame() final;

    void computeUpdateLights();
    void computeReduceLights();
    void computeAndSortMortonCodes();
    void buildLightBVH();
    void submitComputeUpdates();

    void updateClusterGrid();

    void createComputePools();
    void createRenderpasses();
    void createDepthAndClusterSamplesPass();
    void createDepthPrePassResources();
    void createClusterSamplesResources();
    void createDrawFramebuffers();
    void createDrawRenderpass();
    void createReadbackBuffers();
    void createShaderModules();
    void createComputePipelines();
    void createUpdateLightsPipeline();
    void createMortonCodePipeline();
    void createRadixSortPipeline();
    void createMergeSortPipelines();
    void createBVH_Pipelines();
    void createIndirectArgsPipeline();
    void createGraphicsPipelines();
    void createDepthPrePassPipeline();
    void createClusterSamplesPipeline();
    void createDrawPipelines();

    // generic since we use it twice, once for prepass once for rendertarget
    VulkanResource * createDepthStencilResource() const;

    // Used for debugging
    VulkanResource* pointLightsReadbackBuffer{ nullptr };
    VulkanResource* spotLightsReadbackBuffer{ nullptr };
    VulkanResource* directionalLightsReadbackBuffer{ nullptr };

    // Cluster stuff
    VulkanResource* assignLightsToClustersArgumentBuffer{ nullptr };
    VulkanResource* debugClustersDrawIndirectArgumentBuffer{ nullptr };
    VulkanResource* previousUniqueClusters{ nullptr };

    // Sorting stuff
    VulkanResource* pointLightMortonCodes;
    VulkanResource* pointLightIndices;
    VulkanResource* spotLightMortonCodes;
    VulkanResource* spotLightIndices;
    VulkanResource* pointLightMortonCodes_OUT;
    VulkanResource* pointLightIndices_OUT;
    VulkanResource* spotLightMortonCodes_OUT;
    VulkanResource* spotLightIndices_OUT;

    // Used to debug sample clusters
    VulkanResource* clusterColors{ nullptr };  
    VulkanResource* pointLightGrid{ nullptr };
    VulkanResource* spotLightGrid{ nullptr };
    VulkanResource* lightCullingDebugTexture{ nullptr };
    VulkanResource* clusterSamplesDebugTexture{ nullptr };
    VulkanResource* clusterSamplesRenderTarget{ nullptr };

    std::unique_ptr<vpr::Semaphore> computeUpdateCompleteSemaphore;

    VulkanResource* depthPrePassImage{ nullptr };
    VulkanResource* clusterSamplesImage{ nullptr };
    VulkanResource* clusterSamplesHostImageCopy{ nullptr };
    std::unique_ptr<vpr::Framebuffer> clusterSamplesFramebuffer;

    VulkanResource* depthRendertargetImage;
    std::vector<VulkanResource*> drawMultisampleImages;
    std::vector<std::unique_ptr<vpr::Framebuffer>> drawFramebuffers;

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
    std::unique_ptr<vpr::GraphicsPipeline> loadingScreenPipeline;
    std::unique_ptr<vpr::GraphicsPipeline> depthPrePassPipeline;
    std::unique_ptr<vpr::GraphicsPipeline> clusterSamplesPipeline;
    std::unique_ptr<vpr::GraphicsPipeline> opaquePassPipeline;
    std::unique_ptr<vpr::GraphicsPipeline> transparentPassPipeline;

    std::unique_ptr<vpr::GraphicsPipeline> debugPointLightsPipeline;
    std::unique_ptr<vpr::GraphicsPipeline> debugSpotLightsPipeline;
    std::unique_ptr<vpr::GraphicsPipeline> debugDepthTexturePipeline;
    std::unique_ptr<vpr::GraphicsPipeline> debugClustersPipeline;

    std::unique_ptr<vpr::Renderpass> loadingScreenPass;
    std::unique_ptr<vpr::Renderpass> depthAndClusterSamplesPass;
    std::unique_ptr<vpr::Renderpass> clusteredFinalPass;
    std::unique_ptr<vpr::Renderpass> debugLightsPass;
    std::unique_ptr<vpr::Renderpass> renderDebugTexturePass;
    std::unique_ptr<vpr::Renderpass> debugClustersPass;
    std::unique_ptr<vpr::Renderpass> debugLightCountsPass;

    std::array<VkSubpassDependency, 1> depthAndClusterDependencies;
    std::array<VkSubpassDescription, 2> depthAndSamplesPassDescriptions;
    constexpr static VkAttachmentReference prePassDepthRef{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    constexpr static VkAttachmentReference samplesPassColorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    constexpr static VkAttachmentReference samplesPassDepthRref{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };

    std::unique_ptr<vpr::CommandPool> computePools[2];

    std::unordered_map<st::ShaderStage, std::unique_ptr<vpr::ShaderModule>> shaderModules;
    std::unordered_map<std::string, std::vector<st::ShaderStage>> groupStages;
    std::unordered_map<std::string, std::unique_ptr<vpr::PipelineCache>> groupCaches;
    std::unique_ptr<vpr::PipelineCache> cumulativeCache;

};

#endif // !VTF_SCENE_HPP
