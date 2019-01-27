#include "vtfTasksAndSteps.hpp"
#include "vtfStructs.hpp"
#include "vtfFrameData.hpp"
#include <cstdint>
#include <vulkan/vulkan.h>
#include "CommandPool.hpp"
#include "LogicalDevice.hpp"
#include "PhysicalDevice.hpp"
#include "ShaderModule.hpp"
#include "PipelineCache.hpp"
#include "GraphicsPipeline.hpp"
#include "Fence.hpp"
#include "Semaphore.hpp"
#include "Swapchain.hpp"
#include "ResourceContext.hpp"
#include "RenderingContext.hpp"
#include "DescriptorPack.hpp"
#include "Descriptor.hpp"
#include "DescriptorBinder.hpp"
#include "PerspectiveCamera.hpp"
#include "core/ShaderPack.hpp"
#include "common/ShaderStage.hpp"
#include "core/Shader.hpp"
#include "vkAssert.hpp"
#include "Renderpass.hpp"
#include <array>

constexpr static uint32_t SORT_NUM_THREADS_PER_THREAD_GROUP = 256u;
constexpr static uint32_t SORT_ELEMENTS_PER_THREAD = 8u;
constexpr static uint32_t BVH_NUM_THREADS = 32u * 16u;
constexpr static uint32_t AVERAGE_OVERLAPPING_LIGHTS_PER_CLUSTER = 20u;
constexpr static uint32_t LIGHT_GRID_BLOCK_SIZE = 32u;
constexpr static uint32_t CLUSTER_GRID_BLOCK_SIZE = 64u;
constexpr static uint32_t AVERAGE_LIGHTS_PER_TILE = 100u;

// The number of nodes at each level of the BVH.
constexpr static uint32_t NumLevelNodes[6]{
    1,          // 1st level (32^0)
    32,         // 2nd level (32^1)
    1024,       // 3rd level (32^2)
    32768,      // 4th level (32^3)
    1048576,    // 5th level (32^4)
    33554432,   // 6th level (32^5)
};

// The number of nodes required to represent a BVH given the number of levels
// of the BVH.
constexpr static uint32_t NumBVHNodes[6]{
    1,          // 1 level  =32^0
    33,         // 2 levels +32^1
    1057,       // 3 levels +32^2
    33825,      // 4 levels +32^3
    1082401,    // 5 levels +32^4
    34636833,   // 6 levels +32^5
};

constexpr static std::array<VkVertexInputAttributeDescription, 4> VertexAttributes {
    VkVertexInputAttributeDescription{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 },
    VkVertexInputAttributeDescription{ 1, 0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 3 },
    VkVertexInputAttributeDescription{ 2, 0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 6 },
    VkVertexInputAttributeDescription{ 3, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 9 }
};

constexpr static std::array<VkVertexInputBindingDescription, 1> VertexBindingDescriptions {
    VkVertexInputBindingDescription{ 0, sizeof(float) * 11, VK_VERTEX_INPUT_RATE_VERTEX }
};

