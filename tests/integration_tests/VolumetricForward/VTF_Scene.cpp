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
#include "Framebuffer.hpp"
#include "PipelineLayout.hpp"
#include "PipelineCache.hpp"
#include "Semaphore.hpp"
#include "vkAssert.hpp"
#include "LogicalDevice.hpp"
#include "PhysicalDevice.hpp"
#include "Descriptor.hpp"
#include "Swapchain.hpp"
#include "Instance.hpp"
#include "Fence.hpp"
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
#include "PerspectiveCamera.hpp"
#include "material/MaterialParameters.hpp"
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#include <experimental/filesystem>

constexpr static uint32_t DEFAULT_MAX_LIGHTS = 2048u;
const st::ShaderPack* vtfShaders{ nullptr };
std::unique_ptr<ShaderResourcePack> resourcePack{ nullptr };

constexpr static uint32_t SORT_NUM_THREADS_PER_THREAD_GROUP = 256u;
constexpr static uint32_t SORT_ELEMENTS_PER_THREAD = 8u;
constexpr static uint32_t BVH_NUM_THREADS = 32u * 16u;
constexpr static uint32_t AVERAGE_OVERLAPPING_LIGHTS_PER_CLUSTER = 20u;
constexpr static uint32_t LIGHT_GRID_BLOCK_SIZE = 32u;
constexpr static uint32_t CLUSTER_GRID_BLOCK_SIZE = 64u;
constexpr static uint32_t AVERAGE_LIGHTS_PER_TILE = 100u;

constexpr static std::array<VkVertexInputAttributeDescription, 4> VertexAttributes {
    VkVertexInputAttributeDescription{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 },
    VkVertexInputAttributeDescription{ 1, 0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 3 },
    VkVertexInputAttributeDescription{ 2, 0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 6 },
    VkVertexInputAttributeDescription{ 3, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 9 }
};

struct vertex_t {
    vertex_t(glm::vec3 p, glm::vec3 n, glm::vec3 t, glm::vec2 uv) : Position(std::move(p)), Normal(std::move(n)), Tangent(std::move(t)),
        UV(std::move(uv)) {}
    glm::vec3 Position;
    glm::vec3 Normal;
    glm::vec3 Tangent;
    glm::vec2 UV;
};

constexpr static float GOLDEN_RATIO = 1.6180339887498948482045f;
constexpr static float FLOAT_PI = 3.14159265359f;

static const std::array<glm::vec3, 12> ICOSPHERE_INITIAL_VERTICES{
    glm::vec3{-GOLDEN_RATIO, 1.0f, 0.0f },
    glm::vec3{ GOLDEN_RATIO, 1.0f, 0.0f },
    glm::vec3{-GOLDEN_RATIO,-1.0f, 0.0f },
    glm::vec3{ GOLDEN_RATIO,-1.0f, 0.0f },
    glm::vec3{ 0.0f,-GOLDEN_RATIO, 1.0f },
    glm::vec3{ 0.0f, GOLDEN_RATIO, 1.0f },
    glm::vec3{ 0.0f,-GOLDEN_RATIO,-1.0f },
    glm::vec3{ 0.0f, GOLDEN_RATIO,-1.0f },
    glm::vec3{ 1.0f, 0.0f,-GOLDEN_RATIO },
    glm::vec3{ 1.0f, 0.0f, GOLDEN_RATIO },
    glm::vec3{-1.0f, 0.0f,-GOLDEN_RATIO },
    glm::vec3{-1.0f, 0.0f, GOLDEN_RATIO }
};

constexpr static std::array<uint32_t, 60> INITIAL_INDICES{
    0,11, 5,
    0, 5, 1,
    0, 1, 7,
    0, 7,10,
    0,10,11,
    5,11, 4,
    1, 5, 9,
    7, 1, 8,
    10,7, 6,
    11,10,2,
    3, 9, 4,
    3, 4, 2,
    3, 2, 6,
    3, 6, 8,
    3, 8, 9,
    4, 9, 5,
    2, 4,11,
    6, 2,10,
    8, 6, 7,
    9, 8, 1
};

constexpr static std::array<VkVertexInputBindingDescription, 1> VertexBindingDescriptions {
    VkVertexInputBindingDescription{ 0, sizeof(float) * 11, VK_VERTEX_INPUT_RATE_VERTEX }
};

constexpr static VkClearValue DefaultColorClearValue{ 0.1f, 0.1f, 0.15f, 1.0f };
constexpr static VkClearValue DefaultDepthStencilClearValue{ 1.0f, 0 };

constexpr static std::array<const VkClearValue, 2> DepthPrePassAndClusterSamplesClearValues {
    DefaultColorClearValue,
    DefaultDepthStencilClearValue
};

constexpr static std::array<const VkClearValue, 3> DrawPassClearValues{
    DefaultColorClearValue,
    DefaultDepthStencilClearValue,
    DefaultColorClearValue
};

