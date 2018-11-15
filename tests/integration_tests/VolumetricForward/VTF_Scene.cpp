#include "VTF_Scene.hpp"
#include "GraphicsPipeline.hpp"
#include "ShaderModule.hpp"
#include "AllocationRequirements.hpp"
#include "CommandPool.hpp"
#include "Renderpass.hpp"
#include "ResourceContext.hpp"
#include "RenderingContext.hpp"
#include "ResourceTypes.hpp"
#include "DescriptorPool.hpp"
#include "DescriptorSet.hpp"
#include "DescriptorSetLayout.hpp"
#include "PipelineLayout.hpp"
#include "PipelineCache.hpp"
#include "LogicalDevice.hpp"
#include "PhysicalDevice.hpp"
#include "Descriptor.hpp"
#include "Swapchain.hpp"
#include "Instance.hpp"
#include "VkDebugUtils.hpp"
#include "CreateInfoBase.hpp"
#include "vulkan/vulkan.h"
#include "glm/gtc/random.hpp"
#include "core/ShaderPack.hpp"
#include "core/ShaderResource.hpp"
#include "core/Shader.hpp"
#include "core/ResourceGroup.hpp"
#include "core/ResourceUsage.hpp"
#include "ShaderResourcePack.hpp"

constexpr static uint32_t DEFAULT_MAX_LIGHTS = 2048u;
const st::ShaderPack* vtfShaders{ nullptr };
std::unique_ptr<ShaderResourcePack> resourcePack{ nullptr };

constexpr static uint32_t SORT_NUM_THREADS_PER_THREAD_GROUP = 256u;
constexpr static uint32_t SORT_ELEMENTS_PER_THREAD = 8u;
constexpr static uint32_t BVH_NUM_THREADS = 32u * 16u;
constexpr static uint32_t AVERAGE_OVERLAPPING_LIGHTS_PER_CLUSTER = 20u;

struct alignas(16) Matrices_t {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 inverseView;
    glm::mat4 projection;
    glm::mat4 modelView;
    glm::mat4 modelViewProjection;
    glm::mat4 inverseTransposeModel;
    glm::mat4 inverseTransposeModelView;
} MatricesDefault;

struct alignas(16) GlobalsData {
    glm::vec4 viewPosition;
    glm::vec2 mousePosition;
    glm::vec2 windowSize;
    glm::vec2 depthRange;
    uint32_t frame;
    float exposure;
    float gamma;
    float brightness;
} Globals;

struct alignas(16) ClusterData {
    glm::uvec3 GridDim;
    float ViewNear;
    glm::uvec2 ScreenSize;
    float NearK;
    float LogGridDimY;
} ClusterData;

struct alignas(4) DispatchParams_t {
    glm::uvec3 NumThreadGroups{};
    uint32_t Padding0{ 0u };
    glm::uvec3 NumThreads{};
    uint32_t Padding1{ 0u };
} DispatchParams;

struct alignas(4) SortParams {
    uint32_t NumElements;
    uint32_t ChunkSize;
};

struct alignas(16) Frustum {
    glm::vec4 Planes[4];
};

struct alignas(16) AABB {
    glm::vec4 Min;
    glm::vec4 Max;
};

struct alignas(4) LightCountsData {
    uint32_t NumPointLights{ DEFAULT_MAX_LIGHTS };
    uint32_t NumSpotLights{ DEFAULT_MAX_LIGHTS };
    uint32_t NumDirectionalLights{ 4u };
} LightCounts;