constexpr static VkAttachmentReference prePassDepthRef{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
constexpr static VkAttachmentReference samplesPassColorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
constexpr static VkAttachmentReference samplesPassDepthRref{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };
constexpr static VkAttachmentReference drawPassColorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
constexpr static VkAttachmentReference drawPassDepthRef{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
constexpr static VkAttachmentReference drawPassPresentRef{ 2, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR };

constexpr static VkClearValue DefaultColorClearValue{ 0.1f, 0.1f, 0.15f, 1.0f };
constexpr static VkClearValue DefaultDepthStencilClearValue{ 1.0f, 0 };

constexpr static std::array<const VkClearValue, 2> DepthPrePassAndClusterSamplesClearValues{
    DefaultColorClearValue,
    DefaultDepthStencilClearValue
};

constexpr static std::array<const VkClearValue, 3> DrawPassClearValues{
    DefaultColorClearValue,
    DefaultDepthStencilClearValue,
    DefaultColorClearValue
};

constexpr static VkPipelineColorBlendAttachmentState AdditiveBlendingAttachmentState{
    VK_TRUE,
    VK_BLEND_FACTOR_SRC_ALPHA,
    VK_BLEND_FACTOR_ONE,
    VK_BLEND_OP_ADD,
    VK_BLEND_FACTOR_ONE,
    VK_BLEND_FACTOR_ZERO,
    VK_BLEND_OP_ADD,
    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
};

constexpr static VkPipelineColorBlendAttachmentState AlphaBlendingAttachmentState{
    VK_TRUE,
    VK_BLEND_FACTOR_SRC_ALPHA,
    VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    VK_BLEND_OP_ADD,
    VK_BLEND_FACTOR_ONE,
    VK_BLEND_FACTOR_ZERO,
    VK_BLEND_OP_ADD,
    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
};

constexpr static VkPipelineColorBlendAttachmentState DefaultBlendingAttachmentState{
    VK_FALSE,
    VK_BLEND_FACTOR_ONE,
    VK_BLEND_FACTOR_ZERO,
    VK_BLEND_OP_ADD,
    VK_BLEND_FACTOR_ONE,
    VK_BLEND_FACTOR_ZERO,
    VK_BLEND_OP_ADD,
    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
};

uint32_t GetNumLevelsBVH(uint32_t num_leaves) noexcept {
    static const float log32f = glm::log(32.0f);

    uint32_t num_levels = 0;
    if (num_leaves > 0) {
        num_levels = static_cast<uint32_t>(glm::ceil(glm::log(num_leaves) / log32f));
    }

    return num_levels;
}

uint32_t GetNumNodesBVH(uint32_t num_leaves) noexcept {
    uint32_t num_levels = GetNumLevelsBVH(num_leaves);
    uint32_t num_nodes = 0;
    if (num_levels > 0 && num_levels < _countof(NumBVHNodes)) {
        num_nodes = NumBVHNodes[num_levels - 1];
    }

    return num_nodes;
}

// binder is passed as a copy / by-value as this is intended behavior: this new binder instance can now fork into separate threads from the original, but also inherits
// the original sets (so only sets we actually update, the MergeSort one, need to be re-allocated or re-assigned (as updating a set invalidates it for that frame)
void MergeSort(vtf_frame_data_t& frame, VkCommandBuffer cmd, VulkanResource* src_keys, VulkanResource* src_values, VulkanResource* dst_keys, VulkanResource* dst_values,
    uint32_t total_values, uint32_t chunk_size, DescriptorBinder dscr_binder);

void CreateShaders(vtf_frame_data_t & frame) {

    auto* device = RenderingContext::Get().Device();
    auto* physicalDevice = RenderingContext::Get().PhysicalDevice();

    if (!vtf_frame_data_t::shaderModules.empty() && !vtf_frame_data_t::pipelineCaches.empty()) {
        return; // don't recreate this data, it's setup-once (unless we have a recreation event)
    }

    std::vector<std::string> group_names;
    {
        auto group_names_dll = vtf_frame_data_t::vtfShaders->GetShaderGroupNames();
        for (size_t i = 0; i < group_names_dll.NumStrings; ++i) {
            group_names.emplace_back(group_names_dll[i]);
        }
    }

    for (const auto& name : group_names) {
        const st::Shader* curr_shader = vtf_frame_data_t::vtfShaders->GetShaderGroup(name.c_str());
        size_t num_stages{ 0 };
        curr_shader->GetShaderStages(&num_stages, nullptr);
        std::vector<st::ShaderStage> stages(num_stages, st::ShaderStage("aaaa", VK_SHADER_STAGE_ALL));
        curr_shader->GetShaderStages(&num_stages, stages.data());

        for (const auto& stage : stages) {
            size_t binary_sz{ 0 };
            curr_shader->GetShaderBinary(stage, &binary_sz, nullptr);
            std::vector<uint32_t> binary_data(binary_sz);
            curr_shader->GetShaderBinary(stage, &binary_sz, binary_data.data());
            vtf_frame_data_t::shaderModules.emplace(stage, std::make_unique<vpr::ShaderModule>(device->vkHandle(), stage.GetStage(), binary_data.data(), static_cast<uint32_t>(binary_data.size() * sizeof(uint32_t))));
            vtf_frame_data_t::groupStages[name].emplace_back(stage);
        }


        vtf_frame_data_t::pipelineCaches.emplace(name, std::make_unique<vpr::PipelineCache>(device->vkHandle(), physicalDevice->vkHandle(), std::hash<std::string>()(name)));

    }
}

void SetupDescriptors(vtf_frame_data_t& frame) {
    frame.descriptorPack = std::make_unique<DescriptorPack>(nullptr, vtf_frame_data_t::vtfShaders);
}

void createUpdateLightsPipeline(vtf_frame_data_t& frame) {

    auto* device = RenderingContext::Get().Device();
    const static std::string groupName{ "UpdateLights" };
    const st::Shader* update_lights_shader = vtf_frame_data_t::vtfShaders->GetShaderGroup(groupName.c_str());
    const st::ShaderStage& update_lights_stage = vtf_frame_data_t::groupStages.at(groupName).front();

    const VkComputePipelineCreateInfo pipeline_info{
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        nullptr,
        0,
        vtf_frame_data_t::shaderModules.at(update_lights_stage)->PipelineInfo(),
        frame.descriptorPack->PipelineLayout(groupName),
        VK_NULL_HANDLE,
        -1
    };

    frame.computePipelines["UpdateLightsPipeline"] = ComputePipelineState(device->vkHandle());
    VkResult result = vkCreateComputePipelines(device->vkHandle(), vtf_frame_data_t::pipelineCaches.at(groupName)->vkHandle(), 1, &pipeline_info, nullptr, &frame.computePipelines.at("UpdateLightsPipeline").Handle);
    VkAssert(result);

}

void createReduceLightAABBsPipelines(vtf_frame_data_t& frame) {

    auto* device = RenderingContext::Get().Device();
    const static std::string groupName{ "ReduceLights" };
    const st::Shader* reduce_lights_shader = vtf_frame_data_t::vtfShaders->GetShaderGroup(groupName.c_str());
    const st::ShaderStage& reduce_lights_stage = vtf_frame_data_t::groupStages.at(groupName).front();

    VkPipelineShaderStageCreateInfo pipeline_shader_info = vtf_frame_data_t::shaderModules.at(reduce_lights_stage)->PipelineInfo();

    const VkComputePipelineCreateInfo reduce_lights_0_info{
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        nullptr,
        VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT,
        pipeline_shader_info,
        frame.descriptorPack->PipelineLayout(groupName),
        VK_NULL_HANDLE,
        -1
    };

    frame.computePipelines["ReduceLightsAABB0"] = ComputePipelineState(device->vkHandle());
    VkResult result = vkCreateComputePipelines(device->vkHandle(), vtf_frame_data_t::pipelineCaches.at(groupName)->vkHandle(), 1, &reduce_lights_0_info, nullptr, &frame.computePipelines.at("ReduceLightsAABB0").Handle);
    VkAssert(result);

    constexpr static VkSpecializationMapEntry reduce_lights_1_entry{
        0,
        0,
        sizeof(uint32_t)
    };

    constexpr static uint32_t reduce_lights_1_spc_value{ 1 };

    const VkSpecializationInfo reduce_lights_1_specialization_info{
        1,
        &reduce_lights_1_entry,
        sizeof(uint32_t),
        &reduce_lights_1_spc_value
    };

    pipeline_shader_info.pSpecializationInfo = &reduce_lights_1_specialization_info;

    const VkComputePipelineCreateInfo reduce_lights_1_info{
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        nullptr,
        VK_PIPELINE_CREATE_DERIVATIVE_BIT,
        pipeline_shader_info,
        frame.descriptorPack->PipelineLayout(groupName),
        frame.computePipelines.at("ReduceLightsAABB0").Handle,
        -1
    };

    frame.computePipelines["ReduceLightsAABB1"] = ComputePipelineState(device->vkHandle());
    result = vkCreateComputePipelines(device->vkHandle(), vtf_frame_data_t::pipelineCaches.at(groupName)->vkHandle(), 1, &reduce_lights_1_info, nullptr, &frame.computePipelines.at("ReduceLightsAABB1").Handle);
    VkAssert(result);

}

void createMortonCodePipeline(vtf_frame_data_t& frame) {

    auto* device = RenderingContext::Get().Device();
    const static std::string groupName{ "ComputeMortonCodes" };
    const st::Shader* compute_morton_shader = vtf_frame_data_t::vtfShaders->GetShaderGroup(groupName.c_str());
    const st::ShaderStage& morton_stage = vtf_frame_data_t::groupStages.at(groupName).front();

    const VkComputePipelineCreateInfo pipeline_info{
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        nullptr,
        0,
        vtf_frame_data_t::shaderModules.at(morton_stage)->PipelineInfo(),
        frame.descriptorPack->PipelineLayout(groupName),
        VK_NULL_HANDLE,
        -1
    };

    frame.computePipelines["ComputeLightMortonCodesPipeline"] = ComputePipelineState(device->vkHandle());
    VkResult result = vkCreateComputePipelines(device->vkHandle(), vtf_frame_data_t::pipelineCaches.at(groupName)->vkHandle(), 1, &pipeline_info, nullptr, &frame.computePipelines.at("ComputeLightMortonCodesPipeline").Handle);
    VkAssert(result);

}

void createRadixSortPipeline(vtf_frame_data_t& frame) {

    auto* device = RenderingContext::Get().Device();
    const static std::string groupName{ "RadixSort" };
    const st::Shader* radix_shader = vtf_frame_data_t::vtfShaders->GetShaderGroup(groupName.c_str());
    const st::ShaderStage& radix_stage = vtf_frame_data_t::groupStages.at(groupName).front();

    const VkComputePipelineCreateInfo pipeline_info{
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        nullptr,
        0,
        vtf_frame_data_t::shaderModules.at(radix_stage)->PipelineInfo(),
        frame.descriptorPack->PipelineLayout(groupName),
        VK_NULL_HANDLE,
        -1
    };

    frame.computePipelines["RadixSortPipeline"] = ComputePipelineState(device->vkHandle());
    VkResult result = vkCreateComputePipelines(device->vkHandle(), vtf_frame_data_t::pipelineCaches.at(groupName)->vkHandle(), 1, &pipeline_info, nullptr, &frame.computePipelines.at("RadixSortPipeline").Handle);
    VkAssert(result);
}

void createBVH_Pipelines(vtf_frame_data_t& frame) {

    auto* device = RenderingContext::Get().Device();
    const static std::string groupName{ "BuildBVH" };
    const st::Shader* bvh_construction_shader = vtf_frame_data_t::vtfShaders->GetShaderGroup("BuildBVH");
    const st::ShaderStage& bvh_stage = vtf_frame_data_t::groupStages.at("BuildBVH").front();

    // retrieve and prepare to set constants
    size_t num_specialization_constants{ 0 };
    bvh_construction_shader->GetSpecializationConstants(&num_specialization_constants, nullptr);
    std::vector<st::SpecializationConstant> constants(num_specialization_constants);
    bvh_construction_shader->GetSpecializationConstants(&num_specialization_constants, constants.data());

    VkPipelineShaderStageCreateInfo pipeline_shader_info = vtf_frame_data_t::shaderModules.at(bvh_stage)->PipelineInfo();

    VkComputePipelineCreateInfo pipeline_info_0{
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        nullptr,
        VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT,
        pipeline_shader_info,
        frame.descriptorPack->PipelineLayout(groupName),
        VK_NULL_HANDLE,
        -1
    };

    frame.computePipelines["BuildBVHBottomPipeline"] = ComputePipelineState(device->vkHandle());
    VkResult result = vkCreateComputePipelines(device->vkHandle(), vtf_frame_data_t::pipelineCaches.at("BuildBVH")->vkHandle(), 1, &pipeline_info_0, nullptr, &frame.computePipelines.at("BuildBVHBottomPipeline").Handle);
    VkAssert(result);

    constexpr static VkSpecializationMapEntry stage_entry{
        0,
        0,
        sizeof(uint32_t)
    };

    constexpr static uint32_t specialization_value{ 1 };

    const VkSpecializationInfo specialization_info{
        1,
        &stage_entry,
        sizeof(uint32_t),
        &specialization_value
    };

    pipeline_shader_info.pSpecializationInfo = &specialization_info;

    VkComputePipelineCreateInfo pipeline_info_1{
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        nullptr,
        VK_PIPELINE_CREATE_DERIVATIVE_BIT,
        pipeline_shader_info,
        frame.descriptorPack->PipelineLayout(groupName),
        frame.computePipelines["BuildBVHBottomPipeline"].Handle,
        -1
    };

    frame.computePipelines["BuildBVHTopPipeline"] = ComputePipelineState(device->vkHandle());
    result = vkCreateComputePipelines(device->vkHandle(), vtf_frame_data_t::pipelineCaches.at("BuildBVH")->vkHandle(), 1, &pipeline_info_1, nullptr, &frame.computePipelines.at("BuildBVHTopPipeline").Handle);
    VkAssert(result);

}

void createComputeClusterAABBsPipeline(vtf_frame_data_t& frame) {

    auto* device = RenderingContext::Get().Device();
    const static std::string groupName{ "ComputeClusterAABBs" };
    const st::Shader* compute_shader = vtf_frame_data_t::vtfShaders->GetShaderGroup(groupName.c_str());
    const st::ShaderStage& compute_stage = vtf_frame_data_t::groupStages.at(groupName).front();

    const VkComputePipelineCreateInfo pipeline_info{
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        nullptr,
        0,
        vtf_frame_data_t::shaderModules.at(compute_stage)->PipelineInfo(),
        frame.descriptorPack->PipelineLayout(groupName),
        VK_NULL_HANDLE,
        -1
    };

    frame.computePipelines["ComputeClusterAABBsPipeline"] = ComputePipelineState(device->vkHandle());
    VkResult result = vkCreateComputePipelines(device->vkHandle(), vtf_frame_data_t::pipelineCaches.at(groupName)->vkHandle(), 1, &pipeline_info, nullptr, &frame.computePipelines.at("ComputeClusterAABBsPipeline").Handle);
    VkAssert(result);

}

void createIndirectArgsPipeline(vtf_frame_data_t& frame) {

    auto* device = RenderingContext::Get().Device();
    const static std::string groupName{ "UpdateClusterIndirectArgs" };
    const st::Shader* indir_shader = vtf_frame_data_t::vtfShaders->GetShaderGroup(groupName.c_str());
    const st::ShaderStage& indir_stage = vtf_frame_data_t::groupStages.at(groupName).front();

    const VkComputePipelineCreateInfo pipeline_info{
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        nullptr,
        0,
        vtf_frame_data_t::shaderModules.at(indir_stage)->PipelineInfo(),
        frame.descriptorPack->PipelineLayout(groupName),
        VK_NULL_HANDLE,
        -1
    };

    frame.computePipelines["UpdateIndirectArgsPipeline"] = ComputePipelineState(device->vkHandle());
    VkResult result = vkCreateComputePipelines(device->vkHandle(), vtf_frame_data_t::pipelineCaches.at(groupName)->vkHandle(), 1, &pipeline_info, nullptr, &frame.computePipelines.at("UpdateIndirectArgsPipeline").Handle);
    VkAssert(result);
}

void createMergeSortPipelines(vtf_frame_data_t& frame) {

    auto* device = RenderingContext::Get().Device();
    const static std::string groupName{ "MergeSort" };
    const st::Shader* merge_shader = vtf_frame_data_t::vtfShaders->GetShaderGroup(groupName.c_str());
    const st::ShaderStage& radix_stage = vtf_frame_data_t::groupStages.at(groupName).front();

    VkPipelineShaderStageCreateInfo shader_info = vtf_frame_data_t::shaderModules.at(radix_stage)->PipelineInfo();

    constexpr static VkSpecializationMapEntry stage_entry{
        0,
        0,
        sizeof(uint32_t)
    };

    constexpr static uint32_t specialization_value{ 1 };

    const VkSpecializationInfo specialization_info{
        1,
        &stage_entry,
        sizeof(uint32_t),
        &specialization_value
    };

    shader_info.pSpecializationInfo = &specialization_info;

    VkPipeline pipeline_handles_buf[2]{ VK_NULL_HANDLE, VK_NULL_HANDLE };
    const VkComputePipelineCreateInfo pipeline_infos[2]{
        VkComputePipelineCreateInfo{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            nullptr,
            VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT,
            vtf_frame_data_t::shaderModules.at(radix_stage)->PipelineInfo(),
            frame.descriptorPack->PipelineLayout(groupName),
            VK_NULL_HANDLE,
            -1
        },
        VkComputePipelineCreateInfo{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            nullptr,
            VK_PIPELINE_CREATE_DERIVATIVE_BIT,
            shader_info,
            frame.descriptorPack->PipelineLayout(groupName),
            VK_NULL_HANDLE,
            0 // index is previous pipeline to derive from
        }
    };

    VkResult result = vkCreateComputePipelines(device->vkHandle(), vtf_frame_data_t::pipelineCaches.at(groupName)->vkHandle(), 2, pipeline_infos, nullptr, pipeline_handles_buf);
    VkAssert(result);

    frame.computePipelines["MergePathPartitionsPipeline"] = ComputePipelineState(device->vkHandle());
    frame.computePipelines.at("MergePathPartitionsPipeline").Handle = pipeline_handles_buf[0];

    frame.computePipelines["MergeSortPipeline"] = ComputePipelineState(device->vkHandle());
    frame.computePipelines.at("MergeSortPipeline").Handle = pipeline_handles_buf[1];

}

void CreateComputePipelines(vtf_frame_data_t& frame) {
    createUpdateLightsPipeline(frame);
    createReduceLightAABBsPipelines(frame);
    createMortonCodePipeline(frame);
    createRadixSortPipeline(frame);
    createBVH_Pipelines(frame);
    createComputeClusterAABBsPipeline(frame);
    createIndirectArgsPipeline(frame);
    createMergeSortPipelines(frame);
}

void createDepthAndClusterSamplesPass(vtf_frame_data_t& frame) {

    std::array<VkSubpassDependency, 1> depthAndClusterDependencies;
    std::array<VkSubpassDescription, 2> depthAndSamplesPassDescriptions;
    const auto* device = RenderingContext::Get().Device();
    const auto* swapchain = RenderingContext::Get().Swapchain();

    const VkAttachmentDescription clusterSampleAttachmentDescriptions[2]{
        VkAttachmentDescription{
            0,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_SAMPLE_COUNT_1_BIT,
            VK_ATTACHMENT_LOAD_OP_CLEAR,
            VK_ATTACHMENT_STORE_OP_STORE,
            VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            VK_ATTACHMENT_STORE_OP_DONT_CARE,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        },
        VkAttachmentDescription{
            0,
            device->FindDepthFormat(),
            VK_SAMPLE_COUNT_1_BIT,
            VK_ATTACHMENT_LOAD_OP_CLEAR,
            VK_ATTACHMENT_STORE_OP_STORE,
            VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            VK_ATTACHMENT_STORE_OP_DONT_CARE,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL // might as well
        },
    };

    depthAndClusterDependencies[0] = VkSubpassDependency{
        0,
        1,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, // no fragment shader, only have to wait for here
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, // won't read until here in next subpass
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
        VK_DEPENDENCY_BY_REGION_BIT
    };

    depthAndSamplesPassDescriptions[0] = VkSubpassDescription{
        0,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        0u,
        nullptr,
        0u,
        nullptr,
        nullptr,
        &prePassDepthRef,
        0u,
        nullptr
    };

    depthAndSamplesPassDescriptions[1] = VkSubpassDescription{
        0,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        0u,
        nullptr,
        1u,
        &samplesPassColorRef,
        nullptr,
        &samplesPassDepthRref,
        0u,
        nullptr
    };

    const VkRenderPassCreateInfo create_info{
        VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        nullptr,
        0,
        2u,
        clusterSampleAttachmentDescriptions,
        static_cast<uint32_t>(depthAndSamplesPassDescriptions.size()),
        depthAndSamplesPassDescriptions.data(),
        static_cast<uint32_t>(depthAndClusterDependencies.size()),
        depthAndClusterDependencies.data()
    };

    frame.renderPasses["DepthAndClusterSamplesPass"] = std::make_unique<vpr::Renderpass>(device->vkHandle(), create_info);
    frame.renderPasses.at("DepthAndClusterSamplesPass")->SetupBeginInfo(DepthPrePassAndClusterSamplesClearValues.data(), DepthPrePassAndClusterSamplesClearValues.size(), swapchain->Extent());

}

void CreateRenderpasses(vtf_frame_data_t& frame) {
    createDepthAndClusterSamplesPass(frame);
}

void createDepthPrePassPipeline(vtf_frame_data_t& frame) {

    auto* device = RenderingContext::Get().Device();
    static const std::string groupName{ "DepthPrePass" };
    const st::Shader* depth_group = vtf_frame_data_t::vtfShaders->GetShaderGroup(groupName.c_str());

    const st::ShaderStage& depth_vert = vtf_frame_data_t::groupStages.at(groupName).front();
    const st::ShaderStage& depth_frag = vtf_frame_data_t::groupStages.at(groupName).back();

    vpr::GraphicsPipelineInfo pipeline_info;

    pipeline_info.DepthStencilInfo.depthTestEnable = VK_TRUE;
    pipeline_info.DepthStencilInfo.depthWriteEnable = VK_TRUE;
    pipeline_info.DepthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    pipeline_info.VertexInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(VertexAttributes.size());
    pipeline_info.VertexInfo.pVertexAttributeDescriptions = VertexAttributes.data();
    pipeline_info.VertexInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(VertexBindingDescriptions.size());
    pipeline_info.VertexInfo.pVertexBindingDescriptions = VertexBindingDescriptions.data();

    pipeline_info.DynamicStateInfo.dynamicStateCount = 2;
    static constexpr VkDynamicState States[2]{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    pipeline_info.DynamicStateInfo.pDynamicStates = States;

    VkGraphicsPipelineCreateInfo create_info = pipeline_info.GetPipelineCreateInfo();
    const VkPipelineShaderStageCreateInfo shader_stages[2]{ 
        vtf_frame_data_t::shaderModules.at(depth_vert)->PipelineInfo(), 
        vtf_frame_data_t::shaderModules.at(depth_frag)->PipelineInfo() 
    };
    create_info.stageCount = 2;
    create_info.pStages = shader_stages;
    create_info.subpass = 0;
    create_info.renderPass = frame.renderPasses.at("DepthAndClusterSamplesPass")->vkHandle();
    create_info.layout = frame.descriptorPack->PipelineLayout(groupName);

    frame.graphicsPipelines["DepthPrePassPipeline"] = std::make_unique<vpr::GraphicsPipeline>(device->vkHandle());
    frame.graphicsPipelines.at("DepthPrePassPipeline")->Init(create_info, vtf_frame_data_t::pipelineCaches.at(groupName)->vkHandle());

}

void createClusterSamplesPipeline(vtf_frame_data_t& frame) {

    auto* device = RenderingContext::Get().Device();
    static const std::string groupName{ "ClusterSamples" };

    const st::ShaderStage& samples_vert = vtf_frame_data_t::groupStages.at(groupName).front();
    const st::ShaderStage& samples_frag = vtf_frame_data_t::groupStages.at(groupName).back();

    vpr::GraphicsPipelineInfo pipeline_info;
    pipeline_info.VertexInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(VertexBindingDescriptions.size());
    pipeline_info.VertexInfo.pVertexBindingDescriptions = VertexBindingDescriptions.data();
    pipeline_info.VertexInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(VertexAttributes.size());
    pipeline_info.VertexInfo.pVertexAttributeDescriptions = VertexAttributes.data();

    pipeline_info.DepthStencilInfo.depthWriteEnable = VK_FALSE;
    pipeline_info.DepthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    pipeline_info.RasterizationInfo.cullMode = VK_CULL_MODE_NONE;

    pipeline_info.DynamicStateInfo.dynamicStateCount = 2;
    static constexpr VkDynamicState States[2]{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    pipeline_info.DynamicStateInfo.pDynamicStates = States;

    pipeline_info.ColorBlendInfo.attachmentCount = 1;
    pipeline_info.ColorBlendInfo.pAttachments = &AdditiveBlendingAttachmentState;
    pipeline_info.ColorBlendInfo.logicOpEnable = VK_FALSE;

    VkGraphicsPipelineCreateInfo create_info = pipeline_info.GetPipelineCreateInfo();
    const VkPipelineShaderStageCreateInfo stages[2]{ 
        vtf_frame_data_t::shaderModules.at(samples_vert)->PipelineInfo(),
        vtf_frame_data_t::shaderModules.at(samples_frag)->PipelineInfo()
    };
    create_info.stageCount = 2;
    create_info.pStages = stages;
    create_info.subpass = 0;
    create_info.layout = frame.descriptorPack->PipelineLayout(groupName);
    create_info.renderPass = frame.renderPasses.at("DepthAndClusterSamplesPass")->vkHandle();

    frame.graphicsPipelines["ClusterSamplesPipeline"] = std::make_unique<vpr::GraphicsPipeline>(device->vkHandle());
    frame.graphicsPipelines.at("ClusterSamplesPipeline")->Init(create_info, vtf_frame_data_t::pipelineCaches.at(groupName)->vkHandle());

}

void createDrawPipelines(vtf_frame_data_t& frame) {
    static const std::string groupName{ "DrawPass" };

    const st::ShaderStage& draw_vert = vtf_frame_data_t::groupStages.at(groupName).front();
    const st::ShaderStage& draw_frag = vtf_frame_data_t::groupStages.at(groupName).back();
    const VkPipelineShaderStageCreateInfo stages[2]{
        vtf_frame_data_t::shaderModules.at(draw_vert)->PipelineInfo(),
        vtf_frame_data_t::shaderModules.at(draw_frag)->PipelineInfo()
    };

    vpr::GraphicsPipelineInfo pipeline_info;

    pipeline_info.VertexInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(VertexBindingDescriptions.size());
    pipeline_info.VertexInfo.pVertexBindingDescriptions = VertexBindingDescriptions.data();
    pipeline_info.VertexInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(VertexAttributes.size());
    pipeline_info.VertexInfo.pVertexAttributeDescriptions = VertexAttributes.data();

    pipeline_info.DynamicStateInfo.dynamicStateCount = 2;
    static constexpr VkDynamicState States[2]{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    pipeline_info.DynamicStateInfo.pDynamicStates = States;

    pipeline_info.DepthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkGraphicsPipelineCreateInfo opaque_info = pipeline_info.GetPipelineCreateInfo();
    throw std::runtime_error("You didn't finish implementing this!");

}

void CreateGraphicsPipelines(vtf_frame_data_t & frame) {
    createDepthPrePassPipeline(frame);
    createClusterSamplesPipeline(frame);
    createDrawPipelines(frame);
}

void ComputeUpdateLights(vtf_frame_data_t& frame_data) {
    auto& rsrc = ResourceContext::Get();
    // update light counts
    frame_data.LightCounts.NumPointLights = static_cast<uint32_t>(SceneLightsState.PointLights.size());
    frame_data.LightCounts.NumSpotLights = static_cast<uint32_t>(SceneLightsState.SpotLights.size());
    frame_data.LightCounts.NumDirectionalLights = static_cast<uint32_t>(SceneLightsState.DirectionalLights.size());
    VulkanResource* light_counts_buffer = frame_data.rsrcMap["lightCounts"];
    const gpu_resource_data_t lcb_update{
        &frame_data.LightCounts,
        sizeof(LightCountsData)
    };
    rsrc.SetBufferData(light_counts_buffer, 1, &lcb_update);
    // update light positions etc
    auto cmd = frame_data.computePool->GetCmdBuffer(0u);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, frame_data.computePipelines["UpdateLightsPipeline"].Handle);
    //resourcePack->BindGroupSets(cmd, "UpdateLights", VK_PIPELINE_BIND_POINT_COMPUTE);
    uint32_t num_groups_x = glm::max(frame_data.LightCounts.NumPointLights, glm::max(frame_data.LightCounts.NumDirectionalLights, frame_data.LightCounts.NumSpotLights));
    num_groups_x = static_cast<uint32_t>(glm::ceil(num_groups_x / 1024.0f));
    vkCmdDispatch(cmd, num_groups_x, 1, 1);
}

void ComputeReduceLights(vtf_frame_data_t & frame_data) {
    auto& rsrc = ResourceContext::Get();

    Descriptor* descr = frame_data.descriptorPack->RetrieveDescriptor("SortResources");
    const size_t dispatch_params_idx = descr->BindingLocation("DispatchParams");

    constexpr static VkBufferCreateInfo dispatch_params_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        sizeof(DispatchParams_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0,
        nullptr
    };

    // update dispatch params
    uint32_t num_thread_groups = glm::min<uint32_t>(static_cast<uint32_t>(glm::ceil(glm::max(frame_data.LightCounts.NumPointLights, frame_data.LightCounts.NumSpotLights) / 512.0f)), uint32_t(512));
    frame_data.DispatchParams.NumThreadGroups = glm::uvec3(num_thread_groups, 1u, 1u);
    frame_data.DispatchParams.NumThreads = glm::uvec3(num_thread_groups * 512, 1u, 1u);
    const gpu_resource_data_t dp_update{
        &frame_data.DispatchParams,
        sizeof(DispatchParams_t)
    };

    auto& dispatch_params0 = frame_data.rsrcMap["DispatchParams0"];
    if (dispatch_params0) {
        rsrc.SetBufferData(dispatch_params0, 1, &dp_update);
    }
    else {
        dispatch_params0 = rsrc.CreateBuffer(&dispatch_params_info, nullptr, 1, &dp_update, memory_type::HOST_VISIBLE, nullptr);
        // create binder
        auto iter = frame_data.binders.emplace("ReduceLights0", frame_data.descriptorPack->RetrieveBinder("ReduceLights"));
        iter.first->second.BindResourceToIdx("SortResources", dispatch_params_idx, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, dispatch_params0);
        iter.first->second.Update();
    }

    frame_data.DispatchParams.NumThreadGroups = glm::uvec3{ 1u, 1u, 1u };
    frame_data.DispatchParams.NumThreads = glm::uvec3{ 512u, 1u, 1u };
    auto& dispatch_params1 = frame_data.rsrcMap["DispatchParams1"];
    if (dispatch_params1) {
        rsrc.SetBufferData(dispatch_params1, 1, &dp_update);
    }
    else {
        dispatch_params1 = rsrc.CreateBuffer(&dispatch_params_info, nullptr, 1, &dp_update, memory_type::HOST_VISIBLE, nullptr);
        // create binder by copying from sibling binder
        auto iter = frame_data.binders.emplace("ReduceLights1", frame_data.binders.at("ReduceLights0"));
        iter.first->second.BindResourceToIdx("SortResources", dispatch_params_idx, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, dispatch_params1);
        iter.first->second.Update();
    }

    // Reduce lights
    auto cmd = frame_data.computePool->GetCmdBuffer(0);
    frame_data.binders.at("ReduceLights0").Bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, frame_data.computePipelines["ReduceLightsAABB0"].Handle);
    vkCmdDispatch(cmd, num_thread_groups, 1u, 1u);
    // second step of reduction
    frame_data.binders.at("ReduceLights1").BindSingle(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, "SortResources");
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, frame_data.computePipelines["ReduceLightsAABB1"].Handle);
    vkCmdDispatch(cmd, 1u, 1u, 1u);

}

void ComputeAndSortMortonCodes(vtf_frame_data_t& frame) {
    /*
        Future optimizations for this particular step:
        1. Use two separate submits for this one, one per light type (potentially one per queue!)
        2. Further, record those submissions ahead of time and save them.
        3. Then, use an indirect buffer written to by a shader called before them
           to set if they're even dispatched at all. This shader will just check
           the light counts buffer, meaning we can remove almost all host-side
           work for this step AND parallelize it

    */

    auto& rsrc = ResourceContext::Get();
    auto cmd = frame.computePool->GetCmdBuffer(0);
    auto binder = frame.descriptorPack->RetrieveBinder("ComputeMortonCodes");

    auto& pointLightIndices = frame["PointLightIndices"];
    auto& spotLightIndices = frame["SpotLightIndices"];
    auto& pointLightMortonCodes = frame["PointLightMortonCodes"];
    auto& spotLightMortonCodes = frame["SpotLightMortonCodes"];
    auto& pointLightMortonCodes_OUT = frame["PointLightMortonCodes_OUT"];
    auto& pointLightIndices_OUT = frame["PointLightIndices_OUT"];
    auto& spotLightMortonCodes_OUT = frame["SpotLightMortonCodes_OUT"];
    auto& spotLightIndices_OUT = frame["SpotLightIndices_OUT"];

    binder.Bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, frame.computePipelines["ComputeLightMortonCodesPipeline"].Handle);
    uint32_t num_thread_groups = static_cast<uint32_t>(glm::ceil(glm::max(frame.LightCounts.NumPointLights, frame.LightCounts.NumSpotLights) / 1024.0f));
    vkCmdDispatch(cmd, num_thread_groups, 1, 1);

    SortParams sort_params;
    sort_params.ChunkSize = SORT_NUM_THREADS_PER_THREAD_GROUP;
    const VkBufferCopy point_light_copy{ 0, 0, uint32_t(reinterpret_cast<const VkBufferCreateInfo*>(pointLightIndices->Info)->size) };
    const VkBufferCopy spot_light_copy{ 0, 0, uint32_t(reinterpret_cast<const VkBufferCreateInfo*>(spotLightIndices->Info)->size) };

    auto& sort_params_rsrc = frame["SortParams"];
    const gpu_resource_data_t sort_params_copy{ &sort_params, sizeof(SortParams), 0u, 0u, 0u };

    // prefetch binding locations
    Descriptor* sort_descriptor = frame.descriptorPack->RetrieveDescriptor("MergeSortResources");
    const size_t src_keys_loc = sort_descriptor->BindingLocation("InputKeys");
    const size_t dst_keys_loc = sort_descriptor->BindingLocation("OutputKeys");
    const size_t src_values_loc = sort_descriptor->BindingLocation("InputValues");
    const size_t dst_values_loc = sort_descriptor->BindingLocation("OutputValues");

    // bind radix sort pipeline now
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, frame.computePipelines["RadixSortPipeline"].Handle);

    if (frame.LightCounts.NumPointLights > 0u) {

        sort_params.NumElements = frame.LightCounts.NumPointLights;
        rsrc.SetBufferData(sort_params_rsrc, 1, &sort_params_copy);
        // bind proper resources to the descriptor
        binder.BindResourceToIdx("MergeSortResources", src_keys_loc, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, pointLightMortonCodes);
        binder.BindResourceToIdx("MergeSortResources", dst_keys_loc, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, pointLightMortonCodes_OUT);
        binder.BindResourceToIdx("MergeSortResources", src_values_loc, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, pointLightIndices);
        binder.BindResourceToIdx("MergeSortResources", dst_values_loc, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, pointLightIndices_OUT);
        binder.Update();

        // bind and dispatch
        binder.Bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
        uint32_t num_thread_groups = static_cast<uint32_t>(glm::ceil(float(frame.LightCounts.NumPointLights) / float(SORT_NUM_THREADS_PER_THREAD_GROUP)));
        vkCmdDispatch(cmd, num_thread_groups, 1u, 1u);

        // copy resources back into results
        vkCmdCopyBuffer(cmd, (VkBuffer)pointLightMortonCodes_OUT->Handle, (VkBuffer)pointLightMortonCodes->Handle, 1, &point_light_copy);
        vkCmdCopyBuffer(cmd, (VkBuffer)pointLightIndices_OUT->Handle, (VkBuffer)pointLightIndices->Handle, 1, &point_light_copy);
        vkEndCommandBuffer(cmd);
    }

    if (frame.LightCounts.NumSpotLights > 0u) {

        sort_params.NumElements = frame.LightCounts.NumSpotLights;
        rsrc.SetBufferData(sort_params_rsrc, 1, &sort_params_copy);

        // update bindings again
        binder.BindResourceToIdx("MergeSortResources", src_keys_loc, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, spotLightMortonCodes);
        binder.BindResourceToIdx("MergeSortResources", dst_keys_loc, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, spotLightMortonCodes_OUT);
        binder.BindResourceToIdx("MergeSortResources", src_values_loc, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, spotLightIndices);
        binder.BindResourceToIdx("MergeSortResources", dst_values_loc, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, spotLightIndices_OUT);
        binder.Update();
        
        // re-bind, but only the single mutated set
        binder.BindSingle(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, "MergeSortResources");

        uint32_t num_thread_groups = static_cast<uint32_t>(glm::ceil(float(frame.LightCounts.NumSpotLights) / float(SORT_NUM_THREADS_PER_THREAD_GROUP)));
        vkCmdDispatch(cmd, num_thread_groups, 1u, 1u);

        vkCmdCopyBuffer(cmd, (VkBuffer)spotLightMortonCodes_OUT->Handle, (VkBuffer)spotLightMortonCodes->Handle, 1, &spot_light_copy);
        vkCmdCopyBuffer(cmd, (VkBuffer)spotLightIndices_OUT->Handle, (VkBuffer)spotLightIndices->Handle, 1, &spot_light_copy);
        vkEndCommandBuffer(cmd);
    }

    if (frame.LightCounts.NumPointLights > 0u) {
        MergeSort(frame, cmd, pointLightMortonCodes, pointLightIndices, pointLightMortonCodes_OUT, pointLightIndices_OUT, frame.LightCounts.NumPointLights, SORT_NUM_THREADS_PER_THREAD_GROUP, binder);
    }

    if (frame.LightCounts.NumSpotLights > 0u) {
        MergeSort(frame, cmd, spotLightMortonCodes, spotLightIndices, spotLightMortonCodes_OUT, spotLightIndices_OUT, frame.LightCounts.NumSpotLights, SORT_NUM_THREADS_PER_THREAD_GROUP, binder);
    }

}

