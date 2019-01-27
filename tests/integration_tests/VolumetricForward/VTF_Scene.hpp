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
#include "vtfStructs.hpp"
#include "common/ShaderStage.hpp"
#include "glm/vec4.hpp"
#include "glm/vec3.hpp"
#include "glm/mat4x4.hpp"
#include <array>

struct VulkanResource;
struct ComputePipelineState;
class Descriptor;
class DescriptorPack;
class vtf_frame_data_t;

namespace st {
    class ShaderPack;
}


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
    inline static VkSampleCountFlagBits MSAA_SampleCount{ VK_SAMPLE_COUNT_2_BIT };

    std::future<bool> LoadingTask;
    std::atomic<bool> isLoading{ true };

    void MergeSort(VkCommandBuffer cmd, VulkanResource* src_keys, VulkanResource* src_values, VulkanResource* dst_keys, VulkanResource* dst_values,
        uint32_t total_values, uint32_t chunk_size);

private:

    void updateGlobalUBOs();
    void update() final;
    void recordCommands() final;
    void draw() final;
    void endFrame() final;

    void computeUpdateLights();
    void computeReduceLights();
    void computeAndSortMortonCodes();
    void buildLightBVH();
    void submitComputeUpdates();
    void updateClusterGrid(vtf_frame_data_t& rsrcs);
    void computeClusterAABBs();

    void createComputePools();
    void createRenderpasses();
    void createDepthAndClusterSamplesPass();
    void createDepthPrePassResources(vtf_frame_data_t* rsrcs);
    void createClusterSamplesResources(vtf_frame_data_t* rsrcs);
    void createDrawRenderpass();
    void createDrawFramebuffers();
    void createShaderModules();
    void createComputePipelines();
    void createUpdateLightsPipeline();
    void createReduceLightAABBsPipelines();
    void createMortonCodePipeline();
    void createRadixSortPipeline();
    void createMergeSortPipelines();
    void createBVH_Pipelines();
    void createComputeClusterAABBsPipeline();
    void createIndirectArgsPipeline();
    void createGraphicsPipelines();
    void createDepthPrePassPipeline();
    void createClusterSamplesPipeline();
    void createDrawPipelines();

    void createFrameResources(vtf_frame_data_t* rsrcs);
    void createvForwardResources(vtf_frame_data_t* rsrcs);
    void createComputeSemaphores(vtf_frame_data_t* rsrcs);
    void createLightResources(vtf_frame_data_t& rsrcs);
    void createSortingResources(vtf_frame_data_t& rsrcs);
    void createMortonResources(vtf_frame_data_t& frame_data);
    void createBVH_Resources(vtf_frame_data_t* rsrcs);

    VulkanResource* loadTexture(const char * file_path_str);

    void createIcosphereTester();


    // generic since we use it twice, once for prepass once for rendertarget
    VulkanResource* createDepthStencilResource(const VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT) const;

    std::deque<vtf_frame_data_t> frameResources;
    vtf_frame_data_t* currFrameResources;
    // need to keep these, once done we push back onto frameResources
    vtf_frame_data_t* lastFrameResources;

    VulkanResource* clusterSamplesImage{ nullptr };
    VulkanResource* clusterSamplesHostImageCopy{ nullptr };
    std::unique_ptr<vpr::Framebuffer> clusterSamplesFramebuffer;

    std::vector<VulkanResource*> depthRendertargetImages;
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

    std::array<VkSubpassDependency, 1> depthAndClusterDependencies;
    std::array<VkSubpassDescription, 2> depthAndSamplesPassDescriptions;

    std::array<VkSubpassDependency, 2> drawPassDependencies;
    std::array<VkSubpassDescription, 1> drawPassDescriptions;

    std::unique_ptr<vpr::CommandPool> computePools[2];
    std::unique_ptr<vpr::Fence> computeAABBsFence;
    std::unique_ptr<vpr::Fence> mortonSortingFence;

    std::unordered_map<st::ShaderStage, std::unique_ptr<vpr::ShaderModule>> shaderModules;
    std::unordered_map<std::string, std::vector<st::ShaderStage>> groupStages;
    std::unordered_map<std::string, std::unique_ptr<vpr::PipelineCache>> groupCaches;
    std::unique_ptr<vpr::PipelineCache> cumulativeCache;

    std::unique_ptr<struct TestIcosphereMesh> icosphereTester;

};

#endif // !VTF_SCENE_HPP