void VTF_Scene::MergeSort(VkCommandBuffer cmd, VulkanResource* src_keys, VulkanResource* src_values, VulkanResource* dst_keys, VulkanResource* dst_values,
    uint32_t total_values, uint32_t chunk_size) {
    SortParams params;

    const uint32_t compute_idx = RenderingContext::Get().Device()->QueueFamilyIndices().Compute;
    constexpr static uint32_t num_values_per_thread_group = SORT_NUM_THREADS_PER_THREAD_GROUP * SORT_ELEMENTS_PER_THREAD;
    uint32_t num_chunks = static_cast<uint32_t>(glm::ceil(total_values / static_cast<float>(chunk_size)));
    uint32_t pass = 0;

    const size_t input_keys_loc = resourcePack->BindingLocation("InputKeys");
    const size_t output_keys_loc = resourcePack->BindingLocation("OutputKeys");
    const size_t input_values_loc = resourcePack->BindingLocation("InputValues");
    const size_t output_values_loc = resourcePack->BindingLocation("OutputValues");

    while (num_chunks > 1) {

        ++pass;

        params.NumElements = total_values;
        params.ChunkSize = chunk_size;

        uint32_t num_sort_groups = num_chunks / 2u;
        uint32_t num_thread_groups_per_sort_group = static_cast<uint32_t>(glm::ceil((chunk_size * 2) / static_cast<float>(num_values_per_thread_group)));

        {
            auto* rsrc = resourcePack->Find("MergeSort", "MergePathPartitions");
            // Clear buffer
            vkCmdFillBuffer(cmd, (VkBuffer)rsrc->Handle, 0, VK_WHOLE_SIZE, 0);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mergePathPartitionsPipeline->Handle);
            resourcePack->BindGroupSets(cmd, "MergeSort", VK_PIPELINE_BIND_POINT_COMPUTE);
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
                (VkBuffer)rsrc->Handle,
                0,
                VK_WHOLE_SIZE
            };
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &path_partitions_barrier, 0, nullptr);
        }

        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mergeSortPipeline->Handle);
            uint32_t num_values_per_sort_group = glm::min(chunk_size * 2u, total_values);
            num_thread_groups_per_sort_group = static_cast<uint32_t>(glm::ceil(float(num_values_per_sort_group) / float(num_values_per_thread_group)));
            vkCmdDispatch(cmd, num_thread_groups_per_sort_group * num_sort_groups, 1, 1);
        }

        // ping-pong the buffers
        VulkanResource* input_keys = resourcePack->Find("MergeSort", "InputKeys");
        VulkanResource* output_keys = resourcePack->Find("MergeSort", "OutputKeys");
        VulkanResource* input_vals = resourcePack->Find("MergeSort", "InputValues");
        VulkanResource* output_vals = resourcePack->Find("MergeSort", "OutputValues");

        Descriptor* descriptor = resourcePack->GetDescriptor("MergeSort");
        descriptor->BindResourceToIdx(input_keys_loc, output_keys);
        descriptor->BindResourceToIdx(output_keys_loc, input_keys);
        descriptor->BindResourceToIdx(input_values_loc, output_vals);
        descriptor->BindResourceToIdx(output_values_loc, input_vals);

        chunk_size *= 2u;
        num_chunks = static_cast<uint32_t>(glm::ceil(float(total_values) / float(chunk_size)));
    }

    if (pass % 2u == 0u) {
        // if the pass count is odd then we have to copy the results into 
        // where they should actually be
        VulkanResource* input_keys = resourcePack->Find("MergeSort", "InputKeys");
        VulkanResource* output_keys = resourcePack->Find("MergeSort", "OutputKeys");
        VulkanResource* input_vals = resourcePack->Find("MergeSort", "InputValues");
        VulkanResource* output_vals = resourcePack->Find("MergeSort", "OutputValues");

        const VkBufferCopy copy{ 0, 0, VK_WHOLE_SIZE };
        vkCmdCopyBuffer(cmd, (VkBuffer)output_keys->Handle, (VkBuffer)input_keys->Handle, 1, &copy);
        vkCmdCopyBuffer(cmd, (VkBuffer)output_vals->Handle, (VkBuffer)input_vals->Handle, 1, &copy);
    }

}

glm::vec3 HSV_to_RGB(float H, float S, float V) {
    float C = V * S;
    float m = V - C;
    float H2 = H / 60.0f;
    float X = C * (1.0f - std::fabsf(std::fmodf(H2, 2.0f) - 1.0f));
    glm::vec3 RGB;
    switch (int(H2)) {
    case 0:
        RGB = { C, X, 0.0f };
        break;
    case 1:
        RGB = { X, C, 0.0f };
        break;
    case 2:
        RGB = { 0.0f, C, X };
        break;
    case 3:
        RGB = { 0.0f, X, C };
        break;
    case 4:
        RGB = { X, 0.0f, C };
        break;
    case 5:
        RGB = { C, 0.0f, X };
        break;
    default:
        throw std::domain_error("Found invalid value for H2 when converting HSV to RGB");
    }

    return RGB + m;
}