void BuildLightBVH(vtf_frame_data_t& frame) {
    const uint32_t compute_idx = RenderingContext::Get().Device()->QueueFamilyIndices().Compute;
    
    auto& rsrc = ResourceContext::Get();
    auto cmd = frame.computePool->GetCmdBuffer(0);
    auto& bvh_params_rsrc = frame["BVHParams"];
    auto& point_light_bvh = frame["PointLightBVH"];
    auto& spot_light_bvh = frame["SpotLightBVH"];

    const VkBufferMemoryBarrier point_light_bvh_barrier{
        VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        nullptr,
        VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        compute_idx,
        compute_idx,
        (VkBuffer)point_light_bvh->Handle,
        0,
        VK_WHOLE_SIZE
    };

    const VkBufferMemoryBarrier spot_light_bvh_barrier{
        VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        nullptr,
        VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        compute_idx,
        compute_idx,
        (VkBuffer)spot_light_bvh->Handle,
        0,
        VK_WHOLE_SIZE
    };

    const VkBufferMemoryBarrier bvh_params_barrier{
        VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        nullptr,
        VK_ACCESS_HOST_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        compute_idx,
        compute_idx,
        (VkBuffer)bvh_params_rsrc->Handle,
        0u,
        VK_WHOLE_SIZE
    };

    vkCmdFillBuffer(cmd, (VkBuffer)point_light_bvh->Handle, 0u, VK_WHOLE_SIZE, 0u);
    vkCmdFillBuffer(cmd, (VkBuffer)spot_light_bvh->Handle, 0u, VK_WHOLE_SIZE, 0u);

    uint32_t point_light_levels = GetNumLevelsBVH(frame.LightCounts.NumPointLights);
    uint32_t spot_light_levels = GetNumLevelsBVH(frame.LightCounts.NumSpotLights);
    const gpu_resource_data_t bvh_update{ &frame.BVH_Params, sizeof(frame.BVH_Params), 0u, 0u, 0u };

    if ((point_light_levels != frame.BVH_Params.PointLightLevels) || (spot_light_levels != frame.BVH_Params.SpotLightLevels)) {
        rsrc.SetBufferData(bvh_params_rsrc, 1, &bvh_update);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, frame.computePipelines["BuildBVHBottomPipeline"].Handle);

    auto binder = frame.descriptorPack->RetrieveBinder("BuildBVH");
    binder.Bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);

    uint32_t max_leaves = glm::max(frame.LightCounts.NumPointLights, frame.LightCounts.NumSpotLights);
    uint32_t num_thread_groups = static_cast<uint32_t>(glm::ceil(float(max_leaves) / float(BVH_NUM_THREADS)));
    vkCmdDispatch(cmd, num_thread_groups, 1u, 1u);

    uint32_t max_levels = glm::max(frame.BVH_Params.PointLightLevels, frame.BVH_Params.SpotLightLevels);
    if (max_levels > 0u) {

        // Won't we need to barrier things more thoroughly? Track this frame in renderdoc!
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, frame.computePipelines["BuildBVHTopPipeline"].Handle);
        for (uint32_t level = max_levels - 1u; level > 0u; --level) {
            frame.BVH_Params.ChildLevel = level;
            rsrc.SetBufferData(bvh_params_rsrc, 1, &bvh_update);
            // need to ensure writes to bvh_params are visible
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &bvh_params_barrier, 0, nullptr);
            // make sure previous shader finishes and writes are completed before beginning next
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &point_light_bvh_barrier, 0, nullptr);
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &spot_light_bvh_barrier, 0, nullptr);
            uint32_t num_child_nodes = NumLevelNodes[level];
            num_thread_groups = static_cast<uint32_t>(glm::ceil(float(NumLevelNodes[level]) / float(BVH_NUM_THREADS)));
            vkCmdDispatch(cmd, num_thread_groups, 1u, 1u);
        }
    }

}

