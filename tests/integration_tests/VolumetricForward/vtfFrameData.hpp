#pragma once
#ifndef VTF_FRAME_DATA_HPP
#define VTF_FRAME_DATA_HPP
#include "ForwardDecl.hpp"
#include "common/ShaderStage.hpp"
#include "vtfStructs.hpp"
#include "DescriptorBinder.hpp"
#include "Descriptor.hpp"
#include "DescriptorPack.hpp"
#include "multicast_delegate.hpp"
#include "VkDebugUtils.hpp"
#include "Material.hpp"
#include <unordered_map>
#include <string>
#include <memory>
#include <forward_list>
#include <functional>

struct VulkanResource;
class DescriptorPack;
namespace st {
    class ShaderPack;
}

class vtf_frame_data_t {
	vtf_frame_data_t(const vtf_frame_data_t&) = delete;
	vtf_frame_data_t& operator=(const vtf_frame_data_t&) = delete;
public:

    vtf_frame_data_t();
    ~vtf_frame_data_t();

    /*
        Unique resource types and distinct structures
        At some point we need a more anonymous feature for this, so that this "frame data" concept
        can become more universalized and expandable.
    */
    LightCountsData LightCounts;
    DispatchParams_t DispatchParams;
    BVH_Params_t BVH_Params;
    ClusterData_t ClusterData;
    Matrices_t Matrices;
    GlobalsData Globals;

    /*
        Vulkan API resources, either the API version of above resources or the shader resources
    */
    std::unordered_map<std::string, VulkanResource*> rsrcMap{
        { "matrices", nullptr },
        { "globals", nullptr },
        { "ClusterData", nullptr },
        { "ClusterAABBs", nullptr },
        { "ClusterFlags", nullptr },
        { "PointLightIndexList", nullptr },
        { "SpotLightIndexList", nullptr },
        { "PointLightGrid", nullptr },
        { "PointLightIndexCounter", nullptr },
        { "SpotLightIndexCounter", nullptr },
        { "UniqueClustersCounter", nullptr },
        { "UniqueClusters", nullptr },
        { "PreviousUniqueClusters", nullptr },
        { "LightCounts", nullptr },
        { "PointLights", nullptr },
        { "SpotLights", nullptr },
        { "DirectionalLights", nullptr },
        { "IndirectArgs", nullptr },
        // instead of a barrier to allow us to use, update, use dispatchParams; just have two copies
        { "DispatchParams0", nullptr },
        { "DispatchParams1", nullptr },
        { "ReductionParams", nullptr },
        { "SortParams", nullptr },
        { "LightAABBs", nullptr },
        { "PointLightMortonCodes", nullptr },
        { "SpotLightMortonCodes", nullptr },
        { "PointLightIndices", nullptr },
        { "SpotLightIndices", nullptr },
        { "PointLightMortonCodes_OUT", nullptr },
        { "SpotLightMortonCodes_OUT", nullptr },
        { "PointLightIndices_OUT", nullptr },
        { "SpotLightIndices_OUT", nullptr },
        { "MergePathPartitions", nullptr },
        { "bvhParams", nullptr },
        { "PointLightBVH", nullptr },
        { "SpotLightBVH", nullptr },
        { "AssignLightsToClustersArgumentBuffer", nullptr },
        { "DebugClustersDrawIndirectArgumentBuffer", nullptr },
        { "DepthPrePassImage", nullptr },
        { "ClusterSamplesImage", nullptr },
        { "DepthRenderTargetImage", nullptr },
        { "DrawMultisampleImage", nullptr },
		{ "DebugClusterColors", nullptr }
    };

    using obj_render_fn_t = std::function<void(const objRenderStateData& state)>;
	std::vector<obj_render_fn_t> renderFns;
	using binder_fn_t = std::function<void(Descriptor* descr)>;
	std::vector<binder_fn_t> bindFns;

	std::vector<VulkanResource*> transientResources;
    std::vector<VulkanResource*> lastFrameTransientResources;
    std::unordered_map<std::string, ComputePipelineState> computePipelines;
    std::unordered_map<std::string, std::unique_ptr<vpr::GraphicsPipeline>> graphicsPipelines;
    std::unordered_map<std::string, std::unique_ptr<vpr::Renderpass>> renderPasses;
    std::unordered_map<std::string, std::unique_ptr<vpr::Semaphore>> semaphores;
    std::unique_ptr<vpr::CommandPool> computePool;
    std::unique_ptr<vpr::CommandPool> graphicsPool;
    std::unique_ptr<DescriptorPack> descriptorPack;
    std::unordered_map<std::string, DescriptorBinder> binders;
    std::unique_ptr<vpr::Framebuffer> clusterSamplesFramebuffer;
    std::unique_ptr<vpr::Framebuffer> drawFramebuffer;
    std::unique_ptr<vpr::Fence> computeAABBsFence{ nullptr };
	std::unique_ptr<vpr::Fence> graphicsPoolUsageFence{ nullptr };
	std::unique_ptr<vpr::Fence> computePoolUsageFence{ nullptr };
    std::unique_ptr<vpr::Fence> preRenderGraphicsFence{ nullptr };
	std::unique_ptr<QueryPool> queryPool{ nullptr };
	bool firstComputeSubmit{ true };
	bool firstGraphicsSubmit{ true };
    uint32_t imageIdx{ std::numeric_limits<uint32_t>::max() };
    uint32_t lastImageIdx{ std::numeric_limits<uint32_t>::max() };
    VkDispatchIndirectCommand indirectArgsCmd;
    bool updateUniqueClusters{ true };
    bool computeClusterAABBs{ true };
    bool frameRecreate{ false };    
    bool renderDebugClusters{ true };
    bool optimizedLighting{ true };
    vpr::VkDebugUtilsFunctions vkDebugFns;
    glm::mat4 previousViewMatrix = glm::mat4(1.0f);

    Material debugMaterial;

    /*
        Static resources: all of these should really not be duplicated across frames/threads
        pipelineCaches are fully thread-safe, as they are internally synchronized (thank god)
    */
    inline static const st::ShaderPack* vtfShaders{ nullptr };
    static std::unordered_map<std::string, std::vector<st::ShaderStage>> groupStages;
    static std::unordered_map<std::string, std::unique_ptr<vpr::PipelineCache>> pipelineCaches;
    static std::unordered_map<st::ShaderStage, std::unique_ptr<vpr::ShaderModule>> shaderModules;

};

#endif // !VTF_FRAME_DATA_HPP
