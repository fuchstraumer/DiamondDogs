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
#include <unordered_map>
#include <string>
#include <memory>

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
        { "DrawMultisampleImage", nullptr }
    };

    enum class render_type {
        Opaque = 0,
        Transparent = 1,
        OpaqueAndTransparent = 2,
        GUI = 2,
        Postprocess = 3,
        Shadow = 4
    };

    using obj_render_fn_t = delegate_t<void(VkCommandBuffer cmd, DescriptorBinder* binder, render_type type)>;
    multicast_delegate_t<void(VkCommandBuffer cmd, DescriptorBinder* binder, render_type type)> renderFns;
    multicast_delegate_t<void(VkCommandBuffer cmd)> guiLayerRenderFns;

	std::vector<VulkanResource*> transientResources;
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
    uint32_t imageIdx{ std::numeric_limits<uint32_t>::max() };
    uint32_t lastImageIdx{ std::numeric_limits<uint32_t>::max() };
    VkDispatchIndirectCommand indirectArgsCmd;
    bool updateUniqueClusters{ true };
    bool frameRecreate{ false };    
    vpr::VkDebugUtilsFunctions vkDebugFns;

    /*
        Static resources: all of these should really not be duplicated across frames/threads
        pipelineCaches are fully thread-safe, as they are internally synchronized (thank god)
    */
    inline static const st::ShaderPack* vtfShaders{ nullptr };
    static std::unordered_map<std::string, std::vector<st::ShaderStage>> groupStages;
    static std::unordered_map<std::string, std::unique_ptr<vpr::PipelineCache>> pipelineCaches;
    static std::unordered_map<st::ShaderStage, std::unique_ptr<vpr::ShaderModule>> shaderModules;

    DescriptorBinder& GetBinder(const char* name) {
        if (binders.count(name) == 0) {
            auto iter = binders.emplace(name, descriptorPack->RetrieveBinder(name));
            return iter.first->second;
        }
        else {
            return binders.at(name);
        }
    }
};

#endif // !VTF_FRAME_DATA_HPP