void UpdateClusterGrid(vtf_frame_data_t& frame) {
    auto& camera = PerspectiveCamera::Get();

    float fov_y = camera.FOV();
    float z_near = camera.NearPlane();
    float z_far = camera.FarPlane();
    frame.Globals.depthRange = glm::vec2(z_near, z_far);

    auto& ctxt = RenderingContext::Get();
    const uint32_t window_width = ctxt.Swapchain()->Extent().width;
    const uint32_t window_height = ctxt.Swapchain()->Extent().height;
    frame.Globals.windowSize = glm::vec2(window_width, window_height);

    uint32_t cluster_dim_x = static_cast<uint32_t>(glm::ceil(window_width / float(CLUSTER_GRID_BLOCK_SIZE)));
    uint32_t cluster_dim_y = static_cast<uint32_t>(glm::ceil(window_height / float(CLUSTER_GRID_BLOCK_SIZE)));

    float sD = 2.0f * glm::tan(fov_y) / float(cluster_dim_y);
    float log_dim_y = 1.0f / glm::log(1.0f + sD);
    float log_depth = glm::log(z_far / z_near);

    uint32_t cluster_dim_z = static_cast<uint32_t>(glm::floor(log_depth * log_dim_y));
    const uint32_t CLUSTER_SIZE = cluster_dim_x * cluster_dim_y * cluster_dim_z;
    const uint32_t LIGHT_INDEX_LIST_SIZE = cluster_dim_x * cluster_dim_y * AVERAGE_LIGHTS_PER_TILE;

    frame.ClusterData.GridDim = glm::uvec3{ cluster_dim_x, cluster_dim_y, cluster_dim_z };
    frame.ClusterData.ViewNear = z_near;
    frame.ClusterData.ScreenSize = glm::uvec2{ CLUSTER_GRID_BLOCK_SIZE, CLUSTER_GRID_BLOCK_SIZE };
    frame.ClusterData.NearK = 1.0f + sD;
    frame.ClusterData.LogGridDimY = log_dim_y;

    auto& rsrc_context = ResourceContext::Get();
    constexpr static VkBufferCreateInfo cluster_data_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        sizeof(ClusterData_t),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0,
        nullptr
    };

    const gpu_resource_data_t cluster_data_update{
        &frame.ClusterData,
        sizeof(frame.ClusterData),
        0u,
        0u,
        0u
    };

    auto& cluster_data = frame["ClusterData"];
    if (!cluster_data) {
        cluster_data = rsrc_context.CreateBuffer(&cluster_data_info, nullptr, 1, &cluster_data_update, memory_type::HOST_VISIBLE_AND_COHERENT, nullptr);
    }
    else {
        rsrc_context.SetBufferData(cluster_data, 1, &cluster_data_update);
    }

    const VkBufferCreateInfo cluster_flags_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        sizeof(uint32_t) * cluster_dim_x * cluster_dim_y * cluster_dim_z,
        VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0,
        nullptr
    };

    const VkBufferViewCreateInfo cluster_flags_view_info{
        VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
        nullptr,
        0,
        VK_NULL_HANDLE,
        VK_FORMAT_R32_UINT,
        0,
        cluster_flags_info.size
    };

    auto& cluster_flags = frame["ClusterFlags"];
    auto& unique_clusters = frame["UniqueClusters"];
    auto& prev_unique_clusters = frame["PreviousUniqueClusters"];

    if (cluster_flags) {
        rsrc_context.DestroyResource(cluster_flags);
    }
    cluster_flags = rsrc_context.CreateBuffer(&cluster_flags_info, &cluster_flags_view_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr);

    if (unique_clusters) {
        rsrc_context.DestroyResource(unique_clusters);
    }
    unique_clusters = rsrc_context.CreateBuffer(&cluster_flags_info, &cluster_flags_view_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr);

    if (prev_unique_clusters) {
        rsrc_context.DestroyResource(prev_unique_clusters);
    }
    prev_unique_clusters = rsrc_context.CreateBuffer(&cluster_flags_info, &cluster_flags_view_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr);

    frame.updateUniqueClusters = true;

    const VkBufferCreateInfo indir_args_buffer{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        sizeof(VkDispatchIndirectCommand),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0,
        nullptr
    };

    auto& assign_lights_args_buffer = frame["AssignLightsToClustersArgumentBuffer"];
    if (assign_lights_args_buffer) {
        rsrc_context.DestroyResource(assign_lights_args_buffer);
    }
    assign_lights_args_buffer = rsrc_context.CreateBuffer(&indir_args_buffer, nullptr, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr);

    constexpr static VkBufferCreateInfo debug_indirect_draw_buffer_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        sizeof(VkDrawIndexedIndirectCommand),
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0,
        nullptr
    };

    auto& debug_clusters_indir_draw_buffer = frame["DebugClustersDrawIndirectArgumentBuffer"];
    if (debug_clusters_indir_draw_buffer) {
        rsrc_context.DestroyResource(debug_clusters_indir_draw_buffer);
    }
    debug_clusters_indir_draw_buffer  = rsrc_context.CreateBuffer(&debug_indirect_draw_buffer_info, nullptr, 0, nullptr, memory_type::HOST_VISIBLE, nullptr);

    const VkBufferCreateInfo cluster_aabbs_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        CLUSTER_SIZE * sizeof(AABB),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0,
        nullptr
    };

    auto& cluster_aabbs = frame["ClusterAABBs"];
    if (cluster_aabbs) {
        rsrc_context.DestroyResource(cluster_aabbs);
    }
    cluster_aabbs = rsrc_context.CreateBuffer(&cluster_aabbs_info, nullptr, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr);

    const VkBufferCreateInfo point_light_grid_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        CLUSTER_SIZE * sizeof(uint32_t) * 2,
        VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0,
        nullptr
    };

    const VkBufferViewCreateInfo point_light_grid_view_info{
        VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
        nullptr,
        0,
        VK_NULL_HANDLE,
        VK_FORMAT_R32G32_UINT,
        0,
        point_light_grid_info.size
    };

    auto& point_light_grid = frame["PointLightGrid"];
    if (point_light_grid) {
        rsrc_context.DestroyResource(point_light_grid);
    }
    point_light_grid = rsrc_context.CreateBuffer(&point_light_grid_info, &point_light_grid_view_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr);

    auto& spot_light_grid = frame["SpotLightGrid"];
    if (spot_light_grid) {
        rsrc_context.DestroyResource(spot_light_grid);
    }
    spot_light_grid = rsrc_context.CreateBuffer(&point_light_grid_info, &point_light_grid_view_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr);

    const VkBufferCreateInfo indices_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        LIGHT_INDEX_LIST_SIZE * sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0,
        nullptr
    };

    const VkBufferViewCreateInfo indices_view_info{
        VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
        nullptr,
        0,
        VK_NULL_HANDLE,
        VK_FORMAT_R32_UINT,
        0,
        indices_info.size
    };

    auto& point_light_idx_list = frame["PointLightIndexList"];
    if (point_light_idx_list) {
        rsrc_context.DestroyResource(point_light_idx_list);
    }
    point_light_idx_list = rsrc_context.CreateBuffer(&indices_info, &indices_view_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr);

    auto& spot_light_index_list = frame["SpotLightIndexList"];
    if (spot_light_index_list) {
        rsrc_context.DestroyResource(spot_light_index_list);
    }
    spot_light_index_list = rsrc_context.CreateBuffer(&indices_info, &indices_view_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr);

    ComputeClusterAABBs(frame);
}

