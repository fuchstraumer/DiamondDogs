#pragma once
#ifndef VTF_FRAME_DATA_HPP
#define VTF_FRAME_DATA_HPP
#include "ForwardDecl.hpp"
#include <unordered_map>
#include <string>
#include <memory>
#include "vtfStructs.hpp"
#include "DescriptorBinder.hpp"

struct VulkanResource;
class DescriptorPack;

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
        { "DebugClustersDrawIndirectArgumentBuffer", nullptr }
    };

    std::unordered_map<std::string, ComputePipelineState> computePipelines;
    std::unordered_map<std::string, std::unique_ptr<vpr::GraphicsPipeline>> graphicsPipelines;
    static std::unordered_map<std::string, std::unique_ptr<vpr::PipelineCache>> pipelineCaches;

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

    std::unique_ptr<vpr::CommandPool> computePool;
    std::unique_ptr<vpr::CommandPool> graphicsPool;
    std::unique_ptr<DescriptorPack> descriptorPack;
    std::unordered_map<std::string, DescriptorBinder> binders;

    // Following not required, left for once we get further
    // Used for debugging
    VulkanResource* pointLightsReadbackBuffer{ nullptr };
    VulkanResource* spotLightsReadbackBuffer{ nullptr };
    VulkanResource* directionalLightsReadbackBuffer{ nullptr };
    // Cluster stuff
    VulkanResource* assignLightsToClustersArgumentBuffer{ nullptr };
    VulkanResource* debugClustersDrawIndirectArgumentBuffer{ nullptr };
    VulkanResource* previousUniqueClusters{ nullptr };
    // Sorting stuff
    VulkanResource* clusterColors{ nullptr };
    VulkanResource* lightCullingDebugTexture{ nullptr };
    VulkanResource* clusterSamplesDebugTexture{ nullptr };
    VulkanResource* clusterSamplesRenderTarget{ nullptr };

    std::unique_ptr<vpr::Semaphore> computeUpdateCompleteSemaphore{ nullptr };
    std::unique_ptr<vpr::Semaphore> radixSortPointLightsSemaphore{ nullptr };
    std::unique_ptr<vpr::Semaphore> radixSortSpotLightsSemaphore{ nullptr };

    std::unique_ptr<vpr::Fence> computeAABBsFence{ nullptr };

    VulkanResource* depthPrePassImage{ nullptr };
    VkDispatchIndirectCommand indirectArgsCmd;
    bool updateUniqueClusters{ true };
};

#endif // !VTF_FRAME_DATA_HPP