static std::vector<glm::vec4> GenerateColors(uint32_t num_lights) {
    std::vector<glm::vec4> colors(num_lights);
    for (auto& color : colors) {
        color = glm::vec4(HSV_to_RGB(glm::linearRand(0.0f, 360.0f), glm::linearRand(0.0f, 1.0f), 1.0f), 1.0f);
    }
    return colors;
}

template<typename LightType>
static LightType GenerateLight(const glm::vec4& position_ws, const glm::vec4& direction_ws, float spot_angle, float range, const glm::vec3& color);

template<>
static PointLight GenerateLight<PointLight>(const glm::vec4& position_ws, const glm::vec4& direction_ws, float spot_angle, float range, const glm::vec3& color) {
    PointLight result{};
    result.positionWS = position_ws;
    result.color = color;
    result.range = range;
    return result;
}

template<>
static SpotLight GenerateLight<SpotLight>(const glm::vec4& position_ws, const glm::vec4& direction_ws, float spot_angle, float range, const glm::vec3& color) {
    SpotLight result{};
    result.positionWS = position_ws;
    result.directionWS = direction_ws;
    result.spotlightAngle = spot_angle;
    result.range = range;
    result.color = color;
    return result;
}

template<>
static DirectionalLight GenerateLight<DirectionalLight>(const glm::vec4& position_ws, const glm::vec4& direction_ws, float spot_angle, float range, const glm::vec3& color) {
    DirectionalLight result{};
    result.directionWS = direction_ws;
    result.color = color;
    return result;
}

template<typename LightType>
static std::vector<LightType> GenerateLights(uint32_t num_lights) {
    std::vector<LightType> lights(num_lights);

    for (auto& light : lights) {
        glm::vec4 position_ws = glm::vec4{ glm::linearRand(SceneConfig.LightsMinBounds, SceneConfig.LightsMaxBounds), 1.0f };
        glm::vec4 direction_ws = glm::vec4{ glm::sphericalRand(1.0f), 0.0f };
        float spot_angle = glm::linearRand(SceneConfig.MinSpotAngle, SceneConfig.MaxSpotAngle);
        float range = glm::linearRand(SceneConfig.MinRange, SceneConfig.MaxRange);
        glm::vec3 color = HSV_to_RGB(glm::linearRand(0.0f, 360.0f), glm::linearRand(0.0f, 1.0f), 1.0f);
        light = GenerateLight<LightType>(position_ws, direction_ws, spot_angle, range, color);
    }

    return lights;
}


struct alignas(16) Cone {
    glm::vec3 T;
    float h;
    glm::vec3 d;
    float r;
};

struct alignas(4) BVH_Params_t {
    uint32_t PointLightLevels{ 0u };
    uint32_t SpotLightLevels{ 0u };
    uint32_t ChildLevel{ 0u };
    uint32_t Padding{ 0u };
} BVH_Params;

// The number of nodes at each level of the BVH.
constexpr static uint32_t NumLevelNodes[6] {
    1,          // 1st level (32^0)
    32,         // 2nd level (32^1)
    1024,       // 3rd level (32^2)
    32768,      // 4th level (32^3)
    1048576,    // 5th level (32^4)
    33554432,   // 6th level (32^5)
};

// The number of nodes required to represent a BVH given the number of levels
// of the BVH.
constexpr static uint32_t NumBVHNodes[6] {
    1,          // 1 level  =32^0
    33,         // 2 levels +32^1
    1057,       // 3 levels +32^2
    33825,      // 4 levels +32^3
    1082401,    // 5 levels +32^4
    34636833,   // 6 levels +32^5
};

struct ComputePipelineState {
    const vpr::Device* Device{ nullptr };
    VkPipeline Handle{ VK_NULL_HANDLE };
    VkComputePipelineCreateInfo CreateInfo{ vpr::vk_compute_pipeline_create_info_base };
    std::shared_ptr<vpr::PipelineLayout> PipelineLayout{ nullptr };

    void Destroy() {
        if (Handle != VK_NULL_HANDLE) {
            vkDestroyPipeline(Device->vkHandle(), Handle, nullptr);
            Handle = VK_NULL_HANDLE;
        }
        PipelineLayout.reset();
    }

};

VTF_Scene& VTF_Scene::Get() {
    static VTF_Scene scene;
    return scene;
}

void VTF_Scene::Construct(RequiredVprObjects objects, void * user_data) {
    vprObjects = objects;
    vtfShaders = reinterpret_cast<const st::ShaderPack*>(user_data);
    resourcePack = std::make_unique<ShaderResourcePack>(nullptr, vtfShaders);
}