void ComputeClusterAABBs(vtf_frame_data_t& frame) {

    auto* device = RenderingContext::Get().Device();

    constexpr static VkCommandBufferBeginInfo compute_begin_info{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        nullptr,
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        nullptr
    };

    VkCommandBuffer cmd_buffer = frame.computePool->GetCmdBuffer(0u);

    vkBeginCommandBuffer(cmd_buffer, &compute_begin_info);
    auto& binder = frame.GetBinder("ComputeClusterAABBs");
    binder.Bind(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE);
    vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, frame.computePipelines["ComputeClusterAABBsPipeline"].Handle);
    const uint32_t dispatch_size = static_cast<uint32_t>(glm::ceil((frame.ClusterData.GridDim.x * frame.ClusterData.GridDim.y * frame.ClusterData.GridDim.z) / 1024.0f));
    vkCmdDispatch(cmd_buffer, dispatch_size, 1u, 1u);
    vkEndCommandBuffer(cmd_buffer);

    const VkSubmitInfo submit_info{
        VK_STRUCTURE_TYPE_SUBMIT_INFO,
        nullptr,
        0u,
        nullptr,
        0u,
        1u,
        &cmd_buffer,
        0u,
        nullptr
    };

    VkResult result = vkQueueSubmit(device->ComputeQueue(), 1, &submit_info, frame.computeAABBsFence->vkHandle());
    VkAssert(result);

    result = vkWaitForFences(device->vkHandle(), 1, &frame.computeAABBsFence->vkHandle(), VK_TRUE, UINT_MAX);
    VkAssert(result);

    result = vkResetFences(device->vkHandle(), 1, &frame.computeAABBsFence->vkHandle());
    VkAssert(result);

    frame.computePool->ResetCmdPool();
}

