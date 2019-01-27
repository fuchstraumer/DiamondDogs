#pragma once
#ifndef VTF_FRAME_DATA_HPP
#define VTF_FRAME_DATA_HPP
#include "ForwardDecl.hpp"
#include "common/ShaderStage.hpp"
#include "vtfStructs.hpp"
#include "DescriptorBinder.hpp"
#include <unordered_map>
#include <string>
#include <memory>

struct VulkanResource;
class DescriptorPack;
namespace st {
    class ShaderPack;
}

class vtf_frame_data_t {
public:

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

    std::unordered_map<std::string, ComputePipelineState> computePipelines;
    std::unordered_map<std::string, std::unique_ptr<vpr::GraphicsPipeline>> graphicsPipelines;
    std::unordered_map<std::string, std::unique_ptr<vpr::Renderpass>> renderPasses;
    std::unique_ptr<vpr::CommandPool> computePool;
    std::unique_ptr<vpr::CommandPool> graphicsPool;
    std::unique_ptr<DescriptorPack> descriptorPack;
    std::unordered_map<std::string, DescriptorBinder> binders;
    std::unique_ptr<vpr::Framebuffer> clusterSamplesFramebuffer;
    std::unique_ptr<vpr::Framebuffer> drawFramebuffer;
    std::unique_ptr<vpr::Semaphore> computeUpdateCompleteSemaphore{ nullptr };
    std::unique_ptr<vpr::Semaphore> radixSortPointLightsSemaphore{ nullptr };
    std::unique_ptr<vpr::Semaphore> radixSortSpotLightsSemaphore{ nullptr };
    std::unique_ptr<vpr::Fence> computeAABBsFence{ nullptr };
    VkDispatchIndirectCommand indirectArgsCmd;
    bool updateUniqueClusters{ true };
    bool frameRecreate{ false };

    /*
        Static resources: all of these should really not be duplicated across frames/threads
        pipelineCaches are fully thread-safe, as they are internally synchronized (thank god)
    */
    inline static st::ShaderPack* vtfShaders{ nullptr };
    static std::unordered_map<std::string, std::vector<st::ShaderStage>> groupStages;
    static std::unordered_map<std::string, std::unique_ptr<vpr::PipelineCache>> pipelineCaches;
    static std::unordered_map<st::ShaderStage, std::unique_ptr<vpr::ShaderModule>> shaderModules;

    VulkanResource*& operator[](const char* name) {
        return rsrcMap.at(name);
    }

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
