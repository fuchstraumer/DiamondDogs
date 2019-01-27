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

private:

    void updateGlobalUBOs();
    void update() final;
    void recordCommands() final;
    void draw() final;
    void endFrame() final;

    void submitComputeUpdates();

    void createFrameResources(vtf_frame_data_t* rsrcs);
    void createvForwardResources(vtf_frame_data_t* rsrcs);
    void createComputeSemaphores(vtf_frame_data_t* rsrcs);
    void createLightResources(vtf_frame_data_t& rsrcs);
    void createSortingResources(vtf_frame_data_t& rsrcs);
    void createMortonResources(vtf_frame_data_t& frame_data);
    void createBVH_Resources(vtf_frame_data_t* rsrcs);

    VulkanResource* loadTexture(const char * file_path_str);

    void createIcosphereTester();

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

    std::unique_ptr<vpr::CommandPool> computePools[2];
    std::unique_ptr<vpr::Fence> computeAABBsFence;
    std::unique_ptr<vpr::Fence> mortonSortingFence;

    std::unique_ptr<struct TestIcosphereMesh> icosphereTester;

};

#endif // !VTF_SCENE_HPP