void MergeSort(vtf_frame_data_t& frame, VkCommandBuffer cmd, VulkanResource* src_keys, VulkanResource* src_values, VulkanResource* dst_keys, VulkanResource* dst_values,
    uint32_t total_values, uint32_t chunk_size, DescriptorBinder dscr_binder) {
    SortParams params;

    // Create a new mergePathPartitions for this invocation.
    VulkanResource* merge_path_partitions{ nullptr };
    VulkanResource* sort_params_rsrc{ nullptr };

    const uint32_t compute_idx = RenderingContext::Get().Device()->QueueFamilyIndices().Compute;
    constexpr static uint32_t num_values_per_thread_group = SORT_NUM_THREADS_PER_THREAD_GROUP * SORT_ELEMENTS_PER_THREAD;
    uint32_t num_chunks = static_cast<uint32_t>(glm::ceil(total_values / static_cast<float>(chunk_size)));
    uint32_t pass = 0;

    Descriptor* descriptor = frame.descriptorPack->RetrieveDescriptor("MergeSort");
    const size_t merge_pp_loc = descriptor->BindingLocation("MergePathPartitions");
    const size_t sort_params_loc = descriptor->BindingLocation("SortParams");
    const size_t input_keys_loc = descriptor->BindingLocation("InputKeys");
    const size_t output_keys_loc = descriptor->BindingLocation("OutputKeys");
    const size_t input_values_loc = descriptor->BindingLocation("InputValues");
    const size_t output_values_loc = descriptor->BindingLocation("OutputValues");

    dscr_binder.BindResourceToIdx("MergeSort", input_keys_loc, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, src_keys);
    dscr_binder.BindResourceToIdx("MergeSort", output_keys_loc, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, dst_keys);
    dscr_binder.BindResourceToIdx("MergeSort", input_values_loc, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, src_values);
    dscr_binder.BindResourceToIdx("MergeSort", output_values_loc, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, dst_values);
    dscr_binder.BindResourceToIdx("MergeSort", merge_pp_loc, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, merge_path_partitions);
    dscr_binder.BindResourceToIdx("MergeSort", sort_params_loc, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, sort_params_rsrc);
    dscr_binder.Update();

    while (num_chunks > 1) {

        ++pass;

        params.NumElements = total_values;
        params.ChunkSize = chunk_size;

        uint32_t num_sort_groups = num_chunks / 2u;
        uint32_t num_thread_groups_per_sort_group = static_cast<uint32_t>(glm::ceil((chunk_size * 2) / static_cast<float>(num_values_per_thread_group)));

        {

            // Clear buffer
            vkCmdFillBuffer(cmd, (VkBuffer)merge_path_partitions->Handle, 0, VK_WHOLE_SIZE, 0);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, frame.computePipelines["MergePathPartitionsPipeline"].Handle);
            dscr_binder.Bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
            uint32_t num_merge_path_partitions_per_sort_group = num_thread_groups_per_sort_group + 1u;
            uint32_t total_merge_path_partitions = num_merge_path_partitions_per_sort_group * num_sort_groups;
            uint32_t num_thread_groups = static_cast<uint32_t>(glm::ceil(float(total_merge_path_partitions) / float(SORT_NUM_THREADS_PER_THREAD_GROUP)));
            vkCmdDispatch(cmd, num_thread_groups, 1, 1);
            const VkBufferMemoryBarrier path_partitions_barrier{
                VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                nullptr,
                VK_ACCESS_SHADER_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT,
                compute_idx,
                compute_idx,
                (VkBuffer)merge_path_partitions->Handle,
                0,
                VK_WHOLE_SIZE
            };
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &path_partitions_barrier, 0, nullptr);
        }

        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, frame.computePipelines["MergeSortPipeline"].Handle);
            uint32_t num_values_per_sort_group = glm::min(chunk_size * 2u, total_values);
            num_thread_groups_per_sort_group = static_cast<uint32_t>(glm::ceil(float(num_values_per_sort_group) / float(num_values_per_thread_group)));
            vkCmdDispatch(cmd, num_thread_groups_per_sort_group * num_sort_groups, 1, 1);
        }

        // ping-pong the buffers

        dscr_binder.BindResourceToIdx("MergeSort", input_keys_loc, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, dst_keys);
        dscr_binder.BindResourceToIdx("MergeSort", output_keys_loc, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, src_keys);
        dscr_binder.BindResourceToIdx("MergeSort", input_values_loc, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, dst_values);
        dscr_binder.BindResourceToIdx("MergeSort", output_values_loc, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, src_values);
        dscr_binder.Update();

        chunk_size *= 2u;
        num_chunks = static_cast<uint32_t>(glm::ceil(float(total_values) / float(chunk_size)));
    }

    if (pass % 2u == 0u) {
        // if the pass count is odd then we have to copy the results into 
        // where they should actually be

        const VkBufferCopy copy{ 0, 0, VK_WHOLE_SIZE };
        vkCmdCopyBuffer(cmd, (VkBuffer)dst_keys->Handle, (VkBuffer)src_keys->Handle, 1, &copy);
        vkCmdCopyBuffer(cmd, (VkBuffer)dst_values->Handle, (VkBuffer)src_values->Handle, 1, &copy);
    }

}