constexpr static VkPipelineColorBlendAttachmentState AdditiveBlendingAttachmentState {
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

struct alignas(16) Matrices_t {
    glm::mat4 model{ glm::identity<glm::mat4>() };
    glm::mat4 view{ glm::identity<glm::mat4>() };
    glm::mat4 inverseView{ glm::identity<glm::mat4>() };
    glm::mat4 projection{ glm::identity<glm::mat4>() };
    glm::mat4 modelView{ glm::identity<glm::mat4>() };
    glm::mat4 modelViewProjection{ glm::identity<glm::mat4>() };
    glm::mat4 inverseTransposeModel{ glm::identity<glm::mat4>() };
    glm::mat4 inverseTransposeModelView{ glm::identity<glm::mat4>() };
} MatricesDefault;

struct alignas(16) GlobalsData {
    glm::vec4 viewPosition{ 0.0f, 0.0f, 0.0f, 0.0f };
    glm::vec2 mousePosition{ 0.0f, 0.0f };
    glm::vec2 windowSize{ 0.0f, 0.0f };
    glm::vec2 depthRange{ 0.0f, 0.0f };
    uint32_t frame{ 0 };
    float exposure{ 0.0f };
    float gamma{ 0.0f };
    float brightness{ 0.0f };
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

struct TestIcosphereMesh {

    void CreateMesh(const size_t detail_level) {

        Indices.assign(std::begin(INITIAL_INDICES), std::end(INITIAL_INDICES));
        Vertices.reserve(ICOSPHERE_INITIAL_VERTICES.size());
        for (const auto& vert : ICOSPHERE_INITIAL_VERTICES) {
            Vertices.emplace_back(vert, vert, glm::vec3(0.0f,0.0f,0.0f), glm::vec2(0.0f,0.0f));
        }

        for (size_t j = 0; j < detail_level; ++j) {
            size_t num_triangles = Indices.size() / 3;
            Indices.reserve(Indices.capacity() + (num_triangles * 9));
            Vertices.reserve(Vertices.capacity() + (num_triangles * 3));
            for (uint32_t i = 0; i < num_triangles; ++i) {
                uint32_t i0 = Indices[i * 3 + 0];
                uint32_t i1 = Indices[i * 3 + 1];
                uint32_t i2 = Indices[i * 3 + 2];

                uint32_t i3 = static_cast<uint32_t>(Vertices.size());
                uint32_t i4 = i3 + 1;
                uint32_t i5 = i4 + 1;

                Indices[i * 3 + 1] = i3;
                Indices[i * 3 + 2] = i5;

                Indices.insert(Indices.cend(), { i3, i1, i4, i5, i3, i4, i5, i4, i2 });

                glm::vec3 midpoint0 = 0.5f * (Vertices[i0].Position + Vertices[i1].Position);
                glm::vec3 midpoint1 = 0.5f * (Vertices[i1].Position + Vertices[i2].Position);
                glm::vec3 midpoint2 = 0.5f * (Vertices[i2].Position + Vertices[i0].Position);

                Vertices.emplace_back(midpoint0, midpoint0, glm::vec3(0.0f,0.0f,0.0f), glm::vec2(0.0f,0.0f));
                Vertices.emplace_back(midpoint1, midpoint1, glm::vec3(0.0f,0.0f,0.0f), glm::vec2(0.0f,0.0f));
                Vertices.emplace_back(midpoint2, midpoint2, glm::vec3(0.0f,0.0f,0.0f), glm::vec2(0.0f,0.0f));
            }
        }

        for (auto& vert : Vertices) {
            vert.Position = glm::normalize(vert.Position);
            vert.Normal = vert.Position;
        }

        Indices.shrink_to_fit();
        Vertices.shrink_to_fit();

        for (size_t i = 0; i < Vertices.size(); ++i) {
            const glm::vec3& norm = Vertices[i].Normal;
            Vertices[i].UV.x = (glm::atan(norm.x, -norm.z) / FLOAT_PI) * 0.5f + 0.5f;
            Vertices[i].UV.y = -norm.y * 0.5f + 0.5f;
        }

        auto add_vertex_w_uv = [this](const size_t& i, const glm::vec2& uv) {
            const uint32_t& idx = Indices[i];
            Indices[i] = static_cast<uint32_t>(Vertices.size());
            Vertices.emplace_back(Vertices[idx].Position, Vertices[idx].Normal, glm::vec3(0.0f, 0.0f, 0.0f), uv);
        };

        const size_t num_triangles = Indices.size() / 3;
        for (size_t i = 0; i < num_triangles; ++i) {
            const glm::vec2& uv0 = Vertices[Indices[i * 3]].UV;
            const glm::vec2& uv1 = Vertices[Indices[i * 3 + 1]].UV;
            const glm::vec2& uv2 = Vertices[Indices[i * 3 + 2]].UV;
            const float d1 = uv1.x - uv0.x;
            const float d2 = uv2.x - uv0.x;
            if (std::abs(d1) > 0.5f && std::abs(d2) > 0.5f) {
                add_vertex_w_uv(i * 3, uv0 + glm::vec2((d1 > 0.0f) ? 1.0f : -1.0f, 0.0f));
            }
            else if (std::abs(d1) > 0.5f) {
                add_vertex_w_uv(i * 3 + 1, uv1 + glm::vec2((d1 < 0.0f) ? 1.0f : -1.0f, 0.0f));
            }
            else if (std::abs(d2) > 0.5f) {
                add_vertex_w_uv(i * 3 + 2, uv2 + glm::vec2((d2 < 0.0f) ? 1.0f : -1.0f, 0.0f));
            }
        }

        const VkBufferCreateInfo vbo_info{
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            nullptr,
            0,
            sizeof(vertex_t) * Vertices.size(),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_SHARING_MODE_EXCLUSIVE,
            0u,
            nullptr
        };

        const gpu_resource_data_t vbo_data{
            Vertices.data(),
            vbo_info.size,
            0u, 0u, 0u
        };

        auto& rsrc_context = ResourceContext::Get();
        VBO = rsrc_context.CreateBuffer(&vbo_info, nullptr, 1, &vbo_data, memory_type::DEVICE_LOCAL, nullptr);

        const VkBufferCreateInfo ebo_info{
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            nullptr,
            0,
            sizeof(uint32_t) * Indices.size(),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_SHARING_MODE_EXCLUSIVE,
            0u,
            nullptr
        };

        const gpu_resource_data_t ebo_data{
            Indices.data(),
            ebo_info.size,
            0u, 0u, 0u
        };

        EBO = rsrc_context.CreateBuffer(&ebo_info, nullptr, 1, &ebo_data, memory_type::DEVICE_LOCAL, nullptr);

    }

    VulkanResource* VBO{ nullptr };
    VulkanResource* EBO{ nullptr };
    VulkanResource* AlbedoTexture{ nullptr };
    VulkanResource* AmbientOcclusionTexture{ nullptr };
    VulkanResource* HeightMap{ nullptr };
    VulkanResource* NormalMap{ nullptr };
    VulkanResource* MetallicMap{ nullptr };
    VulkanResource* RoughnessMap{ nullptr };
    std::vector<uint32_t> Indices;
    std::vector<vertex_t> Vertices;
    MaterialParameters MaterialParams;

};

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

void GenerateLights() {
    LightCounts.NumPointLights = SceneConfig.NumPointLights;
    VTF_Scene::State.PointLights = GenerateLights<PointLight>(SceneConfig.NumPointLights);
    LightCounts.NumSpotLights = SceneConfig.NumSpotLights;
    VTF_Scene::State.SpotLights = GenerateLights<SpotLight>(SceneConfig.NumSpotLights);
    LightCounts.NumDirectionalLights = SceneConfig.NumDirectionalLights;
    VTF_Scene::State.DirectionalLights = GenerateLights<DirectionalLight>(SceneConfig.NumDirectionalLights);
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

struct ComputePipelineState {

    ComputePipelineState() = default;
    ComputePipelineState(VkDevice dev) : Device(dev) {}

    VkDevice Device{ VK_NULL_HANDLE };
    VkPipeline Handle{ VK_NULL_HANDLE };

    void Destroy() {
        if (Handle != VK_NULL_HANDLE) {
            vkDestroyPipeline(Device, Handle, nullptr);
            Handle = VK_NULL_HANDLE;
        }
    }

};

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
    Descriptor* descriptor = resourcePack->GetDescriptor("MergeSort");
    descriptor->BindResourceToIdx(input_keys_loc, src_keys);
    descriptor->BindResourceToIdx(output_keys_loc, dst_keys);
    descriptor->BindResourceToIdx(input_values_loc, src_values);
    descriptor->BindResourceToIdx(output_values_loc, dst_values);

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

        descriptor->BindResourceToIdx(input_keys_loc, dst_keys);
        descriptor->BindResourceToIdx(output_keys_loc, src_keys);
        descriptor->BindResourceToIdx(input_values_loc, dst_values);
        descriptor->BindResourceToIdx(output_values_loc, src_values);

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

VTF_Scene& VTF_Scene::Get() {
    static VTF_Scene scene;
    return scene;
}

void VTF_Scene::Construct(RequiredVprObjects objects, void * user_data) {
    vprObjects = objects;
    vtfShaders = reinterpret_cast<const st::ShaderPack*>(user_data);
    resourcePack = std::make_unique<ShaderResourcePack>(nullptr, vtfShaders);
    GenerateLights();
    createSemaphores();
    createComputePools();
    createRenderpasses();
    createShaderModules();
    createComputePipelines();
    createGraphicsPipelines();
    createLightResources();
    createSortingResources();
    createIcosphereTester();
    createBVH_Resources();
    updateGlobalUBOs();
    updateClusterGrid();
    computeClusterAABBs();
}

void VTF_Scene::Destroy() {
}

void VTF_Scene::updateGlobalUBOs() {
    auto& camera = PerspectiveCamera::Get();
    auto& ctxt = RenderingContext::Get();
    auto& rsrc = ResourceContext::Get();

    MatricesDefault.projection = camera.ProjectionMatrix();
    MatricesDefault.view = camera.ViewMatrix();
    MatricesDefault.inverseView = glm::inverse(MatricesDefault.view);

    const gpu_resource_data_t matrices_update_data{
        &MatricesDefault,
        sizeof(MatricesDefault),
        0u, 0u, 0u
    };
    rsrc.SetBufferData(resourcePack->At("GlobalResources", "matrices"), 1, &matrices_update_data);

    Globals.depthRange = glm::vec2(camera.NearPlane(), camera.FarPlane());
    Globals.windowSize = glm::vec2(ctxt.Swapchain()->Extent().width, ctxt.Swapchain()->Extent().height);
    Globals.frame += 1;
    
    const gpu_resource_data_t globals_update_data{
        &Globals,
        sizeof(Globals),
        0u, 0u, 0u
    };
    rsrc.SetBufferData(resourcePack->At("GlobalResources", "globals"), 1, &globals_update_data);

}

void VTF_Scene::update() {
    updateGlobalUBOs();
    constexpr static VkCommandBufferBeginInfo compute_begin_info{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        nullptr,
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        nullptr
    };
    vkBeginCommandBuffer(computePools[0]->GetCmdBuffer(0), &compute_begin_info);
    computeUpdateLights();
    computeReduceLights();
    computeAndSortMortonCodes();
    buildLightBVH();
    vkEndCommandBuffer(computePools[0]->GetCmdBuffer(0));
    submitComputeUpdates();
}

void VTF_Scene::recordCommands() {
}

void VTF_Scene::draw() {
}

void VTF_Scene::endFrame() {
}

void VTF_Scene::computeUpdateLights() {
    auto& rsrc = ResourceContext::Get();
    // update light counts
    LightCounts.NumPointLights = static_cast<uint32_t>(State.PointLights.size());
    LightCounts.NumSpotLights = static_cast<uint32_t>(State.SpotLights.size());
    LightCounts.NumDirectionalLights = static_cast<uint32_t>(State.DirectionalLights.size());
    VulkanResource* light_counts_buffer = resourcePack->Find("VolumetricForwardLights", "LightCounts");
    const gpu_resource_data_t lcb_update{
        &LightCounts,
        sizeof(LightCounts)
    };
    rsrc.SetBufferData(light_counts_buffer, 1, &lcb_update);
    // update light positions etc
    auto cmd = computePools[0]->GetCmdBuffer(0);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, updateLightsPipeline->Handle);
    resourcePack->BindGroupSets(cmd, "UpdateLights", VK_PIPELINE_BIND_POINT_COMPUTE);
    uint32_t num_groups_x = glm::max(LightCounts.NumPointLights, glm::max(LightCounts.NumDirectionalLights, LightCounts.NumSpotLights));
    num_groups_x = static_cast<uint32_t>(glm::ceil(num_groups_x / 1024.0f));
    vkCmdDispatch(cmd, num_groups_x, 1, 1);
}

void VTF_Scene::computeReduceLights() {
    auto& rsrc = ResourceContext::Get();

    // update dispatch params
    uint32_t num_thread_groups = glm::min<uint32_t>(static_cast<uint32_t>(glm::ceil(glm::max(LightCounts.NumPointLights, LightCounts.NumSpotLights) / 512.0f)), uint32_t(512));
    DispatchParams.NumThreadGroups = glm::uvec3(num_thread_groups, 1u, 1u);
    DispatchParams.NumThreads = glm::uvec3(num_thread_groups * 512, 1u, 1u);
    const gpu_resource_data_t dp_update{
        &DispatchParams,
        sizeof(DispatchParams)
    };
    VulkanResource* dispatch_params = resourcePack->Find("SortResources", "DispatchParams");
    rsrc.SetBufferData(dispatch_params, 1, &dp_update);

    // Reduce lights
    auto cmd = computePools[0]->GetCmdBuffer(0);
    uint32_t num_elements = DispatchParams.NumThreadGroups.x;
    resourcePack->BindGroupSets(cmd, "ReduceLights", VK_PIPELINE_BIND_POINT_COMPUTE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, reduceLightsAABB0->Handle);
    vkCmdDispatch(cmd, num_thread_groups, 1u, 1u);

    // second step of reduction
    DispatchParams.NumThreadGroups = glm::uvec3{ 1u, 1u, 1u };
    DispatchParams.NumThreads = glm::uvec3{ 512u, 1u, 1u };
    rsrc.SetBufferData(dispatch_params, 1, &dp_update);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, reduceLightsAABB1->Handle);
    vkCmdDispatch(cmd, 1u, 1u, 1u);
}

void VTF_Scene::computeAndSortMortonCodes() {

    auto& rsrc = ResourceContext::Get();

    // Compute morton codes
    auto cmd = computePools[0]->GetCmdBuffer(0);
    resourcePack->BindGroupSets(cmd, "ComputeMortonCodes", VK_PIPELINE_BIND_POINT_COMPUTE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computeLightMortonCodesPipeline->Handle);
    uint32_t num_thread_groups = static_cast<uint32_t>(glm::ceil(glm::max(LightCounts.NumPointLights, LightCounts.NumSpotLights) / 1024.0f));
    vkCmdDispatch(cmd, num_thread_groups, 1, 1);

    SortParams sort_params;
    sort_params.ChunkSize = SORT_NUM_THREADS_PER_THREAD_GROUP;
    const VkBufferCopy copy{ 0, 0, VK_WHOLE_SIZE };

    VulkanResource* sort_params_rsrc = resourcePack->At("SortResources", "SortParams");
    const gpu_resource_data_t sort_params_copy{ &sort_params, sizeof(SortParams), 0u, 0u, 0u };

    // prefetch binding locations
    Descriptor* sort_descriptor = resourcePack->GetDescriptor("MergeSortResources");
    const size_t src_keys_loc = resourcePack->BindingLocation("InputKeys");
    const size_t dst_keys_loc = resourcePack->BindingLocation("OutputKeys");
    const size_t src_values_loc = resourcePack->BindingLocation("InputValues");
    const size_t dst_values_loc = resourcePack->BindingLocation("OutputValues");

    // bind radix sort pipeline now
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, radixSortPipeline->Handle);

    if (LightCounts.NumPointLights > 0u) {

        sort_params.NumElements = LightCounts.NumPointLights;
        rsrc.SetBufferData(sort_params_rsrc, 1, &sort_params_copy);
        // bind proper resources to the descriptor
        sort_descriptor->BindResourceToIdx(src_keys_loc, pointLightMortonCodes);
        sort_descriptor->BindResourceToIdx(dst_keys_loc, pointLightMortonCodes_OUT);
        sort_descriptor->BindResourceToIdx(src_values_loc, pointLightIndices);
        sort_descriptor->BindResourceToIdx(dst_values_loc, pointLightIndices_OUT);

        // bind and dispatch
        resourcePack->BindGroupSets(cmd, "RadixSort", VK_PIPELINE_BIND_POINT_COMPUTE);
        uint32_t num_thread_groups = static_cast<uint32_t>(glm::ceil(float(LightCounts.NumPointLights) / float(SORT_NUM_THREADS_PER_THREAD_GROUP)));
        vkCmdDispatch(cmd, num_thread_groups, 1u, 1u);

        // copy resources back into results
        vkCmdCopyBuffer(cmd, (VkBuffer)pointLightMortonCodes_OUT->Handle, (VkBuffer)pointLightMortonCodes->Handle, 1, &copy);
        vkCmdCopyBuffer(cmd, (VkBuffer)pointLightIndices_OUT->Handle, (VkBuffer)pointLightIndices->Handle, 1, &copy);
    }

    if (LightCounts.NumSpotLights > 0u) {

        sort_params.NumElements = LightCounts.NumSpotLights;
        rsrc.SetBufferData(sort_params_rsrc, 1, &sort_params_copy);
        // update bindings again
        sort_descriptor->BindResourceToIdx(src_keys_loc, spotLightMortonCodes);
        sort_descriptor->BindResourceToIdx(dst_keys_loc, spotLightMortonCodes_OUT);
        sort_descriptor->BindResourceToIdx(src_values_loc, spotLightIndices);
        sort_descriptor->BindResourceToIdx(dst_values_loc, spotLightIndices_OUT);
        // have to repeat binding call so that updates are propagated to descriptor set
        // might test to see if we can just call update() on sort_descriptor??
        resourcePack->BindGroupSets(cmd, "RadixSort", VK_PIPELINE_BIND_POINT_COMPUTE);
        uint32_t num_thread_groups = static_cast<uint32_t>(glm::ceil(float(LightCounts.NumSpotLights) / float(SORT_NUM_THREADS_PER_THREAD_GROUP)));
        vkCmdDispatch(cmd, num_thread_groups, 1u, 1u);

        vkCmdCopyBuffer(cmd, (VkBuffer)spotLightMortonCodes_OUT->Handle, (VkBuffer)spotLightMortonCodes->Handle, 1, &copy);
        vkCmdCopyBuffer(cmd, (VkBuffer)spotLightIndices_OUT->Handle, (VkBuffer)spotLightIndices->Handle, 1, &copy);
    }

    if (LightCounts.NumPointLights > 0u) {
        MergeSort(cmd, pointLightMortonCodes, pointLightIndices, pointLightMortonCodes_OUT, pointLightIndices_OUT, LightCounts.NumPointLights, SORT_NUM_THREADS_PER_THREAD_GROUP);
    }

    if (LightCounts.NumSpotLights > 0u) {
        MergeSort(cmd, spotLightMortonCodes, spotLightIndices, spotLightMortonCodes_OUT, spotLightIndices_OUT, LightCounts.NumSpotLights, SORT_NUM_THREADS_PER_THREAD_GROUP);
    }
}