void VTF_Scene::Destroy()
{
}

uint32_t VTF_Scene::GetNumLevelsBVH(uint32_t num_leaves) {
    static const float log32f = glm::log(32.0f);

    uint32_t num_levels = 0;
    if (num_leaves > 0) {
        num_levels = static_cast<uint32_t>(glm::ceil(glm::log(num_leaves) / log32f));
    }

    return num_levels;
}

uint32_t VTF_Scene::GetNumNodesBVH(uint32_t num_leaves) {
    uint32_t num_levels = GetNumLevelsBVH(num_leaves);
    uint32_t num_nodes = 0;
    if (num_levels > 0 && num_levels < _countof(NumBVHNodes)) {
        num_nodes = NumBVHNodes[num_levels - 1];
    }
    
    return num_nodes;
}

void VTF_Scene::GenerateSceneLights() {
    State.PointLights = GenerateLights<PointLight>(SceneConfig.MaxLights);
    LightCounts.NumPointLights = static_cast<uint32_t>(State.PointLights.size());
    State.SpotLights = GenerateLights<SpotLight>(SceneConfig.MaxLights);
    LightCounts.NumSpotLights = static_cast<uint32_t>(State.SpotLights.size());
    State.DirectionalLights = GenerateLights<DirectionalLight>(8);
    LightCounts.NumDirectionalLights = static_cast<uint32_t>(State.DirectionalLights.size());
}

void VTF_Scene::update() {

    auto& rsrc = ResourceContext::Get();

    LightCounts.NumPointLights = static_cast<uint32_t>(State.PointLights.size());
    LightCounts.NumSpotLights = static_cast<uint32_t>(State.SpotLights.size());
    LightCounts.NumDirectionalLights = static_cast<uint32_t>(State.DirectionalLights.size());
    VulkanResource* light_counts_buffer = resourcePack->Find("VolumetricForwardLights", "LightCounts");
    const gpu_resource_data_t lcb_update{
        &LightCounts,
        sizeof(LightCounts)
    };
    rsrc.SetBufferData(light_counts_buffer, 1, &lcb_update);

    const vpr::DescriptorSet* lights_descriptor = resourcePack->DescriptorSet("VolumetricForwardLights");

    {
        auto cmd = computePools[0]->GetCmdBuffer(0);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, updateLightsPipeline->Handle);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, updateLightsPipeline->PipelineLayout->vkHandle(), 2, 1, &lights_descriptor->vkHandle(), 0, nullptr);
        uint32_t num_groups_x = glm::max(LightCounts.NumPointLights, glm::max(LightCounts.NumDirectionalLights, LightCounts.NumSpotLights));
        num_groups_x = static_cast<uint32_t>(glm::ceil(num_groups_x / 1024.0f));
        vkCmdDispatch(cmd, num_groups_x, 1, 1);

    }

    {
        // Reduce lights
        auto cmd = computePools[0]->GetCmdBuffer(0);
        uint32_t num_thread_groups = glm::min<uint32_t>(static_cast<uint32_t>(glm::ceil(glm::max(LightCounts.NumPointLights, LightCounts.NumSpotLights) / 512.0f)), uint32_t(512));
        DispatchParams.NumThreadGroups = glm::uvec3(num_thread_groups, 1u, 1u);
        DispatchParams.NumThreads = glm::uvec3(num_thread_groups * 512, 1u, 1u);
        uint32_t num_elements = DispatchParams.NumThreadGroups.x;
        vkCmdBindDescriptorSets()
    }

}

void VTF_Scene::recordCommands()
{
}

void VTF_Scene::draw()
{
}

void VTF_Scene::endFrame()
{
}

void VTF_Scene::createComputePools() {
    const static VkCommandPoolCreateInfo pool_info{
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        nullptr,
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        vprObjects.device->QueueFamilyIndices().Compute
    };
    computePools[0] = std::make_unique<vpr::CommandPool>(vprObjects.device->vkHandle(), pool_info);
    computePools[0]->AllocateCmdBuffers(1, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    computePools[1] = std::make_unique<vpr::CommandPool>(vprObjects.device->vkHandle(), pool_info);
    computePools[1]->AllocateCmdBuffers(1, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
}

void VTF_Scene::createReadbackBuffers() {

}