void VTF_Scene::buildLightBVH() {
    const uint32_t compute_idx = RenderingContext::Get().Device()->QueueFamilyIndices().Compute;

    auto& rsrc = ResourceContext::Get();
    auto cmd = computePools[0]->GetCmdBuffer(0);
    VulkanResource* bvh_params_rsrc = resourcePack->At("BVHResources", "BVHParams");
    VulkanResource* point_light_bvh = resourcePack->At("BVHResources", "PointLightBVH");
    VulkanResource* spot_light_bvh = resourcePack->At("BVHResources", "SpotLightBVH");

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

    uint32_t point_light_levels = GetNumLevelsBVH(LightCounts.NumPointLights);
    uint32_t spot_light_levels = GetNumLevelsBVH(LightCounts.NumSpotLights);
    const gpu_resource_data_t bvh_update{ &BVH_Params, sizeof(BVH_Params), 0u, 0u, 0u };

    if ((point_light_levels != BVH_Params.PointLightLevels) || (spot_light_levels != BVH_Params.SpotLightLevels)) {
        rsrc.SetBufferData(bvh_params_rsrc, 1, &bvh_update);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, buildBVHBottomPipeline->Handle);
    resourcePack->BindGroupSets(cmd, "BuildBVH", VK_PIPELINE_BIND_POINT_COMPUTE);
    uint32_t max_leaves = glm::max(LightCounts.NumPointLights, LightCounts.NumSpotLights);
    uint32_t num_thread_groups = static_cast<uint32_t>(glm::ceil(float(max_leaves) / float(BVH_NUM_THREADS)));
    vkCmdDispatch(cmd, num_thread_groups, 1u, 1u);

    uint32_t max_levels = glm::max(BVH_Params.PointLightLevels, BVH_Params.SpotLightLevels);
    if (max_levels > 0u) {

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, buildBVHTopPipeline->Handle);
        for (uint32_t level = max_levels - 1u; level > 0u; --level) {
            BVH_Params.ChildLevel = level;
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

void VTF_Scene::submitComputeUpdates() {

    constexpr static VkPipelineStageFlags wait_mask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    const VkSubmitInfo submit_info{
        VK_STRUCTURE_TYPE_SUBMIT_INFO,
        nullptr,
        0u,
        nullptr,
        0,
        1,
        &computePools[0]->GetCmdBuffer(0u),
        1,
        &computeUpdateCompleteSemaphore->vkHandle()
    };
    VkResult result = vkQueueSubmit(vprObjects.device->ComputeQueue(), 1, &submit_info, VK_NULL_HANDLE);
    VkAssert(result);

}

void VTF_Scene::updateClusterGrid() {
    auto& camera = PerspectiveCamera::Get();

    float fov_y = camera.FOV();
    float z_near = camera.NearPlane();
    float z_far = camera.FarPlane();
    Globals.depthRange = glm::vec2(z_near, z_far);

    auto& ctxt = RenderingContext::Get();
    const uint32_t window_width = ctxt.Swapchain()->Extent().width;
    const uint32_t window_height = ctxt.Swapchain()->Extent().height;
    Globals.windowSize = glm::vec2(window_width, window_height);

    uint32_t cluster_dim_x = static_cast<uint32_t>(glm::ceil(window_width / float(CLUSTER_GRID_BLOCK_SIZE)));
    uint32_t cluster_dim_y = static_cast<uint32_t>(glm::ceil(window_height / float(CLUSTER_GRID_BLOCK_SIZE)));

    float sD = 2.0f * glm::tan(fov_y) / float(cluster_dim_y);
    float log_dim_y = 1.0f / glm::log(1.0f + sD);
    float log_depth = glm::log(z_far / z_near);

    uint32_t cluster_dim_z = static_cast<uint32_t>(glm::floor(log_depth * log_dim_y));    
    const uint32_t CLUSTER_SIZE = cluster_dim_x * cluster_dim_y * cluster_dim_z;
    const uint32_t LIGHT_INDEX_LIST_SIZE = cluster_dim_x * cluster_dim_y * AVERAGE_LIGHTS_PER_TILE;

    ClusterData.GridDim = glm::uvec3{ cluster_dim_x, cluster_dim_y, cluster_dim_z };
    ClusterData.ViewNear = z_near;
    ClusterData.ScreenSize = glm::uvec2{ CLUSTER_GRID_BLOCK_SIZE, CLUSTER_GRID_BLOCK_SIZE };
    ClusterData.NearK = 1.0f + sD;
    ClusterData.LogGridDimY = log_dim_y;

    auto& rsrc = ResourceContext::Get();
    const gpu_resource_data_t cluster_data_update{
        &ClusterData,
        sizeof(ClusterData),
        0u,
        0u,
        0u
    };
    VulkanResource* cluster_data_rsrc = resourcePack->At("VolumetricForward", "ClusterData");
    rsrc.SetBufferData(cluster_data_rsrc, 1, &cluster_data_update);

    clusterFlags = resourcePack->At("VolumetricForward", "ClusterFlags");
    VkBufferCreateInfo cluster_flags_create_info = *reinterpret_cast<const VkBufferCreateInfo*>(clusterFlags->Info);
    cluster_flags_create_info.size = cluster_dim_x * cluster_dim_y * cluster_dim_z * sizeof(uint8_t);
    VkBufferViewCreateInfo cluster_flags_view_info = *reinterpret_cast<const VkBufferViewCreateInfo*>(clusterFlags->ViewInfo);
    cluster_flags_view_info.range = cluster_flags_create_info.size;
    rsrc.DestroyResource(clusterFlags);
    clusterFlags = rsrc.CreateBuffer(&cluster_flags_create_info, &cluster_flags_view_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr);
    resourcePack->UpdateResource("VolumetricForward", "ClusterFlags", clusterFlags);

    uniqueClusters = resourcePack->At("VolumetricForward", "UniqueClusters");
    rsrc.DestroyResource(uniqueClusters);
    uniqueClusters = rsrc.CreateBuffer(&cluster_flags_create_info, &cluster_flags_view_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr);
    resourcePack->UpdateResource("VolumetricForward", "UniqueClusters", uniqueClusters);
    previousUniqueClusters = rsrc.CreateBuffer(&cluster_flags_create_info, &cluster_flags_view_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr);
    UpdateUniqueClusters = true;

    assignLightsToClustersArgumentBuffer = resourcePack->At("IndirectArgsSet", "IndirectArgs");

    // we'll leave this one empty for now
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
    if (debugClustersDrawIndirectArgumentBuffer) {
        rsrc.DestroyResource(debugClustersDrawIndirectArgumentBuffer);
    }
    debugClustersDrawIndirectArgumentBuffer = rsrc.CreateBuffer(&debug_indirect_draw_buffer_info, nullptr, 0, nullptr, memory_type::HOST_VISIBLE, nullptr);

    clusterAABBs = resourcePack->At("VolumetricForward", "ClusterAABBs");
    VkBufferCreateInfo cluster_aabbs_info = *reinterpret_cast<const VkBufferCreateInfo*>(clusterAABBs->Info);
    cluster_aabbs_info.size = CLUSTER_SIZE * sizeof(AABB);
    rsrc.DestroyResource(clusterAABBs);
    clusterAABBs = rsrc.CreateBuffer(&cluster_aabbs_info, nullptr, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr);
    resourcePack->UpdateResource("VolumetricForward", "ClusterAABBs", clusterAABBs);

    pointLightGrid = resourcePack->At("VolumetricForward", "PointLightGrid");
    VkBufferCreateInfo point_light_grid_info = *reinterpret_cast<const VkBufferCreateInfo*>(pointLightGrid->Info);
    point_light_grid_info.size = CLUSTER_SIZE * sizeof(uint32_t) * 2;
    VkBufferViewCreateInfo point_light_grid_view_info = *reinterpret_cast<const VkBufferViewCreateInfo*>(pointLightGrid->ViewInfo);
    point_light_grid_view_info.range = point_light_grid_info.size;
    rsrc.DestroyResource(pointLightGrid);
    pointLightGrid = rsrc.CreateBuffer(&point_light_grid_info, &point_light_grid_view_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr);
    resourcePack->UpdateResource("VolumetricForward", "PointLightGrid", pointLightGrid);

    spotLightGrid = resourcePack->At("VolumetricForward", "SpotLightGrid");
    rsrc.DestroyResource(spotLightGrid);
    spotLightGrid = rsrc.CreateBuffer(&point_light_grid_info, &point_light_grid_view_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr);
    resourcePack->UpdateResource("VolumetricForward", "SpotLightGrid", spotLightGrid);

    pointLightIndexList = resourcePack->At("VolumetricForward", "PointLightIndexList");
    VkBufferCreateInfo indices_info = *reinterpret_cast<const VkBufferCreateInfo*>(pointLightIndexList->Info);
    indices_info.size = LIGHT_INDEX_LIST_SIZE * sizeof(uint32_t);
    VkBufferViewCreateInfo indices_view_info = *reinterpret_cast<const VkBufferViewCreateInfo*>(pointLightIndexList->ViewInfo);
    indices_view_info.range = indices_info.size;
    rsrc.DestroyResource(pointLightIndexList);
    pointLightIndexList = rsrc.CreateBuffer(&indices_info, &indices_view_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr);
    resourcePack->UpdateResource("VolumetricForward", "PointLightIndexList", pointLightIndexList);

    spotLightIndexList = resourcePack->At("VolumetricForward", "SpotLightIndexList");
    rsrc.DestroyResource(spotLightIndexList);
    spotLightIndexList = rsrc.CreateBuffer(&indices_info, &indices_view_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr);
    resourcePack->UpdateResource("VolumetricForward", "SpotLightIndexList", spotLightIndexList);

    computeClusterAABBs();
}

void VTF_Scene::computeClusterAABBs() {
    constexpr static VkCommandBufferBeginInfo compute_begin_info{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        nullptr,
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        nullptr
    };
    VkCommandBuffer cmd_buffer = computePools[1]->GetCmdBuffer(0u);

    vkBeginCommandBuffer(cmd_buffer, &compute_begin_info);
        resourcePack->BindGroupSets(cmd_buffer, "ComputeClusterAABBs", VK_PIPELINE_BIND_POINT_COMPUTE);
        vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, computeClusterAABBsPipeline->Handle);
        const uint32_t dispatch_size = static_cast<uint32_t>(glm::ceil((ClusterData.GridDim.x * ClusterData.GridDim.y * ClusterData.GridDim.z) / 1024.0f));
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

    VkResult result = vkQueueSubmit(vprObjects.device->ComputeQueue(), 1, &submit_info, computeAABBsFence->vkHandle());
    VkAssert(result);

    result = vkWaitForFences(vprObjects.device->vkHandle(), 1, &computeAABBsFence->vkHandle(), VK_TRUE, UINT_MAX);
    VkAssert(result);

    result = vkResetFences(vprObjects.device->vkHandle(), 1, &computeAABBsFence->vkHandle());
    VkAssert(result);

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

void VTF_Scene::createRenderpasses() {
    createDepthAndClusterSamplesPass();
    createDepthPrePassResources();
    createClusterSamplesResources();
    createDrawRenderpass();
    createDrawFramebuffers();
}

void VTF_Scene::createDepthAndClusterSamplesPass() {

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
            vprObjects.device->FindDepthFormat(),
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

    depthAndClusterSamplesPass = std::make_unique<vpr::Renderpass>(vprObjects.device->vkHandle(), create_info);
    depthAndClusterSamplesPass->SetupBeginInfo(DepthPrePassAndClusterSamplesClearValues.data(), DepthPrePassAndClusterSamplesClearValues.size(), vprObjects.swapchain->Extent());

}

VulkanResource* VTF_Scene::createDepthStencilResource(const VkSampleCountFlagBits samples) const {
    const VkFormat depth_format = vprObjects.device->FindDepthFormat();
    const uint32_t img_width = vprObjects.swapchain->Extent().width;
    const uint32_t img_height = vprObjects.swapchain->Extent().height;

    const VkImageCreateInfo image_info{
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        nullptr,
        0,
        VK_IMAGE_TYPE_2D,
        depth_format,
        VkExtent3D{ img_width, img_height, 1 },
        1,
        1,
        samples,
        vprObjects.device->GetFormatTiling(depth_format, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT),
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0,
        nullptr,
        VK_IMAGE_LAYOUT_UNDEFINED
    };

    const VkImageViewCreateInfo view_info{
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        nullptr,
        0,
        VK_NULL_HANDLE,
        VK_IMAGE_VIEW_TYPE_2D,
        depth_format,
        VkComponentMapping{ VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A },
        VkImageSubresourceRange{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 }
    };

    auto& rsrc = ResourceContext::Get();
    VulkanResource* result{ nullptr };
    result = rsrc.CreateImage(&image_info, &view_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr);
    return result;
}

void VTF_Scene::createDepthPrePassResources() {
    depthPrePassImage = createDepthStencilResource();
}

void VTF_Scene::createClusterSamplesResources() {
    const VkFormat cluster_samples_color_format = VK_FORMAT_R8G8B8A8_UNORM;
    const VkImageTiling tiling_type = vprObjects.device->GetFormatTiling(cluster_samples_color_format, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT);
    const uint32_t img_width = vprObjects.swapchain->Extent().width;
    const uint32_t img_height = vprObjects.swapchain->Extent().height;

    const VkImageCreateInfo img_info{
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        nullptr,
        0,
        VK_IMAGE_TYPE_2D,
        cluster_samples_color_format,
        VkExtent3D{ img_width, img_height, 1 },
        1,
        1,
        VK_SAMPLE_COUNT_1_BIT,
        tiling_type,
        // need transfer src bit so we can dump it to cpu-visible textures
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0,
        nullptr,
        VK_IMAGE_LAYOUT_UNDEFINED
    };

    const VkImageViewCreateInfo view_info{
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        nullptr,
        0,
        VK_NULL_HANDLE,
        VK_IMAGE_VIEW_TYPE_2D,
        cluster_samples_color_format,
        VkComponentMapping{ VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A },
        VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
    };

    auto& rsrc = ResourceContext::Get();
    clusterSamplesImage = rsrc.CreateImage(&img_info, &view_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr);
    const VkImageView view_handles[2]{
        (VkImageView)clusterSamplesImage->ViewHandle,
        (VkImageView)depthPrePassImage->ViewHandle
    };

    const VkFramebufferCreateInfo framebuffer_info{
        VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        nullptr,
        0,
        depthAndClusterSamplesPass->vkHandle(),
        2u,
        view_handles,
        img_width,
        img_height,
        1
    };

    clusterSamplesFramebuffer = std::make_unique<vpr::Framebuffer>(vprObjects.device->vkHandle(), framebuffer_info);

}

void VTF_Scene::createDrawRenderpass() {

    drawPassDependencies = decltype(drawPassDependencies){
        VkSubpassDependency{
            VK_SUBPASS_EXTERNAL,
            0,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_MEMORY_READ_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_DEPENDENCY_BY_REGION_BIT
        },
        VkSubpassDependency{
            0,
            VK_SUBPASS_EXTERNAL,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_MEMORY_READ_BIT,
            VK_DEPENDENCY_BY_REGION_BIT
        }
    };

    drawPassDescriptions[0] = VkSubpassDescription{
        0u,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        0u,
        nullptr,
        1u,
        &drawPassColorRef,
        &drawPassPresentRef,
        &drawPassDepthRef,
        0u,
        nullptr
    };

    const std::array<VkAttachmentDescription, 3> attachments{
        VkAttachmentDescription{ // color, msaa
            0,
            vprObjects.swapchain->ColorFormat(),
            MSAA_SampleCount,
            VK_ATTACHMENT_LOAD_OP_CLEAR,
            VK_ATTACHMENT_STORE_OP_DONT_CARE,
            VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            VK_ATTACHMENT_STORE_OP_DONT_CARE,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        },
        VkAttachmentDescription{ // depth
            0,
            vprObjects.device->FindDepthFormat(),
            MSAA_SampleCount,
            VK_ATTACHMENT_LOAD_OP_CLEAR,
            VK_ATTACHMENT_STORE_OP_DONT_CARE,
            VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            VK_ATTACHMENT_STORE_OP_DONT_CARE,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
        },
        VkAttachmentDescription{ // present src
            0,
            vprObjects.swapchain->ColorFormat(),
            VK_SAMPLE_COUNT_1_BIT,
            VK_ATTACHMENT_LOAD_OP_CLEAR,
            VK_ATTACHMENT_STORE_OP_STORE,
            VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            VK_ATTACHMENT_STORE_OP_DONT_CARE,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
        }
    };

    const VkRenderPassCreateInfo create_info{
        VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        nullptr,
        0,
        static_cast<uint32_t>(attachments.size()),
        attachments.data(),
        static_cast<uint32_t>(drawPassDescriptions.size()),
        drawPassDescriptions.data(),
        static_cast<uint32_t>(drawPassDependencies.size()),
        drawPassDependencies.data()
    };

    primaryDrawPass = std::make_unique<vpr::Renderpass>(vprObjects.device->vkHandle(), create_info);
    primaryDrawPass->SetupBeginInfo(DrawPassClearValues.data(), DrawPassClearValues.size(), vprObjects.swapchain->Extent());

}

void VTF_Scene::createDrawFramebuffers() {

    const uint32_t img_count = vprObjects.swapchain->ImageCount();
    const VkExtent2D& img_extent = vprObjects.swapchain->Extent();

    const VkImageCreateInfo img_info{
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        nullptr,
        0,
        VK_IMAGE_TYPE_2D,
        vprObjects.swapchain->ColorFormat(),
        VkExtent3D{ img_extent.width, img_extent.height, 1 },
        1,
        1,
        MSAA_SampleCount,
        vprObjects.device->GetFormatTiling(vprObjects.swapchain->ColorFormat(), VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT),
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0u,
        nullptr,
        VK_IMAGE_LAYOUT_UNDEFINED
    };

    const VkImageViewCreateInfo view_info{
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        nullptr,
        0,
        VK_NULL_HANDLE,
        VK_IMAGE_VIEW_TYPE_2D,
        img_info.format,
        VkComponentMapping{ VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A },
        VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
    };

    auto& rsrc = ResourceContext::Get();
    for (uint32_t i = 0u; i < img_count; ++i) {
        depthRendertargetImages.emplace_back(createDepthStencilResource(MSAA_SampleCount));
        drawMultisampleImages.emplace_back(rsrc.CreateImage(&img_info, &view_info, 0u, nullptr, memory_type::DEVICE_LOCAL, nullptr));
        const VkImageView view_handles[3]{ 
            (VkImageView)drawMultisampleImages.back()->ViewHandle, (VkImageView)depthRendertargetImages.back()->ViewHandle, 
            vprObjects.swapchain->ImageView(size_t(i)) 
        };
        const VkFramebufferCreateInfo framebuffer_info{
            VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            nullptr,
            0,
            primaryDrawPass->vkHandle(),
            3,
            view_handles,
            img_extent.width,
            img_extent.height,
            1
        };

        drawFramebuffers.emplace_back(std::make_unique<vpr::Framebuffer>(vprObjects.device->vkHandle(), framebuffer_info));
    }

}

void VTF_Scene::createShaderModules() {
    
    std::vector<std::string> group_names;
    {
        auto group_names_dll = vtfShaders->GetShaderGroupNames();
        for (size_t i = 0; i < group_names_dll.NumStrings; ++i) {
            group_names.emplace_back(group_names_dll[i]);
        }
    }

    for (const auto& name : group_names) {
        const st::Shader* curr_shader = vtfShaders->GetShaderGroup(name.c_str());
        size_t num_stages{ 0 };
        curr_shader->GetShaderStages(&num_stages, nullptr);
        std::vector<st::ShaderStage> stages(num_stages, st::ShaderStage("aaaa", VK_SHADER_STAGE_ALL));
        curr_shader->GetShaderStages(&num_stages, stages.data());
        
        for (const auto& stage : stages) {
            size_t binary_sz{ 0 };
            curr_shader->GetShaderBinary(stage, &binary_sz, nullptr);
            std::vector<uint32_t> binary_data(binary_sz);
            curr_shader->GetShaderBinary(stage, &binary_sz, binary_data.data());
            shaderModules.emplace(stage, std::make_unique<vpr::ShaderModule>(vprObjects.device->vkHandle(), stage.GetStage(), binary_data.data(), static_cast<uint32_t>(binary_data.size() * sizeof(uint32_t))));
            groupStages[name].emplace_back(stage);
        }


        groupCaches.emplace(name, std::make_unique<vpr::PipelineCache>(vprObjects.device->vkHandle(), vprObjects.physicalDevice->vkHandle(), std::hash<std::string>()(name)));

    }

}

void VTF_Scene::createComputePipelines() {
    createUpdateLightsPipeline();
    createReduceLightAABBsPipelines();
    createMortonCodePipeline();
    createRadixSortPipeline();
    createBVH_Pipelines();
    createComputeClusterAABBsPipeline();
    createIndirectArgsPipeline();
    createMergeSortPipelines();
}

void VTF_Scene::createUpdateLightsPipeline() {
    const static std::string groupName{ "UpdateLights" };
    const st::Shader* update_lights_shader = vtfShaders->GetShaderGroup(groupName.c_str());
    const st::ShaderStage& update_lights_stage = groupStages.at(groupName).front();

    const VkComputePipelineCreateInfo pipeline_info{
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        nullptr,
        0,
        shaderModules.at(update_lights_stage)->PipelineInfo(),
        resourcePack->PipelineLayout(groupName),
        VK_NULL_HANDLE,
        -1
    };

    updateLightsPipeline = std::make_unique<ComputePipelineState>(vprObjects.device->vkHandle());
    VkResult result = vkCreateComputePipelines(vprObjects.device->vkHandle(), groupCaches.at(groupName)->vkHandle(), 1, &pipeline_info, nullptr, &updateLightsPipeline->Handle);
    VkAssert(result);

}

void VTF_Scene::createReduceLightAABBsPipelines() {
    const static std::string groupName{ "ReduceLights" };
    const st::Shader* reduce_lights_shader = vtfShaders->GetShaderGroup(groupName.c_str());
    const st::ShaderStage& reduce_lights_stage = groupStages.at(groupName).front();

    VkPipelineShaderStageCreateInfo pipeline_shader_info = shaderModules.at(reduce_lights_stage)->PipelineInfo();

    const VkComputePipelineCreateInfo reduce_lights_0_info{
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        nullptr,
        VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT,
        pipeline_shader_info,
        resourcePack->PipelineLayout(groupName),
        VK_NULL_HANDLE,
        -1
    };

    reduceLightsAABB0 = std::make_unique<ComputePipelineState>(vprObjects.device->vkHandle());
    VkResult result = vkCreateComputePipelines(vprObjects.device->vkHandle(), groupCaches.at(groupName)->vkHandle(), 1, &reduce_lights_0_info, nullptr, &reduceLightsAABB0->Handle);
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
        resourcePack->PipelineLayout(groupName),
        reduceLightsAABB0->Handle,
        -1
    };

    reduceLightsAABB1 = std::make_unique<ComputePipelineState>(vprObjects.device->vkHandle());
    result = vkCreateComputePipelines(vprObjects.device->vkHandle(), groupCaches.at(groupName)->vkHandle(), 1, &reduce_lights_1_info, nullptr, &reduceLightsAABB1->Handle);
    VkAssert(result);

}

void VTF_Scene::createMortonCodePipeline() {
    const static std::string groupName{ "ComputeMortonCodes" };
    const st::Shader* compute_morton_shader = vtfShaders->GetShaderGroup(groupName.c_str());
    const st::ShaderStage& morton_stage = groupStages.at(groupName).front();

    const VkComputePipelineCreateInfo pipeline_info{
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        nullptr,
        0,
        shaderModules.at(morton_stage)->PipelineInfo(),
        resourcePack->PipelineLayout("ComputeMortonCodes"),
        VK_NULL_HANDLE,
        -1
    };

    computeLightMortonCodesPipeline = std::make_unique<ComputePipelineState>(vprObjects.device->vkHandle());
    VkResult result = vkCreateComputePipelines(vprObjects.device->vkHandle(), groupCaches.at(groupName)->vkHandle(), 1, &pipeline_info, nullptr, &computeLightMortonCodesPipeline->Handle);
    VkAssert(result);

}

void VTF_Scene::createRadixSortPipeline() {
    const static std::string groupName{ "RadixSort" };
    const st::Shader* radix_shader = vtfShaders->GetShaderGroup(groupName.c_str());
    const st::ShaderStage& radix_stage = groupStages.at(groupName).front();

    const VkComputePipelineCreateInfo pipeline_info{
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        nullptr,
        0,
        shaderModules.at(radix_stage)->PipelineInfo(),
        resourcePack->PipelineLayout("RadixSort"),
        VK_NULL_HANDLE,
        -1
    };

    radixSortPipeline = std::make_unique<ComputePipelineState>(vprObjects.device->vkHandle());
    VkResult result = vkCreateComputePipelines(vprObjects.device->vkHandle(), groupCaches.at(groupName)->vkHandle(), 1, &pipeline_info, nullptr, &radixSortPipeline->Handle);
    VkAssert(result);

}

void VTF_Scene::createBVH_Pipelines() {
    const static std::string groupName{ "BuildBVH" };
    const st::Shader* bvh_construction_shader = vtfShaders->GetShaderGroup("BuildBVH");
    const st::ShaderStage& bvh_stage = groupStages.at("BuildBVH").front();

    // retrieve and prepare to set constants
    size_t num_specialization_constants{ 0 };
    bvh_construction_shader->GetSpecializationConstants(&num_specialization_constants, nullptr);
    std::vector<st::SpecializationConstant> constants(num_specialization_constants);
    bvh_construction_shader->GetSpecializationConstants(&num_specialization_constants, constants.data());

    VkPipelineShaderStageCreateInfo pipeline_shader_info = shaderModules.at(bvh_stage)->PipelineInfo();

    VkComputePipelineCreateInfo pipeline_info_0{
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        nullptr,
        VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT,
        pipeline_shader_info,
        resourcePack->PipelineLayout("BuildBVH"),
        VK_NULL_HANDLE,
        -1
    };

    buildBVHBottomPipeline = std::make_unique<ComputePipelineState>();
    buildBVHBottomPipeline->Device = vprObjects.device->vkHandle();
    VkResult result = vkCreateComputePipelines(vprObjects.device->vkHandle(), groupCaches.at("BuildBVH")->vkHandle(), 1, &pipeline_info_0, nullptr, &buildBVHBottomPipeline->Handle);
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
    
    VkComputePipelineCreateInfo pipeline_info_1 {
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        nullptr,
        VK_PIPELINE_CREATE_DERIVATIVE_BIT,
        pipeline_shader_info,
        resourcePack->PipelineLayout("BuildBVH"),
        buildBVHBottomPipeline->Handle,
        -1
    };

    buildBVHTopPipeline = std::make_unique<ComputePipelineState>();
    buildBVHTopPipeline->Device = vprObjects.device->vkHandle();
    result = vkCreateComputePipelines(vprObjects.device->vkHandle(), groupCaches.at("BuildBVH")->vkHandle(), 1, &pipeline_info_1, nullptr, &buildBVHTopPipeline->Handle);
    
}

void VTF_Scene::createComputeClusterAABBsPipeline() {
    const static std::string groupName{ "ComputeClusterAABBs" };
    const st::Shader* compute_shader = vtfShaders->GetShaderGroup(groupName.c_str());
    const st::ShaderStage& compute_stage = groupStages.at(groupName).front();

    const VkComputePipelineCreateInfo pipeline_info{
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        nullptr,
        0,
        shaderModules.at(compute_stage)->PipelineInfo(),
        resourcePack->PipelineLayout(groupName),
        VK_NULL_HANDLE,
        -1
    };

    computeClusterAABBsPipeline = std::make_unique<ComputePipelineState>(vprObjects.device->vkHandle());
    VkResult result = vkCreateComputePipelines(vprObjects.device->vkHandle(), groupCaches.at(groupName)->vkHandle(), 1, &pipeline_info, nullptr, &computeClusterAABBsPipeline->Handle);
    VkAssert(result);

    computeAABBsFence = std::make_unique<vpr::Fence>(vprObjects.device->vkHandle(), 0u);

}

void VTF_Scene::createIndirectArgsPipeline() {
    const static std::string groupName{ "UpdateClusterIndirectArgs" };
    const st::Shader* indir_shader = vtfShaders->GetShaderGroup(groupName.c_str());
    const st::ShaderStage& indir_stage = groupStages.at(groupName).front();
    
    const VkComputePipelineCreateInfo pipeline_info{
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        nullptr,
        0,
        shaderModules.at(indir_stage)->PipelineInfo(),
        resourcePack->PipelineLayout(groupName),
        VK_NULL_HANDLE,
        -1
    };

    updateIndirectArgsPipeline = std::make_unique<ComputePipelineState>(vprObjects.device->vkHandle());
    VkResult result = vkCreateComputePipelines(vprObjects.device->vkHandle(), groupCaches.at(groupName)->vkHandle(), 1, &pipeline_info, nullptr, &updateIndirectArgsPipeline->Handle);
    VkAssert(result);

}

void VTF_Scene::createMergeSortPipelines() {
    const static std::string groupName{ "MergeSort" };
    const st::Shader* merge_shader = vtfShaders->GetShaderGroup(groupName.c_str());
    const st::ShaderStage& radix_stage = groupStages.at(groupName).front();

    mergePathPartitionsPipeline = std::make_unique<ComputePipelineState>(vprObjects.device->vkHandle());

    VkPipelineShaderStageCreateInfo shader_info = shaderModules.at(radix_stage)->PipelineInfo();

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

    mergeSortPipeline = std::make_unique<ComputePipelineState>(vprObjects.device->vkHandle());

    VkPipeline pipeline_handles_buf[2]{ VK_NULL_HANDLE, VK_NULL_HANDLE };
    const VkComputePipelineCreateInfo pipeline_infos[2]{
        VkComputePipelineCreateInfo{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            nullptr,
            VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT,
            shaderModules.at(radix_stage)->PipelineInfo(),
            resourcePack->PipelineLayout("MergeSort"),
            VK_NULL_HANDLE,
            -1
        },
        VkComputePipelineCreateInfo{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            nullptr,
            VK_PIPELINE_CREATE_DERIVATIVE_BIT,
            shader_info,
            resourcePack->PipelineLayout("MergeSort"),
            VK_NULL_HANDLE,
            0 // index is previous pipeline to derive from
        }
    };

    VkResult result = vkCreateComputePipelines(vprObjects.device->vkHandle(), groupCaches.at(groupName)->vkHandle(), 2, pipeline_infos, nullptr, pipeline_handles_buf);
    VkAssert(result);

    // copy handles over
    mergePathPartitionsPipeline->Handle = pipeline_handles_buf[0];
    mergeSortPipeline->Handle = pipeline_handles_buf[1];

}

void VTF_Scene::createGraphicsPipelines() {
    createDepthPrePassPipeline();
    createClusterSamplesPipeline();
    createDrawPipelines();
}

void VTF_Scene::createDepthPrePassPipeline() {
    using namespace vpr;

    static const std::string groupName{ "DepthPrePass" };
    const st::Shader* depth_group = vtfShaders->GetShaderGroup(groupName.c_str());

    const st::ShaderStage& depth_vert = groupStages.at(groupName).front();
    const st::ShaderStage& depth_frag = groupStages.at(groupName).back();

    GraphicsPipelineInfo pipeline_info;

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
    const VkPipelineShaderStageCreateInfo shader_stages[2]{ shaderModules.at(depth_vert)->PipelineInfo(), shaderModules.at(depth_frag)->PipelineInfo() };
    create_info.stageCount = 2;
    create_info.pStages = shader_stages;
    create_info.subpass = 0;
    create_info.renderPass = depthAndClusterSamplesPass->vkHandle();
    create_info.layout = resourcePack->PipelineLayout(groupName);

    depthPrePassPipeline = std::make_unique<vpr::GraphicsPipeline>(vprObjects.device->vkHandle());
    depthPrePassPipeline->Init(create_info, groupCaches.at(groupName)->vkHandle());

}

void VTF_Scene::createClusterSamplesPipeline() {
    static const std::string groupName{ "ClusterSamples" };

    const st::ShaderStage& samples_vert = groupStages.at(groupName).front();
    const st::ShaderStage& samples_frag = groupStages.at(groupName).back();

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
    const VkPipelineShaderStageCreateInfo stages[2]{ shaderModules.at(samples_vert)->PipelineInfo(), shaderModules.at(samples_frag)->PipelineInfo() };
    create_info.stageCount = 2;
    create_info.pStages = stages;
    create_info.subpass = 0;
    create_info.layout = resourcePack->PipelineLayout(groupName);
    create_info.renderPass = depthAndClusterSamplesPass->vkHandle();

    clusterSamplesPipeline = std::make_unique<vpr::GraphicsPipeline>(vprObjects.device->vkHandle());
    clusterSamplesPipeline->Init(create_info, groupCaches.at(groupName)->vkHandle());

}

void VTF_Scene::createDrawPipelines() {
    static const std::string groupName{ "DrawPass" };

    const st::ShaderStage& draw_vert = groupStages.at(groupName).front();
    const st::ShaderStage& draw_frag = groupStages.at(groupName).back();
    const VkPipelineShaderStageCreateInfo stages[2]{ 
        shaderModules.at(draw_vert)->PipelineInfo(), 
        shaderModules.at(draw_frag)->PipelineInfo() 
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


}

void VTF_Scene::createLightResources() {
    auto& rsrc_context = ResourceContext::Get();

    if (State.PointLights.empty()) {
        GenerateSceneLights();
    }

    VulkanResource* point_lights_buffer = resourcePack->At("VolumetricForwardLights", "PointLights");
    const gpu_resource_data_t point_lights_data{
        State.PointLights.data(),
        State.PointLights.size() * sizeof(PointLight),
        0u, 0u, 0u
    };
    const uint32_t curr_point_lights_buffer_size = static_cast<uint32_t>(reinterpret_cast<const VkBufferCreateInfo*>(point_lights_buffer->Info)->size);
    const uint32_t required_point_lights_buffer_size = static_cast<uint32_t>(point_lights_data.DataSize);
    if (required_point_lights_buffer_size > curr_point_lights_buffer_size) {
        VkBufferCreateInfo recreate_buffer_info = *reinterpret_cast<const VkBufferCreateInfo*>(point_lights_buffer->Info);
        recreate_buffer_info.size = required_point_lights_buffer_size;
        rsrc_context.DestroyResource(point_lights_buffer);
        point_lights_buffer = rsrc_context.CreateBuffer(&recreate_buffer_info, nullptr, 1, &point_lights_data, memory_type::DEVICE_LOCAL, nullptr);
        resourcePack->UpdateResource("VolumetricForwardLights", "PointLights", point_lights_buffer);
    }
    else {
        rsrc_context.SetBufferData(point_lights_buffer, 1, &point_lights_data);
    }

    VulkanResource* spot_lights_buffer = resourcePack->At("VolumetricForwardLights", "SpotLights");
    const gpu_resource_data_t spot_lights_data{
        State.SpotLights.data(),
        State.SpotLights.size() * sizeof(SpotLight),
        0u, 0u, 0u
    };
    const uint32_t curr_spot_lights_buffer_size = static_cast<uint32_t>(reinterpret_cast<const VkBufferCreateInfo*>(spot_lights_buffer->Info)->size);
    const uint32_t required_spot_lights_buffer_size = static_cast<uint32_t>(spot_lights_data.DataSize);
    if (required_spot_lights_buffer_size > curr_spot_lights_buffer_size) {
        VkBufferCreateInfo recreate_buffer_info = *reinterpret_cast<const VkBufferCreateInfo*>(spot_lights_buffer->Info);
        recreate_buffer_info.size = required_spot_lights_buffer_size;
        rsrc_context.DestroyResource(spot_lights_buffer);
        spot_lights_buffer = rsrc_context.CreateBuffer(&recreate_buffer_info, nullptr, 1, &spot_lights_data, memory_type::DEVICE_LOCAL, nullptr);
        resourcePack->UpdateResource("VolumetricForwardLights", "SpotLights", spot_lights_buffer);
    }
    else {
        rsrc_context.SetBufferData(spot_lights_buffer, 1, &spot_lights_data);
    }

    VulkanResource* dir_lights_buffer = resourcePack->At("VolumetricForwardLights", "DirectionalLights");
    const gpu_resource_data_t dir_lights_data{
        State.DirectionalLights.data(),
        State.DirectionalLights.size() * sizeof(DirectionalLight),
        0u, 0u, 0u
    };
    const uint32_t current_dir_lights_buffer_size = static_cast<uint32_t>(reinterpret_cast<const VkBufferCreateInfo*>(dir_lights_buffer->Info)->size);
    const uint32_t req_dir_lights_buffer_size = static_cast<uint32_t>(dir_lights_data.DataSize);
    if (req_dir_lights_buffer_size > current_dir_lights_buffer_size) {
        VkBufferCreateInfo recreate_buffer_info = *reinterpret_cast<const VkBufferCreateInfo*>(dir_lights_buffer->Info);
        recreate_buffer_info.size = req_dir_lights_buffer_size;
        rsrc_context.DestroyResource(dir_lights_buffer);
        dir_lights_buffer = rsrc_context.CreateBuffer(&recreate_buffer_info, nullptr, 1, &dir_lights_data, memory_type::DEVICE_LOCAL, nullptr);
    }
    else {
        rsrc_context.SetBufferData(dir_lights_buffer, 1, &dir_lights_data);
    }

}

void VTF_Scene::createSortingResources() {
    auto& rsrc_context = ResourceContext::Get();

    VkBufferCreateInfo sort_buffers_create_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        sizeof(uint32_t) * LightCounts.NumPointLights,
        VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0u,
        nullptr
    };

    VkBufferViewCreateInfo sort_buffer_views_create_info{
        VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
        nullptr,
        0,
        VK_NULL_HANDLE,
        VK_FORMAT_R32_UINT,
        0u,
        sort_buffers_create_info.size
    };

    pointLightIndices = rsrc_context.CreateBuffer(&sort_buffers_create_info, &sort_buffer_views_create_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr);
    rsrc_context.FillBuffer(pointLightIndices, 0u, 0u, size_t(sort_buffers_create_info.size));
    pointLightMortonCodes = rsrc_context.CreateBuffer(&sort_buffers_create_info, &sort_buffer_views_create_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr);
    rsrc_context.FillBuffer(pointLightMortonCodes, 0u, 0u, size_t(sort_buffers_create_info.size));
    pointLightIndices_OUT = rsrc_context.CreateBuffer(&sort_buffers_create_info, &sort_buffer_views_create_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr);
    rsrc_context.FillBuffer(pointLightIndices_OUT, 0u, 0u, size_t(sort_buffers_create_info.size));
    pointLightMortonCodes_OUT = rsrc_context.CreateBuffer(&sort_buffers_create_info, &sort_buffer_views_create_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr);
    rsrc_context.FillBuffer(pointLightMortonCodes_OUT, 0u, 0u, size_t(sort_buffers_create_info.size));

    sort_buffers_create_info.size = sizeof(uint32_t) * LightCounts.NumSpotLights;
    sort_buffer_views_create_info.range = sort_buffers_create_info.size;

    spotLightIndices = rsrc_context.CreateBuffer(&sort_buffers_create_info, &sort_buffer_views_create_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr);
    rsrc_context.FillBuffer(pointLightIndices, 0u, 0u, size_t(sort_buffers_create_info.size));
    spotLightMortonCodes = rsrc_context.CreateBuffer(&sort_buffers_create_info, &sort_buffer_views_create_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr);
    rsrc_context.FillBuffer(spotLightMortonCodes, 0u, 0u, size_t(sort_buffers_create_info.size));
    spotLightIndices_OUT = rsrc_context.CreateBuffer(&sort_buffers_create_info, &sort_buffer_views_create_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr);
    rsrc_context.FillBuffer(spotLightIndices_OUT, 0u, 0u, size_t(sort_buffers_create_info.size));
    spotLightMortonCodes_OUT = rsrc_context.CreateBuffer(&sort_buffers_create_info, &sort_buffer_views_create_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr);
    rsrc_context.FillBuffer(spotLightMortonCodes_OUT, 0u, 0u, size_t(sort_buffers_create_info.size));

    resourcePack->UpdateResource("SortResources", "PointLightIndices", pointLightIndices);
    resourcePack->UpdateResource("SortResources", "SpotLightIndices", spotLightIndices);
    resourcePack->UpdateResource("SortResources", "PointLightMortonCodes", pointLightMortonCodes);
    resourcePack->UpdateResource("SortResources", "SpotLightMortonCodes", spotLightMortonCodes);

    rsrc_context.Update();

}

void VTF_Scene::createBVH_Resources() {
    auto& rsrc_context = ResourceContext::Get();

    const uint32_t point_light_nodes = GetNumNodesBVH(LightCounts.NumPointLights);
    const uint32_t spot_light_nodes = GetNumNodesBVH(LightCounts.NumSpotLights);

    pointLightBVH = resourcePack->At("BVHResources", "PointLightBVH");
    const uint32_t curr_point_light_bvh_size = static_cast<uint32_t>(reinterpret_cast<const VkBufferCreateInfo*>(pointLightBVH->Info)->size);
    const uint32_t req_point_light_bvh_size = static_cast<uint32_t>(point_light_nodes * sizeof(AABB));
    if (req_point_light_bvh_size > curr_point_light_bvh_size) {
        VkBufferCreateInfo recreate_buffer_info = *reinterpret_cast<const VkBufferCreateInfo*>(pointLightBVH->Info);
        recreate_buffer_info.size = req_point_light_bvh_size;
        rsrc_context.DestroyResource(pointLightBVH);
        pointLightBVH = rsrc_context.CreateBuffer(&recreate_buffer_info, nullptr, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr);
        resourcePack->UpdateResource("BVHResources", "PointLightBVH", pointLightBVH);
    }

    spotLightBVH = resourcePack->At("BVHResources", "SpotLightBVH");
    const uint32_t curr_spot_light_bvh_size = static_cast<uint32_t>(reinterpret_cast<const VkBufferCreateInfo*>(spotLightBVH->Info)->size);
    const uint32_t req_spot_light_bvh_size = static_cast<uint32_t>(spot_light_nodes * sizeof(AABB));
    if (req_spot_light_bvh_size > curr_spot_light_bvh_size) {
        VkBufferCreateInfo recreate_buffer_info = *reinterpret_cast<const VkBufferCreateInfo*>(spotLightBVH->Info);
        recreate_buffer_info.size = req_spot_light_bvh_size;
        rsrc_context.DestroyResource(spotLightBVH);
        spotLightBVH = rsrc_context.CreateBuffer(&recreate_buffer_info, nullptr, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr);
        resourcePack->UpdateResource("BVHResources", "SpotLightBVH", spotLightBVH);
    }

}

VulkanResource* VTF_Scene::loadTexture(const char* file_path_str) {
    auto& rsrc_context = ResourceContext::Get();
    stbi_uc* pixels = nullptr;
    int x{ 0 };
    int y{ 0 };
    int channels{ 0 };

    pixels = stbi_load(file_path_str, &x, &y, &channels, 4);
    size_t albedo_footprint = channels * x * y * sizeof(stbi_uc);
    const gpu_image_resource_data_t img_rsrc_data{
        pixels,
        albedo_footprint,
        size_t(x),
        size_t(y),
        0u,
        1u,
        0u
    };

    VkFormat img_format = VK_FORMAT_UNDEFINED;
    if (channels == 4) {
        img_format = VK_FORMAT_R8G8B8A8_UNORM;
    }
    else if (channels == 3) {
        img_format = VK_FORMAT_R8G8B8_UNORM;
    }
    else if (channels == 2) {
        img_format = VK_FORMAT_R8G8_UNORM;
    }
    else if (channels == 1) {
        img_format = VK_FORMAT_R8_UNORM;
    }
    else {
        throw std::runtime_error("Bad format");
    }

    const VkImageCreateInfo img_create_info{
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        nullptr,
        0,
        VK_IMAGE_TYPE_2D,
        img_format,
        VkExtent3D{ uint32_t(x), uint32_t(y), 1u },
        1u,
        1u,
        VK_SAMPLE_COUNT_1_BIT,
        vprObjects.device->GetFormatTiling(img_format, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT),
        VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0,
        nullptr,
        VK_IMAGE_LAYOUT_UNDEFINED
    };

    const VkImageViewCreateInfo view_create_info {
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        nullptr,
        0,
        VK_NULL_HANDLE,
        VK_IMAGE_VIEW_TYPE_2D,
        img_format,
        VkComponentMapping{ VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A },
        VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
    };

    VulkanResource* result = rsrc_context.CreateImage(&img_create_info, &view_create_info, 1, &img_rsrc_data, memory_type::DEVICE_LOCAL, nullptr);
    // we do it like this so we can safely free "pixels" before returning
    rsrc_context.Update();
    stbi_image_free(pixels);
    return result;
}

void VTF_Scene::createIcosphereTester() {
    namespace fs = std::experimental::filesystem;

    icosphereTester = std::make_unique<TestIcosphereMesh>();
    icosphereTester->CreateMesh(5u);

    const static fs::path prefix_path_to_textures{ "../../../../assets/textures/harsh_bricks" };
    if (!fs::exists(prefix_path_to_textures)) {
        throw std::runtime_error("Error in prefix path to harsh brick textures!");
    }

    const fs::path albedo_path = prefix_path_to_textures / fs::path("harshbricks-albedo.png");
    const std::string albedo_str = albedo_path.string();
    const fs::path ao_path = prefix_path_to_textures / fs::path("harshbricks-ao2.png");
    const std::string ao_str = ao_path.string();
    const fs::path metallic_path = prefix_path_to_textures / fs::path("harshbricks-metalness.png");
    const std::string metallic_str = metallic_path.string();
    const fs::path normal_path = prefix_path_to_textures / fs::path("harshbricks-normal.png");
    const std::string normal_str = normal_path.string();
    const fs::path roughness_path = prefix_path_to_textures / fs::path("harshbricks-roughness.png");
    const std::string roughness_str = roughness_path.string();

    icosphereTester->AlbedoTexture = loadTexture(albedo_str.c_str());
    icosphereTester->AmbientOcclusionTexture = loadTexture(ao_str.c_str());
    icosphereTester->NormalMap = loadTexture(normal_str.c_str());
    icosphereTester->MetallicMap = loadTexture(metallic_str.c_str());
    icosphereTester->RoughnessMap = loadTexture(roughness_str.c_str());
    
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
