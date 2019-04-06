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
#include "Framebuffer.hpp"
#include <array>
#include <unordered_set>
#include <unordered_map>
#include <string>

constexpr static uint32_t SORT_NUM_THREADS_PER_THREAD_GROUP = 256u;
constexpr static uint32_t SORT_ELEMENTS_PER_THREAD = 8u;
constexpr static uint32_t BVH_NUM_THREADS = 32u * 16u;
constexpr static uint32_t AVERAGE_OVERLAPPING_LIGHTS_PER_CLUSTER = 20u;
constexpr static uint32_t LIGHT_GRID_BLOCK_SIZE = 32u;
constexpr static uint32_t CLUSTER_GRID_BLOCK_SIZE = 64u;
constexpr static uint32_t AVERAGE_LIGHTS_PER_TILE = 100u;
constexpr static uint32_t MAX_POINT_LIGHTS = 8192u;

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

constexpr static resource_creation_flags DEF_RESOURCE_FLAGS = VTF_USE_DEBUG_INFO ? resource_creation_flags(ResourceCreateMemoryStrategyMinTime | ResourceCreateUserDataAsString) : resource_creation_flags(ResourceCreateMemoryStrategyMinTime);

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

void ComputePipelineCreationShim(vtf_frame_data_t& frame, const std::string& name, const VkComputePipelineCreateInfo* pipeline_info, const std::string& group_name)
{
	auto* device = RenderingContext::Get().Device();
	frame.computePipelines[name] = ComputePipelineState(device->vkHandle());
	VkResult result = vkCreateComputePipelines(device->vkHandle(), vtf_frame_data_t::pipelineCaches.at(group_name)->vkHandle(), 1, pipeline_info, nullptr, &frame.computePipelines.at(name).Handle);
	VkAssert(result);
	if constexpr (VTF_USE_DEBUG_INFO && VTF_VALIDATION_ENABLED)
	{
		result = RenderingContext::SetObjectName(VK_OBJECT_TYPE_PIPELINE, (uint64_t)frame.computePipelines.at(name).Handle, VTF_DEBUG_OBJECT_NAME(name.c_str()));
		VkAssert(result);
	}
}

// binder is passed as a copy / by-value as this is intended behavior: this new binder instance can now fork into separate threads from the original, but also inherits
// the original sets (so only sets we actually update, the MergeSort one, need to be re-allocated or re-assigned (as updating a set invalidates it for that frame)
void MergeSort(vtf_frame_data_t& frame, VkCommandBuffer cmd, VulkanResource* src_keys, VulkanResource* src_values, VulkanResource* dst_keys, VulkanResource* dst_values,
    uint32_t total_values, uint32_t chunk_size, DescriptorBinder dscr_binder);

VulkanResource* createDepthStencilResource(const VkSampleCountFlagBits samples);

std::string ShaderStringWithStage(const std::string& base_name, const VkShaderStageFlags flags)
{
	switch (flags)
	{
	case VK_SHADER_STAGE_VERTEX_BIT:
		return base_name + std::string("_Vertex");
	case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
		return base_name + std::string("_TessCntl");
	case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
		return base_name + std::string("_TessEval");
	case VK_SHADER_STAGE_GEOMETRY_BIT:
		return base_name + std::string("_Geometry");
	case VK_SHADER_STAGE_FRAGMENT_BIT:
		return base_name + std::string("_Fragment");
	case VK_SHADER_STAGE_COMPUTE_BIT:
		return base_name + std::string("_Compute");
	default:
		throw std::domain_error("Invalid shader stage!");
	}
}

void CreateShaders(const st::ShaderPack* pack) {

    vtf_frame_data_t::vtfShaders = pack;

    auto* device = RenderingContext::Get().Device();
    auto* physicalDevice = RenderingContext::Get().PhysicalDevice();
	auto debug_fns = device->DebugUtilsHandler();

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
            auto iter = vtf_frame_data_t::shaderModules.emplace(stage, std::make_unique<vpr::ShaderModule>(device->vkHandle(), stage.GetStage(), binary_data.data(), static_cast<uint32_t>(binary_data.size() * sizeof(uint32_t))));
            vtf_frame_data_t::groupStages[name].emplace_back(stage);
			if constexpr (VTF_USE_DEBUG_INFO && VTF_VALIDATION_ENABLED)
			{
				const std::string shader_stage_name{ ShaderStringWithStage(name, stage.GetStage()) };
				VkResult result = RenderingContext::SetObjectName(VK_OBJECT_TYPE_SHADER_MODULE, (uint64_t)iter.first->second->vkHandle(), VTF_DEBUG_OBJECT_NAME(shader_stage_name.c_str()));
				VkAssert(result);
			}
        }


        auto iter = vtf_frame_data_t::pipelineCaches.emplace(name, std::make_unique<vpr::PipelineCache>(device->vkHandle(), physicalDevice->vkHandle(), std::hash<std::string>()(name)));
		assert(iter.second);
		if constexpr (VTF_USE_DEBUG_INFO && VTF_VALIDATION_ENABLED)
		{
			const std::string pipeline_cache_name = name + std::string("_PipelineCache");
			VkResult result = RenderingContext::SetObjectName(VK_OBJECT_TYPE_PIPELINE_CACHE, (uint64_t)iter.first->second->vkHandle(), VTF_DEBUG_OBJECT_NAME(pipeline_cache_name.c_str()));
			VkAssert(result);
		}

    }
}

void SetupDescriptors(vtf_frame_data_t& frame) {
    frame.descriptorPack = std::make_unique<DescriptorPack>(nullptr, vtf_frame_data_t::vtfShaders);
}

void createGlobalResources(vtf_frame_data_t& frame) {

    const VkBufferCreateInfo matrices_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        sizeof(Matrices_t),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0u,
        nullptr
    };

    const VkBufferCreateInfo globals_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        sizeof(GlobalsData),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0u,
        nullptr
    };

    auto& rsrc_context = ResourceContext::Get();

    frame.rsrcMap["matrices"] = rsrc_context.CreateBuffer(&matrices_info, nullptr, 0u, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "matrices");
    frame.rsrcMap["globals"] = rsrc_context.CreateBuffer(&globals_info, nullptr, 0u, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "globals");

    auto* descr = frame.descriptorPack->RetrieveDescriptor("GlobalResources");
    descr->BindResourceToIdx(descr->BindingLocation("matrices"), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frame.rsrcMap.at("matrices"));
    descr->BindResourceToIdx(descr->BindingLocation("globals"), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frame.rsrcMap.at("globals"));

}

void createVolumetricForwardResources(vtf_frame_data_t& frame) {

    auto& camera = PerspectiveCamera::Get();
    auto* vf_descr = frame.descriptorPack->RetrieveDescriptor("VolumetricForward");

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

    auto& cluster_data = frame.rsrcMap["ClusterData"];
    if (!cluster_data) {
        cluster_data = rsrc_context.CreateBuffer(&cluster_data_info, nullptr, 1, &cluster_data_update, resource_usage::CPU_TO_GPU, DEF_RESOURCE_FLAGS, "ClusterData");
    }
    else {
        rsrc_context.SetBufferData(cluster_data, 1, &cluster_data_update);
    }

    vf_descr->BindResourceToIdx(vf_descr->BindingLocation("ClusterData"), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frame.rsrcMap.at("ClusterData"));

    const VkBufferCreateInfo cluster_flags_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        sizeof(uint32_t) * cluster_dim_x * cluster_dim_y * cluster_dim_z,
        VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
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

    auto& cluster_flags = frame.rsrcMap["ClusterFlags"];
    auto& unique_clusters = frame.rsrcMap["UniqueClusters"];
    auto& prev_unique_clusters = frame.rsrcMap["PreviousUniqueClusters"];

    if (cluster_flags) {
        rsrc_context.DestroyResource(cluster_flags);
    }
    cluster_flags = rsrc_context.CreateBuffer(&cluster_flags_info, &cluster_flags_view_info, 0, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "ClusterFlags");
    vf_descr->BindResourceToIdx(vf_descr->BindingLocation("ClusterFlags"), VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, cluster_flags);

    if (unique_clusters) {
        rsrc_context.DestroyResource(unique_clusters);
    }
    unique_clusters = rsrc_context.CreateBuffer(&cluster_flags_info, &cluster_flags_view_info, 0, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "UniqueClusters");
    vf_descr->BindResourceToIdx(vf_descr->BindingLocation("UniqueClusters"), VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, unique_clusters);

    if (prev_unique_clusters) {
        rsrc_context.DestroyResource(prev_unique_clusters);
    }
    prev_unique_clusters = rsrc_context.CreateBuffer(&cluster_flags_info, &cluster_flags_view_info, 0, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "PrevUniqueClusters");

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

    auto& assign_lights_args_buffer = frame.rsrcMap["AssignLightsToClustersArgumentBuffer"];
    if (assign_lights_args_buffer) {
        rsrc_context.DestroyResource(assign_lights_args_buffer);
    }
    assign_lights_args_buffer = rsrc_context.CreateBuffer(&indir_args_buffer, nullptr, 0, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "AssignLightsToClustersArgumentBuffer");

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

    auto& debug_clusters_indir_draw_buffer = frame.rsrcMap["DebugClustersDrawIndirectArgumentBuffer"];
    if (debug_clusters_indir_draw_buffer) {
        rsrc_context.DestroyResource(debug_clusters_indir_draw_buffer);
    }
    debug_clusters_indir_draw_buffer = rsrc_context.CreateBuffer(&debug_indirect_draw_buffer_info, nullptr, 0, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "DebugClustersDrawIndirectArgumentBuffer");

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

    auto& cluster_aabbs = frame.rsrcMap["ClusterAABBs"];
    if (cluster_aabbs) {
        rsrc_context.DestroyResource(cluster_aabbs);
    }
    cluster_aabbs = rsrc_context.CreateBuffer(&cluster_aabbs_info, nullptr, 0, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "ClusterAABBs");
    vf_descr->BindResourceToIdx(vf_descr->BindingLocation("ClusterAABBs"), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, cluster_aabbs);

    const VkBufferCreateInfo point_light_grid_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        CLUSTER_SIZE * sizeof(uint32_t) * 2,
        VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
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

    auto& point_light_grid = frame.rsrcMap["PointLightGrid"];
    if (point_light_grid) {
        rsrc_context.DestroyResource(point_light_grid);
    }
    point_light_grid = rsrc_context.CreateBuffer(&point_light_grid_info, &point_light_grid_view_info, 0, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "PointLightGrid");
    vf_descr->BindResourceToIdx(vf_descr->BindingLocation("PointLightGrid"), VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, point_light_grid);

    auto& spot_light_grid = frame.rsrcMap["SpotLightGrid"];
    if (spot_light_grid) {
        rsrc_context.DestroyResource(spot_light_grid);
    }
    spot_light_grid = rsrc_context.CreateBuffer(&point_light_grid_info, &point_light_grid_view_info, 0, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "PointLightGrid");
    vf_descr->BindResourceToIdx(vf_descr->BindingLocation("SpotLightGrid"), VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, spot_light_grid);

    const VkBufferCreateInfo indices_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        LIGHT_INDEX_LIST_SIZE * sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
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

    auto& point_light_idx_list = frame.rsrcMap["PointLightIndexList"];
    if (point_light_idx_list) {
        rsrc_context.DestroyResource(point_light_idx_list);
    }
    point_light_idx_list = rsrc_context.CreateBuffer(&indices_info, &indices_view_info, 0, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "PointLightIndexList");
    vf_descr->BindResourceToIdx(vf_descr->BindingLocation("PointLightIndexList"), VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, point_light_idx_list);

    auto& spot_light_index_list = frame.rsrcMap["SpotLightIndexList"];
    if (spot_light_index_list) {
        rsrc_context.DestroyResource(spot_light_index_list);
    }
    spot_light_index_list = rsrc_context.CreateBuffer(&indices_info, &indices_view_info, 0, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "PointLightIndexList");
    vf_descr->BindResourceToIdx(vf_descr->BindingLocation("SpotLightIndexList"), VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, spot_light_index_list);

    const VkBufferCreateInfo counter_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0u,
        nullptr
    };

    const VkBufferViewCreateInfo counter_view_info{
        VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
        nullptr,
        0,
        VK_NULL_HANDLE,
        VK_FORMAT_R32_UINT,
        0,
        sizeof(uint32_t)
    };

    frame.rsrcMap["PointLightIndexCounter"] = rsrc_context.CreateBuffer(&counter_info, &counter_view_info, 0, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "PointLightIndexCounter");
    vf_descr->BindResourceToIdx(vf_descr->BindingLocation("PointLightIndexCounter"), VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, frame.rsrcMap.at("PointLightIndexCounter"));
    frame.rsrcMap["SpotLightIndexCounter"] = rsrc_context.CreateBuffer(&counter_info, &counter_view_info, 0, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "SpotLightIndexCounter");
    vf_descr->BindResourceToIdx(vf_descr->BindingLocation("SpotLightIndexCounter"), VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, frame.rsrcMap.at("SpotLightIndexCounter"));
    frame.rsrcMap["UniqueClustersCounter"] = rsrc_context.CreateBuffer(&counter_info, &counter_view_info, 0, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "UniqueClustersCounter");
    vf_descr->BindResourceToIdx(vf_descr->BindingLocation("UniqueClustersCounter"), VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, frame.rsrcMap.at("UniqueClustersCounter"));

}

void createLightResources(vtf_frame_data_t& frame) {
    auto& rsrc_context = ResourceContext::Get();
    auto* descr = frame.descriptorPack->RetrieveDescriptor("VolumetricForwardLights");

    if (SceneLightsState().PointLights.empty()) {
        throw std::runtime_error("Didn't generate lights before setting up light GPU buffers!");
    }

    VkBufferCreateInfo lights_buffer_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        sizeof(PointLight) * SceneLightsState().PointLights.size(),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0,
        nullptr
    };

    const gpu_resource_data_t point_lights_data{
        SceneLightsState().PointLights.data(),
        SceneLightsState().PointLights.size() * sizeof(PointLight),
        0u, 0u, 0u
    };

    frame.rsrcMap["PointLights"] = rsrc_context.CreateBuffer(&lights_buffer_info, nullptr, 1, &point_lights_data, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "PointLights");
    descr->BindResourceToIdx(descr->BindingLocation("PointLights"), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, frame.rsrcMap.at("PointLights"));

    const gpu_resource_data_t spot_lights_data{
        SceneLightsState().SpotLights.data(),
        SceneLightsState().SpotLights.size() * sizeof(SpotLight),
        0u, 0u, 0u
    };

    lights_buffer_info.size = spot_lights_data.DataSize;
    frame.rsrcMap["SpotLights"] = rsrc_context.CreateBuffer(&lights_buffer_info, nullptr, 1, &spot_lights_data, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "SpotLights");
    descr->BindResourceToIdx(descr->BindingLocation("SpotLights"), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, frame.rsrcMap.at("SpotLights"));


    const gpu_resource_data_t dir_lights_data{
        SceneLightsState().DirectionalLights.data(),
        SceneLightsState().DirectionalLights.size() * sizeof(DirectionalLight),
        0u, 0u, 0u
    };
    lights_buffer_info.size = dir_lights_data.DataSize;

    frame.rsrcMap["DirectionalLights"] = rsrc_context.CreateBuffer(&lights_buffer_info, nullptr, 1, &dir_lights_data, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "DirectionalLights");
    descr->BindResourceToIdx(descr->BindingLocation("DirectionalLights"), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, frame.rsrcMap.at("DirectionalLights"));

    constexpr static VkBufferCreateInfo light_counts_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        sizeof(LightCountsData),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0u,
        nullptr
    };

    frame.LightCounts.NumPointLights = static_cast<uint32_t>(SceneLightsState().PointLights.size());
    frame.LightCounts.NumSpotLights = static_cast<uint32_t>(SceneLightsState().SpotLights.size());
    frame.LightCounts.NumDirectionalLights = static_cast<uint32_t>(SceneLightsState().DirectionalLights.size());

    const gpu_resource_data_t light_counts_data{
        &frame.LightCounts,
        sizeof(LightCountsData),
        0u, 0u, 0u
    };

    frame.rsrcMap["LightCounts"] = rsrc_context.CreateBuffer(&light_counts_info, nullptr, 1, &light_counts_data, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "LightCounts");
    descr->BindResourceToIdx(descr->BindingLocation("LightCounts"), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frame.rsrcMap.at("LightCounts"));

}

void createIndirectArgsResource(vtf_frame_data_t& frame) {

    constexpr static VkBufferCreateInfo indir_args_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        sizeof(VkDispatchIndirectCommand),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0u,
        nullptr
    };

    auto& rsrc_context = ResourceContext::Get();
    auto* descr = frame.descriptorPack->RetrieveDescriptor("IndirectArgsSet");

    frame.rsrcMap["IndirectArgs"] = rsrc_context.CreateBuffer(&indir_args_info, nullptr, 0, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "IndirectArgs");
    descr->BindResourceToIdx(descr->BindingLocation("IndirectArgs"), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, frame.rsrcMap.at("IndirectArgs"));

}

void createSortResources(vtf_frame_data_t& frame) {

    auto& rsrc_context = ResourceContext::Get();
    auto* descr = frame.descriptorPack->RetrieveDescriptor("SortResources");

    constexpr static VkBufferCreateInfo dispatch_params_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        sizeof(DispatchParams_t),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0u,
        nullptr
    };

    frame.rsrcMap["DispatchParams"] = rsrc_context.CreateBuffer(&dispatch_params_info, nullptr, 0u, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "DispatchParams");
    descr->BindResourceToIdx(descr->BindingLocation("DispatchParams"), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frame.rsrcMap.at("DispatchParams"));

    constexpr static VkBufferCreateInfo reduction_params_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0, 
        sizeof(uint32_t),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0u,
        nullptr
    };

    frame.rsrcMap["ReductionParams"] = rsrc_context.CreateBuffer(&reduction_params_info, nullptr, 0u, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "ReductionParams");
    descr->BindResourceToIdx(descr->BindingLocation("ReductionParams"), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frame.rsrcMap.at("ReductionParams"));

    constexpr static VkBufferCreateInfo sort_params_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        sizeof(uint32_t) * 2u,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0u,
        nullptr
    };

    frame.rsrcMap["SortParams"] = rsrc_context.CreateBuffer(&sort_params_info, nullptr, 0u, nullptr, resource_usage::CPU_ONLY, DEF_RESOURCE_FLAGS, "SortParams");
    descr->BindResourceToIdx(descr->BindingLocation("SortParams"), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frame.rsrcMap.at("SortParams"));

    constexpr static VkBufferCreateInfo light_aabbs_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        sizeof(AABB) * 512u,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0u,
        nullptr
    };

    frame.rsrcMap["LightAABBs"] = rsrc_context.CreateBuffer(&light_aabbs_info, nullptr, 0u, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "LightAABBs");
    descr->BindResourceToIdx(descr->BindingLocation("LightAABBs"), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, frame.rsrcMap.at("LightAABBs"));

    VkBufferCreateInfo sort_buffers_create_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        sizeof(uint32_t) * frame.LightCounts.NumPointLights,
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

    frame.rsrcMap["PointLightIndices"] = rsrc_context.CreateBuffer(&sort_buffers_create_info, &sort_buffer_views_create_info, 0, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "PointLightIndices");
    frame.rsrcMap["PointLightMortonCodes"] = rsrc_context.CreateBuffer(&sort_buffers_create_info, &sort_buffer_views_create_info, 0, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "PointLightMortonCodes");
    frame.rsrcMap["PointLightIndices_OUT"] = rsrc_context.CreateBuffer(&sort_buffers_create_info, &sort_buffer_views_create_info, 0, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "PointLightIndices_OUT");
    frame.rsrcMap["PointLightMortonCodes_OUT"] = rsrc_context.CreateBuffer(&sort_buffers_create_info, &sort_buffer_views_create_info, 0, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "PointLightMortonCodes_OUT");

    descr->BindResourceToIdx(descr->BindingLocation("PointLightIndices"), VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, frame.rsrcMap.at("PointLightIndices"));
    descr->BindResourceToIdx(descr->BindingLocation("PointLightMortonCodes"), VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, frame.rsrcMap.at("PointLightMortonCodes"));

    rsrc_context.FillBuffer(frame.rsrcMap.at("PointLightIndices"), 0u, 0u, size_t(sort_buffers_create_info.size));
    rsrc_context.FillBuffer(frame.rsrcMap.at("PointLightMortonCodes"), 0u, 0u, size_t(sort_buffers_create_info.size));
    rsrc_context.FillBuffer(frame.rsrcMap.at("PointLightIndices_OUT"), 0u, 0u, size_t(sort_buffers_create_info.size));
    rsrc_context.FillBuffer(frame.rsrcMap.at("PointLightMortonCodes_OUT"), 0u, 0u, size_t(sort_buffers_create_info.size));

    sort_buffers_create_info.size = sizeof(uint32_t) * frame.LightCounts.NumSpotLights;
    sort_buffer_views_create_info.range = sort_buffers_create_info.size;

    frame.rsrcMap["SpotLightIndices"] = rsrc_context.CreateBuffer(&sort_buffers_create_info, &sort_buffer_views_create_info, 0, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "SpotLightIndices");
    frame.rsrcMap["SpotLightMortonCodes"] = rsrc_context.CreateBuffer(&sort_buffers_create_info, &sort_buffer_views_create_info, 0, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "SpotLightMortonCodes");
    frame.rsrcMap["SpotLightIndices_OUT"] = rsrc_context.CreateBuffer(&sort_buffers_create_info, &sort_buffer_views_create_info, 0, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "SpotLightIndices_OUT");
    frame.rsrcMap["SpotLightMortonCodes_OUT"] = rsrc_context.CreateBuffer(&sort_buffers_create_info, &sort_buffer_views_create_info, 0, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "SpotLightMortonCodes_OUT");

    descr->BindResourceToIdx(descr->BindingLocation("SpotLightIndices"), VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, frame.rsrcMap.at("SpotLightIndices"));
    descr->BindResourceToIdx(descr->BindingLocation("SpotLightMortonCodes"), VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, frame.rsrcMap.at("SpotLightMortonCodes"));

    rsrc_context.FillBuffer(frame.rsrcMap.at("PointLightIndices"), 0u, 0u, size_t(sort_buffers_create_info.size));
    rsrc_context.FillBuffer(frame.rsrcMap.at("SpotLightMortonCodes"), 0u, 0u, size_t(sort_buffers_create_info.size));
    rsrc_context.FillBuffer(frame.rsrcMap.at("SpotLightIndices_OUT"), 0u, 0u, size_t(sort_buffers_create_info.size));
    rsrc_context.FillBuffer(frame.rsrcMap.at("SpotLightMortonCodes_OUT"), 0u, 0u, size_t(sort_buffers_create_info.size));

}

VulkanResource* CreateMergePathPartitions(vtf_frame_data_t& frame, bool dummy_buffer = false) {
    // merge sort resources are mostly bound before execution, except "MergePathPartitions"

    uint32_t num_chunks = static_cast<uint32_t>(glm::ceil(MAX_POINT_LIGHTS / SORT_NUM_THREADS_PER_THREAD_GROUP));
    uint32_t max_sort_groups = num_chunks / 2u;
    uint32_t path_partitions = static_cast<uint32_t>(glm::ceil((SORT_NUM_THREADS_PER_THREAD_GROUP * 2u) / (SORT_ELEMENTS_PER_THREAD * SORT_NUM_THREADS_PER_THREAD_GROUP) + 1u));
	// create it with some small but nonzero size, or with the proper size. dummy buffer is for a single case where we just need something bound in that spot
    uint32_t merge_path_partitions_buffer_sz = dummy_buffer ? sizeof(uint32_t) * 64u : path_partitions * max_sort_groups;

    const VkBufferCreateInfo merge_path_partitions_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        merge_path_partitions_buffer_sz,
        VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0u,
        nullptr
    };

    const VkBufferViewCreateInfo merge_path_partitions_view_info{
        VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
        nullptr,
        0,
        VK_NULL_HANDLE,
        VK_FORMAT_R32_SINT,
        0u,
        merge_path_partitions_info.size
    };

    auto& rsrc_context = ResourceContext::Get();
    VulkanResource* result = rsrc_context.CreateBuffer(&merge_path_partitions_info, &merge_path_partitions_view_info, 0, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "MergePathPartitions");
	return result;
}

void createBVH_Resources(vtf_frame_data_t& frame) {
    auto& rsrc_context = ResourceContext::Get();
    auto* descr = frame.descriptorPack->RetrieveDescriptor("BVHResources");

    constexpr static VkBufferCreateInfo bvh_params_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        sizeof(BVH_Params_t),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0u,
        nullptr
    };

    frame.rsrcMap["BVHParams"] = rsrc_context.CreateBuffer(&bvh_params_info, nullptr, 0u, nullptr, resource_usage::CPU_ONLY, DEF_RESOURCE_FLAGS, "BVHParams");
    descr->BindResourceToIdx(descr->BindingLocation("BVHParams"), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frame.rsrcMap.at("BVHParams"));

    const uint32_t point_light_nodes = GetNumNodesBVH(frame.LightCounts.NumPointLights);
    const uint32_t spot_light_nodes = GetNumNodesBVH(frame.LightCounts.NumSpotLights);

    VkBufferCreateInfo bvh_buffer_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        sizeof(AABB) * point_light_nodes,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0u,
        nullptr
    };
    
    frame.rsrcMap["PointLightBVH"] = rsrc_context.CreateBuffer(&bvh_buffer_info, nullptr, 0, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "PointLightBVH");
    descr->BindResourceToIdx(descr->BindingLocation("PointLightBVH"), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, frame.rsrcMap.at("PointLightBVH"));

    bvh_buffer_info.size = sizeof(AABB) * spot_light_nodes;
    frame.rsrcMap["SpotLightBVH"] = rsrc_context.CreateBuffer(&bvh_buffer_info, nullptr, 0, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "SpotLightBVH");
    descr->BindResourceToIdx(descr->BindingLocation("SpotLightBVH"), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, frame.rsrcMap.at("SpotLightBVH"));

}

void createMaterialSamplers(vtf_frame_data_t& frame) {
    // only resource from the material pack that we can create ahead of time

    constexpr static VkSamplerCreateInfo linear_repeat_sampler{
        VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        nullptr,
        0,
        VK_FILTER_LINEAR,
        VK_FILTER_LINEAR,
        VK_SAMPLER_MIPMAP_MODE_LINEAR,
        VK_SAMPLER_ADDRESS_MODE_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_REPEAT,
        0.0f,
        VK_FALSE,
        0.0f,
        VK_FALSE,
        VK_COMPARE_OP_ALWAYS,
        0.0f,
        0.0f,
        VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
        VK_FALSE
    };

    constexpr static VkSamplerCreateInfo linear_clamp_sampler{
        VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        nullptr,
        0,
        VK_FILTER_LINEAR,
        VK_FILTER_LINEAR,
        VK_SAMPLER_MIPMAP_MODE_LINEAR,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        0.0f,
        VK_FALSE,
        0.0f,
        VK_FALSE,
        VK_COMPARE_OP_ALWAYS,
        0.0f,
        0.0f,
        VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
        VK_FALSE
    };

    const VkSamplerCreateInfo anisotropic_sampler{
        VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        nullptr,
        0,
        VK_FILTER_NEAREST,
        VK_FILTER_NEAREST,
        VK_SAMPLER_MIPMAP_MODE_NEAREST,
        VK_SAMPLER_ADDRESS_MODE_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_REPEAT,
        0.0f,
        VK_TRUE,
        SceneConfig.AnisotropyAmount,
        VK_FALSE,
        VK_COMPARE_OP_NEVER,
        0.0f,
        0.0f,
        VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
        VK_FALSE
    };

    auto& rsrc_context = ResourceContext::Get();

    frame.rsrcMap["LinearRepeatSampler"] = rsrc_context.CreateSampler(&linear_repeat_sampler, ResourceCreateUserDataAsString, "LinearRepeatSampler");
    frame.rsrcMap["LinearClampSampler"] = rsrc_context.CreateSampler(&linear_clamp_sampler, ResourceCreateUserDataAsString, "LinearClampSampler");
    frame.rsrcMap["AnisotropicSampler"] = rsrc_context.CreateSampler(&anisotropic_sampler, ResourceCreateUserDataAsString, "AnisotropicSampler");

    auto* descr = frame.descriptorPack->RetrieveDescriptor("Material"); // bind samplers now
    descr->BindResourceToIdx(descr->BindingLocation("LinearRepeatSampler"), VK_DESCRIPTOR_TYPE_SAMPLER, frame.rsrcMap.at("LinearRepeatSampler"));
    descr->BindResourceToIdx(descr->BindingLocation("LinearClampSampler"), VK_DESCRIPTOR_TYPE_SAMPLER, frame.rsrcMap.at("LinearClampSampler"));
    descr->BindResourceToIdx(descr->BindingLocation("AnisotropicSampler"), VK_DESCRIPTOR_TYPE_SAMPLER, frame.rsrcMap.at("AnisotropicSampler"));

}

void createSemaphores(vtf_frame_data_t& frame) {
    auto* device = RenderingContext::Get().Device();
    frame.semaphores["ImageAcquire"] = std::make_unique<vpr::Semaphore>(device->vkHandle());
    frame.semaphores["ComputeUpdateComplete"] = std::make_unique<vpr::Semaphore>(device->vkHandle());
    frame.semaphores["RenderComplete"] = std::make_unique<vpr::Semaphore>(device->vkHandle());
}

void setupCommandPools(vtf_frame_data_t& frame)
{

	constexpr static VkCommandPoolCreateInfo command_pool_info{
		VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		nullptr,
		VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
		0u
	};
	auto* device = RenderingContext::Get().Device();
	auto pool_info = command_pool_info;
	pool_info.queueFamilyIndex = device->QueueFamilyIndices().Compute;
	frame.computePool = std::make_unique<vpr::CommandPool>(device->vkHandle(), pool_info);
	frame.computePool->AllocateCmdBuffers(4u, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
	pool_info.queueFamilyIndex = device->QueueFamilyIndices().Graphics;
	frame.graphicsPool = std::make_unique<vpr::CommandPool>(device->vkHandle(), pool_info);
	frame.graphicsPool->AllocateCmdBuffers(4u, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
	frame.vkDebugFns = device->DebugUtilsHandler();
}

void createDebugResources(vtf_frame_data_t& frame)
{
	auto& cluster_colors_vec = SceneLightsState().ClusterColors;
	VkDeviceSize required_mem = cluster_colors_vec.size() * sizeof(glm::u8vec4);

	const VkBufferCreateInfo buffer_info{
		VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		nullptr,
		0,
		required_mem,
		VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_SHARING_MODE_EXCLUSIVE,
		0u,
		nullptr
	};

	const VkBufferViewCreateInfo buffer_view_info{
		VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
		nullptr,
		0,
		VK_NULL_HANDLE,
		VK_FORMAT_R8G8B8A8_UNORM,
		0u,
		required_mem
	};

	const gpu_resource_data_t buffer_data{
		cluster_colors_vec.data(), required_mem, 0u, 0u, 0u
	};
	
	auto& ctxt = ResourceContext::Get();
	frame.rsrcMap["DebugClusterColors"] = ctxt.CreateBuffer(&buffer_info, &buffer_view_info, 1u, &buffer_data, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "DebugClusterColors");

	auto* descr = frame.descriptorPack->RetrieveDescriptor("Debug");
	const size_t loc = descr->BindingLocation("ClusterColors");
	descr->BindResourceToIdx(loc, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, frame.rsrcMap.at("DebugClusterColors"));

}

void CreateResources(vtf_frame_data_t & frame) {
    // creates and does initial descriptor binding, so that recursive/further calls 
    // to use these bindings actually work lol
    createGlobalResources(frame);
    createVolumetricForwardResources(frame);
    createLightResources(frame); // make sure lights have been generated first! we upload initial data here too
    createIndirectArgsResource(frame);
    createSortResources(frame);
    //createMergeSortResource(frame);
    createBVH_Resources(frame);
    createMaterialSamplers(frame);
    createSemaphores(frame);
	setupCommandPools(frame);
	createDebugResources(frame);
}

void CreateSemaphores(vtf_frame_data_t & frame) {
    auto* device = RenderingContext::Get().Device();
    frame.semaphores["ImageAcquire"] = std::make_unique<vpr::Semaphore>(device->vkHandle());
    frame.semaphores["ComputeUpdateComplete"] = std::make_unique<vpr::Semaphore>(device->vkHandle());
    frame.semaphores["PreBackbufferWorkComplete"] = std::make_unique<vpr::Semaphore>(device->vkHandle());
    frame.semaphores["RenderComplete"] = std::make_unique<vpr::Semaphore>(device->vkHandle());
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

	ComputePipelineCreationShim(frame, "UpdateLightsPipeline", &pipeline_info, groupName);

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

	ComputePipelineCreationShim(frame, "ReduceLightsAABB0", &reduce_lights_0_info, groupName);

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

	ComputePipelineCreationShim(frame, "ReduceLightsAABB1", &reduce_lights_1_info, groupName);

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

	ComputePipelineCreationShim(frame, "ComputeLightMortonCodesPipeline", &pipeline_info, groupName);

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

	ComputePipelineCreationShim(frame, "SortMortonCodes", &pipeline_info, groupName);

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

	ComputePipelineCreationShim(frame, "BuildBVHBottomPipeline", &pipeline_info_0, groupName);

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

	ComputePipelineCreationShim(frame, "BuildBVHTopPipeline", &pipeline_info_1, groupName);

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

	ComputePipelineCreationShim(frame, "ComputeClusterAABBsPipeline", &pipeline_info, groupName);

	frame.computeAABBsFence = std::make_unique<vpr::Fence>(device->vkHandle(), 0);
	if constexpr (VTF_VALIDATION_ENABLED && VTF_USE_DEBUG_INFO)
	{
		const std::string fence_name = groupName + std::string("_Fence");
		RenderingContext::SetObjectName(VK_OBJECT_TYPE_FENCE, (uint64_t)frame.computeAABBsFence->vkHandle(), VTF_DEBUG_OBJECT_NAME(fence_name.c_str()));
	}
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

	ComputePipelineCreationShim(frame, "UpdateIndirectArgsPipeline", &pipeline_info, groupName);

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

void createFindUniqueClustersPipeline(vtf_frame_data_t& frame)
{

	auto* device = RenderingContext::Get().Device();
	static const std::string groupName{ "FindUniqueClusters" };

	const st::ShaderStage& shader_stage = vtf_frame_data_t::groupStages.at(groupName).front();

	const VkComputePipelineCreateInfo create_info{
		VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		nullptr,
		0,
		vtf_frame_data_t::shaderModules.at(shader_stage)->PipelineInfo(),
		frame.descriptorPack->PipelineLayout(groupName),
		VK_NULL_HANDLE,
		-1
	};

	ComputePipelineCreationShim(frame, "FindUniqueClustersPipeline", &create_info, groupName);

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
	createFindUniqueClustersPipeline(frame);
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

    // create image for this pass
    frame.rsrcMap["DepthPrePassImage"] = createDepthStencilResource(VK_SAMPLE_COUNT_1_BIT);

}

void createClusterSamplesResources(vtf_frame_data_t& frame) {

    const auto* device = RenderingContext::Get().Device();
    const auto* swapchain = RenderingContext::Get().Swapchain();
    const VkFormat cluster_samples_color_format = VK_FORMAT_R8G8B8A8_UNORM;
    const VkImageTiling tiling_type = device->GetFormatTiling(cluster_samples_color_format, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT);
    const uint32_t img_width = swapchain->Extent().width;
    const uint32_t img_height = swapchain->Extent().height;

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
    frame.rsrcMap["ClusterSamplesImage"] = rsrc.CreateImage(&img_info, &view_info, 0, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "ClusterSamplesImage");
    const VkImageView view_handles[2]{
        (VkImageView)frame.rsrcMap.at("ClusterSamplesImage")->ViewHandle,
        (VkImageView)frame.rsrcMap.at("DepthPrePassImage")->ViewHandle
    };

    const VkFramebufferCreateInfo framebuffer_info{
        VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        nullptr,
        0,
        frame.renderPasses.at("DepthAndClusterSamplesPass")->vkHandle(),
        2u,
        view_handles,
        img_width,
        img_height,
        1
    };

    frame.clusterSamplesFramebuffer = std::make_unique<vpr::Framebuffer>(device->vkHandle(), framebuffer_info);

}

void createDrawPass(vtf_frame_data_t& frame) {

    const auto* device = RenderingContext::Get().Device();
    const auto* swapchain = RenderingContext::Get().Swapchain();
    std::array<VkSubpassDependency, 2> drawPassDependencies;
    std::array<VkSubpassDescription, 1> drawPassDescriptions;

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
            swapchain->ColorFormat(),
            SceneConfig.MSAA_SampleCount,
            VK_ATTACHMENT_LOAD_OP_CLEAR,
            VK_ATTACHMENT_STORE_OP_DONT_CARE,
            VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            VK_ATTACHMENT_STORE_OP_DONT_CARE,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        },
        VkAttachmentDescription{ // depth
            0,
            device->FindDepthFormat(),
            SceneConfig.MSAA_SampleCount,
            VK_ATTACHMENT_LOAD_OP_CLEAR,
            VK_ATTACHMENT_STORE_OP_DONT_CARE,
            VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            VK_ATTACHMENT_STORE_OP_DONT_CARE,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
        },
        VkAttachmentDescription{ // present src
            0,
            swapchain->ColorFormat(),
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

    frame.renderPasses["DrawPass"] = std::make_unique<vpr::Renderpass>(device->vkHandle(), create_info);
    frame.renderPasses["DrawPass"]->SetupBeginInfo(DrawPassClearValues.data(), DrawPassClearValues.size(), swapchain->Extent());

}

void createDrawingResources(vtf_frame_data_t& frame) {

    const auto* device = RenderingContext::Get().Device();
    const auto* swapchain = RenderingContext::Get().Swapchain();
    const uint32_t img_count = swapchain->ImageCount();
    const VkExtent2D& img_extent = swapchain->Extent();

    const VkImageCreateInfo img_info{
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        nullptr,
        0,
        VK_IMAGE_TYPE_2D,
        swapchain->ColorFormat(),
        VkExtent3D{ img_extent.width, img_extent.height, 1 },
        1,
        1,
        SceneConfig.MSAA_SampleCount,
        device->GetFormatTiling(swapchain->ColorFormat(), VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT),
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
    frame.rsrcMap["DepthRendertargetImage"] = createDepthStencilResource(SceneConfig.MSAA_SampleCount);
    frame.rsrcMap["DrawMultisampleImage"] = rsrc.CreateImage(&img_info, &view_info, 0u, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "DrawMultisampleImage");


}

void CreateRenderpasses(vtf_frame_data_t& frame) {
    createDrawingResources(frame); // gonna need these regardless
    createDepthAndClusterSamplesPass(frame);
    createClusterSamplesResources(frame); // required, as we have backing images and framebuffers to setup
    createDrawPass(frame);
}

void createDrawFrameBuffer(vtf_frame_data_t & frame) {

    if (frame.imageIdx == frame.lastImageIdx) {
        return; // don't need to create it again, image idx didn't change
    }

    const auto* device = RenderingContext::Get().Device();
    const auto* swapchain = RenderingContext::Get().Swapchain();
    const VkExtent2D& img_extent = swapchain->Extent();

    const VkImageView view_handles[3]{
        (VkImageView)frame.rsrcMap["DrawMultisampleImage"]->ViewHandle, (VkImageView)frame.rsrcMap["DepthRendertargetImage"]->ViewHandle,
        swapchain->ImageView(size_t(frame.imageIdx))
    };
    
    const VkFramebufferCreateInfo framebuffer_info{
        VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        nullptr,
        0,
        frame.renderPasses.at("PrimaryDrawPass")->vkHandle(),
        3,
        view_handles,
        img_extent.width,
        img_extent.height,
        1
    };

    frame.drawFramebuffer = std::make_unique<vpr::Framebuffer>(device->vkHandle(), framebuffer_info);

	if constexpr (VTF_VALIDATION_ENABLED && VTF_USE_DEBUG_INFO)
	{
		VkResult result = RenderingContext::SetObjectName(VK_OBJECT_TYPE_FRAMEBUFFER, (uint64_t)frame.drawFramebuffer->vkHandle(), VTF_DEBUG_OBJECT_NAME("DrawFramebuffer"));
		VkAssert(result);
	}
    
}

void createDepthPrePassPipeline(vtf_frame_data_t& frame) {

    auto* device = RenderingContext::Get().Device();
    static const std::string groupName{ "DepthPrePass" };
	static const std::string pipelineName{ "DepthPrePassPipeline" };
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

    frame.graphicsPipelines[pipelineName] = std::make_unique<vpr::GraphicsPipeline>(device->vkHandle());
    frame.graphicsPipelines.at(pipelineName)->Init(create_info, vtf_frame_data_t::pipelineCaches.at(groupName)->vkHandle());
	if constexpr (VTF_VALIDATION_ENABLED && VTF_USE_DEBUG_INFO)
	{
		RenderingContext::SetObjectName(VK_OBJECT_TYPE_PIPELINE, (uint64_t)frame.graphicsPipelines.at(pipelineName)->vkHandle(), pipelineName.c_str());
	}

}

void createClusterSamplesPipeline(vtf_frame_data_t& frame) {

    auto* device = RenderingContext::Get().Device();
    static const std::string groupName{ "ClusterSamples" };
	static const std::string pipelineName{ "ClusterSamplesPipeline" };

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
    create_info.subpass = 1;
    create_info.layout = frame.descriptorPack->PipelineLayout(groupName);
    create_info.renderPass = frame.renderPasses.at("DepthAndClusterSamplesPass")->vkHandle();

    frame.graphicsPipelines[pipelineName] = std::make_unique<vpr::GraphicsPipeline>(device->vkHandle());
    frame.graphicsPipelines.at(pipelineName)->Init(create_info, vtf_frame_data_t::pipelineCaches.at(groupName)->vkHandle());
	if constexpr (VTF_VALIDATION_ENABLED && VTF_USE_DEBUG_INFO)
	{
		RenderingContext::SetObjectName(VK_OBJECT_TYPE_PIPELINE, (uint64_t)frame.graphicsPipelines.at(pipelineName)->vkHandle(), pipelineName.c_str());
	}

}

void createDrawPipelines(vtf_frame_data_t& frame) {
    static const std::string groupName{ "DrawPass" };
	static const std::string opaquePipelineName{ "OpaqueDrawPipeline" };
	static const std::string transPipelineName{ "TransparentDrawPipeline" };
    auto* device = RenderingContext::Get().Device();

    const st::ShaderStage& draw_vert = vtf_frame_data_t::groupStages.at(groupName).front();
    const st::ShaderStage& draw_frag = vtf_frame_data_t::groupStages.at(groupName).back();
    const VkPipelineShaderStageCreateInfo stages[2]{
        vtf_frame_data_t::shaderModules.at(draw_vert)->PipelineInfo(),
        vtf_frame_data_t::shaderModules.at(draw_frag)->PipelineInfo()
    };

    vpr::GraphicsPipelineInfo opaque_pipeline_info;

    opaque_pipeline_info.VertexInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(VertexBindingDescriptions.size());
    opaque_pipeline_info.VertexInfo.pVertexBindingDescriptions = VertexBindingDescriptions.data();
    opaque_pipeline_info.VertexInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(VertexAttributes.size());
    opaque_pipeline_info.VertexInfo.pVertexAttributeDescriptions = VertexAttributes.data();

    opaque_pipeline_info.DynamicStateInfo.dynamicStateCount = 2;
    static constexpr VkDynamicState States[2]{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    opaque_pipeline_info.DynamicStateInfo.pDynamicStates = States;
    
    opaque_pipeline_info.DepthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    opaque_pipeline_info.DepthStencilInfo.depthTestEnable = VK_TRUE;
    opaque_pipeline_info.DepthStencilInfo.depthWriteEnable = VK_TRUE;

    opaque_pipeline_info.RasterizationInfo.cullMode = VK_CULL_MODE_BACK_BIT;

    opaque_pipeline_info.MultisampleInfo.minSampleShading = 0.25f;
    opaque_pipeline_info.MultisampleInfo.sampleShadingEnable = VK_TRUE;
    opaque_pipeline_info.MultisampleInfo.rasterizationSamples = SceneConfig.MSAA_SampleCount;

    opaque_pipeline_info.ColorBlendInfo.attachmentCount = 1u;
    opaque_pipeline_info.ColorBlendInfo.pAttachments = &DefaultBlendingAttachmentState;

    VkGraphicsPipelineCreateInfo opaque_create_info = opaque_pipeline_info.GetPipelineCreateInfo();
    
    opaque_create_info.pStages = stages;
    opaque_create_info.flags = VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT;
    opaque_create_info.stageCount = 2u;
    opaque_create_info.layout = frame.descriptorPack->PipelineLayout(groupName);
    opaque_create_info.subpass = 0;
    opaque_create_info.renderPass = frame.renderPasses.at(groupName)->vkHandle();
    opaque_create_info.basePipelineHandle = VK_NULL_HANDLE;
    opaque_create_info.basePipelineIndex = -1;

    frame.graphicsPipelines[opaquePipelineName] = std::make_unique<vpr::GraphicsPipeline>(device->vkHandle());
    frame.graphicsPipelines.at(opaquePipelineName)->Init(opaque_create_info, frame.pipelineCaches.at(groupName)->vkHandle());

    vpr::GraphicsPipelineInfo transparent_pipeline_info = opaque_pipeline_info;

    transparent_pipeline_info.RasterizationInfo.cullMode = VK_CULL_MODE_NONE;
    transparent_pipeline_info.DepthStencilInfo.depthWriteEnable = VK_FALSE;
    transparent_pipeline_info.ColorBlendInfo.pAttachments = &AlphaBlendingAttachmentState;

    VkGraphicsPipelineCreateInfo transparent_create_info = transparent_pipeline_info.GetPipelineCreateInfo();

    transparent_create_info.stageCount = 2u;
    transparent_create_info.pStages = stages;
    transparent_create_info.flags = VK_PIPELINE_CREATE_DERIVATIVE_BIT;
    transparent_create_info.layout = frame.descriptorPack->PipelineLayout(groupName);
    transparent_create_info.subpass = 0u;
    transparent_create_info.renderPass = frame.renderPasses.at(groupName)->vkHandle();
    transparent_create_info.basePipelineHandle = frame.graphicsPipelines.at("OpaqueDrawPipeline")->vkHandle();
    transparent_create_info.basePipelineIndex = -1;

    frame.graphicsPipelines[transPipelineName] = std::make_unique<vpr::GraphicsPipeline>(device->vkHandle());
    frame.graphicsPipelines.at(transPipelineName)->Init(transparent_create_info, frame.pipelineCaches.at(groupName)->vkHandle());

	if constexpr (VTF_VALIDATION_ENABLED && VTF_USE_DEBUG_INFO)
	{
		RenderingContext::SetObjectName(VK_OBJECT_TYPE_PIPELINE, (uint64_t)frame.graphicsPipelines.at(opaquePipelineName)->vkHandle(), opaquePipelineName.c_str());
		RenderingContext::SetObjectName(VK_OBJECT_TYPE_PIPELINE, (uint64_t)frame.graphicsPipelines.at(transPipelineName)->vkHandle(), transPipelineName.c_str());
	}

}

void CreateGraphicsPipelines(vtf_frame_data_t & frame) {
    createDepthPrePassPipeline(frame);
    createClusterSamplesPipeline(frame);
    createDrawPipelines(frame);
}

void miscSetup(vtf_frame_data_t& frame) {

    if (VTF_VALIDATION_ENABLED) {
        frame.vkDebugFns = RenderingContext::Get().Device()->DebugUtilsHandler();
    }

}

void FullFrameSetup(vtf_frame_data_t* frame) {
    SetupDescriptors(*frame);
    CreateSemaphores(*frame);
    CreateResources(*frame);
    CreateComputePipelines(*frame);
    CreateRenderpasses(*frame);
    CreateGraphicsPipelines(*frame);
    miscSetup(*frame);
}

void CalculateGridDims(uint32_t& grid_x, uint32_t& grid_y, uint32_t& grid_z)
{
	auto& camera = PerspectiveCamera::Get();
	float fov_y = camera.FOV();
	float z_near = camera.NearPlane();
	float z_far = camera.FarPlane();

	auto& ctxt = RenderingContext::Get();
	const uint32_t window_width = ctxt.Swapchain()->Extent().width;
	const uint32_t window_height = ctxt.Swapchain()->Extent().height;

	grid_x = static_cast<uint32_t>(glm::ceil(window_width / float(CLUSTER_GRID_BLOCK_SIZE)));
	grid_y = static_cast<uint32_t>(glm::ceil(window_height / float(CLUSTER_GRID_BLOCK_SIZE)));

	float sD = 2.0f * glm::tan(fov_y) / float(grid_y);
	float log_dim_y = 1.0f / glm::log(1.0f + sD);
	float log_depth = glm::log(z_far / z_near);

	grid_z = static_cast<uint32_t>(glm::floor(log_depth * log_dim_y));
}

void ComputeUpdateLights(vtf_frame_data_t& frame) {

    constexpr static VkDebugUtilsLabelEXT debug_label {
        VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        nullptr,
        "UpdateLights",
        { 241.0f / 255.0f, 66.0f / 255.0f, 244.0f / 255.0f, 1.0f }
    };

    auto& rsrc = ResourceContext::Get();

    // update light counts
    frame.LightCounts.NumPointLights = static_cast<uint32_t>(SceneLightsState().PointLights.size());
    frame.LightCounts.NumSpotLights = static_cast<uint32_t>(SceneLightsState().SpotLights.size());
    frame.LightCounts.NumDirectionalLights = static_cast<uint32_t>(SceneLightsState().DirectionalLights.size());
    VulkanResource* light_counts_buffer = frame.rsrcMap["LightCounts"];
    const gpu_resource_data_t lcb_update{
        &frame.LightCounts,
        sizeof(LightCountsData)
    };
    rsrc.SetBufferData(light_counts_buffer, 1, &lcb_update);

    // update light positions etc
    auto cmd = frame.computePool->GetCmdBuffer(0u);
    if constexpr (VTF_VALIDATION_ENABLED) {
        frame.vkDebugFns.vkCmdBeginDebugUtilsLabel(cmd, &debug_label);
    }
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, frame.computePipelines["UpdateLightsPipeline"].Handle);
    auto& binder = frame.GetBinder("UpdateLights");
	binder.Update();
    binder.Bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
    uint32_t num_groups_x = glm::max(frame.LightCounts.NumPointLights, glm::max(frame.LightCounts.NumDirectionalLights, frame.LightCounts.NumSpotLights));
    num_groups_x = static_cast<uint32_t>(glm::ceil(num_groups_x / 1024.0f));
    vkCmdDispatch(cmd, num_groups_x, 1, 1);
    if constexpr (VTF_VALIDATION_ENABLED) {
        frame.vkDebugFns.vkCmdEndDebugUtilsLabel(cmd);
    }

}

void ComputeReduceLights(vtf_frame_data_t& frame) {

    constexpr static VkDebugUtilsLabelEXT debug_label{
        VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        nullptr,
        "ReduceLights",
        { 182.0f / 255.0f, 66.0f / 255.0f, 244.0f / 255.0f, 1.0f }
    };

    auto& rsrc = ResourceContext::Get();

    Descriptor* descr = frame.descriptorPack->RetrieveDescriptor("SortResources");
    const size_t dispatch_params_idx = descr->BindingLocation("DispatchParams");

    constexpr static VkBufferCreateInfo dispatch_params_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        sizeof(DispatchParams_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0,
        nullptr
    };

    // update dispatch params
    uint32_t num_thread_groups = glm::min<uint32_t>(static_cast<uint32_t>(glm::ceil(glm::max(frame.LightCounts.NumPointLights, frame.LightCounts.NumSpotLights) / 512.0f)), uint32_t(512));
    frame.DispatchParams.NumThreadGroups = glm::uvec3(num_thread_groups, 1u, 1u);
    frame.DispatchParams.NumThreads = glm::uvec3(num_thread_groups * 512, 1u, 1u);
    const gpu_resource_data_t dp_update{
        &frame.DispatchParams,
        sizeof(DispatchParams_t)
    };

    auto& dispatch_params0 = frame.rsrcMap["DispatchParams0"];
    if (dispatch_params0) {
        rsrc.SetBufferData(dispatch_params0, 1, &dp_update);
    }
    else {
        dispatch_params0 = rsrc.CreateBuffer(&dispatch_params_info, nullptr, 1, &dp_update, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "DispatchParams0");
    }

    auto& binder0 = frame.GetBinder("ReduceLights");
    binder0.BindResourceToIdx("SortResources", dispatch_params_idx, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, dispatch_params0);
    binder0.Update();

    // Reduce lights
    auto cmd = frame.computePool->GetCmdBuffer(0);
    if constexpr (VTF_VALIDATION_ENABLED) {
        frame.vkDebugFns.vkCmdBeginDebugUtilsLabel(cmd, &debug_label);
    }
    binder0.Bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, frame.computePipelines.at("ReduceLightsAABB0").Handle);
    vkCmdDispatch(cmd, num_thread_groups, 1u, 1u);

    // second step of reduction
    frame.DispatchParams.NumThreadGroups = glm::uvec3{ 1u, 1u, 1u };
    frame.DispatchParams.NumThreads = glm::uvec3{ 512u, 1u, 1u };
    auto& dispatch_params1 = frame.rsrcMap["DispatchParams1"];
    if (dispatch_params1) {
        rsrc.SetBufferData(dispatch_params1, 1, &dp_update);
    }
    else {
        dispatch_params1 = rsrc.CreateBuffer(&dispatch_params_info, nullptr, 1, &dp_update, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "DispatchParams1");
    }
    auto& binder1 = frame.GetBinder("ReduceLights");
    binder1.BindResourceToIdx("SortResources", dispatch_params_idx, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, dispatch_params1);
    binder1.Update();
    binder1.Bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, frame.computePipelines.at("ReduceLightsAABB1").Handle);
    vkCmdDispatch(cmd, 1u, 1u, 1u);
    if constexpr (VTF_VALIDATION_ENABLED) {
        frame.vkDebugFns.vkCmdEndDebugUtilsLabel(cmd);
    }

}

void ComputeMortonCodes(vtf_frame_data_t& frame) {

    constexpr static VkDebugUtilsLabelEXT debug_label{
        VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        nullptr,
        "ComputeMortonCodes",
        { 122.0f / 255.0f, 66.0f / 255.0f, 244.0f / 255.0f, 1.0f }
    };

    auto& rsrc = ResourceContext::Get();
    auto cmd = frame.computePool->GetCmdBuffer(0);
    auto binder = frame.descriptorPack->RetrieveBinder("ComputeMortonCodes");
    auto& pointLightIndices = frame.rsrcMap["PointLightIndices"];
    auto& spotLightIndices = frame.rsrcMap["SpotLightIndices"];
    auto& pointLightMortonCodes = frame.rsrcMap["PointLightMortonCodes"];
    auto& spotLightMortonCodes = frame.rsrcMap["SpotLightMortonCodes"];
    auto& pointLightMortonCodes_OUT = frame.rsrcMap["PointLightMortonCodes_OUT"];
    auto& pointLightIndices_OUT = frame.rsrcMap["PointLightIndices_OUT"];
    auto& spotLightMortonCodes_OUT = frame.rsrcMap["SpotLightMortonCodes_OUT"];
    auto& spotLightIndices_OUT = frame.rsrcMap["SpotLightIndices_OUT"];

    const VkDeviceSize point_light_indices_size = reinterpret_cast<const VkBufferCreateInfo*>(pointLightIndices->Info)->size;
    const VkDeviceSize spot_light_indices_size = reinterpret_cast<const VkBufferCreateInfo*>(spotLightIndices->Info)->size;
    const VkDeviceSize point_light_codes_size = reinterpret_cast<const VkBufferCreateInfo*>(pointLightMortonCodes->Info)->size;
    const VkDeviceSize spot_light_codes_size = reinterpret_cast<const VkBufferCreateInfo*>(spotLightMortonCodes->Info)->size;

    const std::array<VkBufferMemoryBarrier, 4> compute_morton_codes_barriers {
        VkBufferMemoryBarrier{
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            nullptr,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkBuffer)pointLightIndices->Handle,
            0u,
            point_light_indices_size
        },
        VkBufferMemoryBarrier{
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            nullptr,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkBuffer)spotLightIndices->Handle,
            0u,
            spot_light_indices_size
        },
        VkBufferMemoryBarrier{
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            nullptr,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkBuffer)pointLightMortonCodes->Handle,
            0u,
            point_light_codes_size
        },
        VkBufferMemoryBarrier{
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            nullptr,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkBuffer)spotLightMortonCodes->Handle,
            0u,
            spot_light_codes_size
        }
    };

    if constexpr (VTF_VALIDATION_ENABLED) {
        frame.vkDebugFns.vkCmdBeginDebugUtilsLabel(cmd, &debug_label);
    }
	binder.Update();
    binder.Bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, frame.computePipelines["ComputeLightMortonCodesPipeline"].Handle);
    uint32_t num_thread_groups = static_cast<uint32_t>(glm::ceil(glm::max(frame.LightCounts.NumPointLights, frame.LightCounts.NumSpotLights) / 1024.0f));
    vkCmdDispatch(cmd, num_thread_groups, 1, 1);
    // barrier to make sure writes from this dispatch finish before we try to sort
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0u, nullptr, static_cast<uint32_t>(compute_morton_codes_barriers.size()), 
        compute_morton_codes_barriers.data(), 0u, nullptr);
    if constexpr (VTF_VALIDATION_ENABLED) {
        frame.vkDebugFns.vkCmdEndDebugUtilsLabel(cmd);
    }

}

void SortMortonCodes(vtf_frame_data_t& frame) {

    constexpr static VkDebugUtilsLabelEXT debug_label{
        VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        nullptr,
        "SortMortonCodes",
        { 66.0f / 255.0f, 119.0f / 255.0f, 244.0f / 255.0f, 1.0f }
    };

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
    auto binder = frame.descriptorPack->RetrieveBinder("RadixSort");

    auto& pointLightIndices = frame.rsrcMap["PointLightIndices"];
    auto& spotLightIndices = frame.rsrcMap["SpotLightIndices"];
    auto& pointLightMortonCodes = frame.rsrcMap["PointLightMortonCodes"];
    auto& spotLightMortonCodes = frame.rsrcMap["SpotLightMortonCodes"];
    auto& pointLightMortonCodes_OUT = frame.rsrcMap["PointLightMortonCodes_OUT"];
    auto& pointLightIndices_OUT = frame.rsrcMap["PointLightIndices_OUT"];
    auto& spotLightMortonCodes_OUT = frame.rsrcMap["SpotLightMortonCodes_OUT"];
    auto& spotLightIndices_OUT = frame.rsrcMap["SpotLightIndices_OUT"];

    SortParams sort_params;
    sort_params.ChunkSize = SORT_NUM_THREADS_PER_THREAD_GROUP;
    const VkBufferCopy point_light_copy{ 0, 0, uint32_t(reinterpret_cast<const VkBufferCreateInfo*>(pointLightIndices->Info)->size) };
    const VkBufferCopy spot_light_copy{ 0, 0, uint32_t(reinterpret_cast<const VkBufferCreateInfo*>(spotLightIndices->Info)->size) };

    auto& sort_params_rsrc = frame.rsrcMap["SortParams"];
    const gpu_resource_data_t sort_params_copy{ &sort_params, sizeof(SortParams), 0u, 0u, 0u };

	// TODO: Create a way to not have to do this
	// Create a dummy resource so that things don't break for us.
	VulkanResource* merge_path_partitions_empty = CreateMergePathPartitions(frame);

    // prefetch binding locations
    Descriptor* sort_descriptor = frame.descriptorPack->RetrieveDescriptor("MergeSortResources");
    const size_t src_keys_loc = sort_descriptor->BindingLocation("InputKeys");
    const size_t dst_keys_loc = sort_descriptor->BindingLocation("OutputKeys");
    const size_t src_values_loc = sort_descriptor->BindingLocation("InputValues");
    const size_t dst_values_loc = sort_descriptor->BindingLocation("OutputValues");
	const size_t mpp_loc = sort_descriptor->BindingLocation("MergePathPartitions");
	binder.BindResourceToIdx("MergeSortResources", mpp_loc, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, merge_path_partitions_empty);

    const VkDeviceSize point_light_indices_size = reinterpret_cast<const VkBufferCreateInfo*>(pointLightIndices->Info)->size;
    const VkDeviceSize spot_light_indices_size = reinterpret_cast<const VkBufferCreateInfo*>(spotLightIndices->Info)->size;
    const VkDeviceSize point_light_codes_size = reinterpret_cast<const VkBufferCreateInfo*>(pointLightMortonCodes->Info)->size;
    const VkDeviceSize spot_light_codes_size = reinterpret_cast<const VkBufferCreateInfo*>(spotLightMortonCodes->Info)->size;

    std::array<VkBufferMemoryBarrier, 4> radix_sort_barriers{
        // Input keys
        VkBufferMemoryBarrier{
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            nullptr,
            VK_ACCESS_SHADER_READ_BIT,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkBuffer)pointLightMortonCodes->Handle,
            0u,
            point_light_codes_size
        },
        // Input values
        VkBufferMemoryBarrier{
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            nullptr,
            VK_ACCESS_SHADER_READ_BIT,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkBuffer)pointLightIndices->Handle,
            0u,
            point_light_indices_size
        },
        // Output keys
        VkBufferMemoryBarrier{
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            nullptr,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkBuffer)pointLightMortonCodes_OUT->Handle,
            0u,
            point_light_codes_size
        },
        // Output values
        VkBufferMemoryBarrier{
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            nullptr,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkBuffer)pointLightIndices_OUT->Handle,
            0u,
            point_light_indices_size
        }
    };

    if constexpr (VTF_VALIDATION_ENABLED) {
        frame.vkDebugFns.vkCmdBeginDebugUtilsLabel(cmd, &debug_label);
    }

    // bind radix sort pipeline now
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, frame.computePipelines["SortMortonCodes"].Handle);

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

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0u, nullptr,
            static_cast<uint32_t>(radix_sort_barriers.size()), radix_sort_barriers.data(), 0u, nullptr);

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
        binder.Bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);

        uint32_t num_thread_groups = static_cast<uint32_t>(glm::ceil(float(frame.LightCounts.NumSpotLights) / float(SORT_NUM_THREADS_PER_THREAD_GROUP)));
        vkCmdDispatch(cmd, num_thread_groups, 1u, 1u);

        vkCmdCopyBuffer(cmd, (VkBuffer)spotLightMortonCodes_OUT->Handle, (VkBuffer)spotLightMortonCodes->Handle, 1, &spot_light_copy);
        vkCmdCopyBuffer(cmd, (VkBuffer)spotLightIndices_OUT->Handle, (VkBuffer)spotLightIndices->Handle, 1, &spot_light_copy);

        radix_sort_barriers[0].buffer = (VkBuffer)spotLightMortonCodes->Handle;
        radix_sort_barriers[0].size = spot_light_codes_size;
        radix_sort_barriers[1].buffer = (VkBuffer)spotLightMortonCodes_OUT->Handle;
        radix_sort_barriers[1].size = spot_light_codes_size;
        radix_sort_barriers[2].buffer = (VkBuffer)spotLightIndices->Handle;
        radix_sort_barriers[2].size = spot_light_indices_size;
        radix_sort_barriers[3].buffer = (VkBuffer)spotLightIndices_OUT->Handle;
        radix_sort_barriers[3].size = spot_light_indices_size;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0u, nullptr,
            static_cast<uint32_t>(radix_sort_barriers.size()), radix_sort_barriers.data(), 0u, nullptr);

    }

    if (frame.LightCounts.NumPointLights > 0u) {
        MergeSort(frame, cmd, pointLightMortonCodes, pointLightIndices, pointLightMortonCodes_OUT, pointLightIndices_OUT, frame.LightCounts.NumPointLights, SORT_NUM_THREADS_PER_THREAD_GROUP, binder);
    }

    if (frame.LightCounts.NumSpotLights > 0u) {
        MergeSort(frame, cmd, spotLightMortonCodes, spotLightIndices, spotLightMortonCodes_OUT, spotLightIndices_OUT, frame.LightCounts.NumSpotLights, SORT_NUM_THREADS_PER_THREAD_GROUP, binder);
    }

    if constexpr (VTF_VALIDATION_ENABLED) {
        frame.vkDebugFns.vkCmdEndDebugUtilsLabel(cmd);
    }

}

void BuildLightBVH(vtf_frame_data_t& frame) {

    constexpr static VkDebugUtilsLabelEXT debug_label{
        VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        nullptr,
        "BuildLightBVH",
        { 66.0f / 255.0f, 209.0f / 255.0f, 244.0f / 255.0f, 1.0f }
    };

    const uint32_t compute_idx = RenderingContext::Get().Device()->QueueFamilyIndices().Compute;
    
    auto& rsrc = ResourceContext::Get();
    auto cmd = frame.computePool->GetCmdBuffer(0);
    auto& bvh_params_rsrc = frame.rsrcMap["BVHParams"];
    auto& point_light_bvh = frame.rsrcMap["PointLightBVH"];
    auto& spot_light_bvh = frame.rsrcMap["SpotLightBVH"];

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

    if constexpr (VTF_VALIDATION_ENABLED) {
        frame.vkDebugFns.vkCmdBeginDebugUtilsLabel(cmd, &debug_label);
    }

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
	binder.Update();
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

    if constexpr (VTF_VALIDATION_ENABLED) {
        frame.vkDebugFns.vkCmdBeginDebugUtilsLabel(cmd, &debug_label);
    }

}

void UpdateClusterGrid(vtf_frame_data_t& frame) {
    frame.updateUniqueClusters = true;
    ComputeClusterAABBs(frame);
}

void ComputeClusterAABBs(vtf_frame_data_t& frame) {

    constexpr static VkDebugUtilsLabelEXT debug_label{
        VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        nullptr,
        "ComputerClusterAABBs",
        { 113.0f / 255.0f, 244.0f / 255.0f, 66.0f / 255.0f, 1.0f }
    };

    auto* device = RenderingContext::Get().Device();

    constexpr static VkCommandBufferBeginInfo compute_begin_info{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        nullptr,
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        nullptr
    };

    VkCommandBuffer cmd_buffer = frame.computePool->GetCmdBuffer(0u);

    vkBeginCommandBuffer(cmd_buffer, &compute_begin_info);
    if constexpr (VTF_VALIDATION_ENABLED) {
        frame.vkDebugFns.vkCmdBeginDebugUtilsLabel(cmd_buffer, &debug_label);
    }
    auto& binder = frame.GetBinder("ComputeClusterAABBs");
	binder.Update();
    binder.Bind(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE);
    vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, frame.computePipelines["ComputeClusterAABBsPipeline"].Handle);
    const uint32_t dispatch_size = static_cast<uint32_t>(glm::ceil((frame.ClusterData.GridDim.x * frame.ClusterData.GridDim.y * frame.ClusterData.GridDim.z) / 1024.0f));
    vkCmdDispatch(cmd_buffer, dispatch_size, 1u, 1u);
    if constexpr (VTF_VALIDATION_ENABLED) {
        frame.vkDebugFns.vkCmdEndDebugUtilsLabel(cmd_buffer);
    }
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

void SubmitComputeWork(vtf_frame_data_t& frame) {

    constexpr static VkPipelineStageFlags wait_mask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    auto* device = RenderingContext::Get().Device();

    const VkSubmitInfo submit_info{
        VK_STRUCTURE_TYPE_SUBMIT_INFO,
        nullptr,
        0u,
        nullptr,
        0,
        1,
        &frame.computePool->GetCmdBuffer(0u),
        1,
        &frame.semaphores.at("ComputeUpdateComplete")->vkHandle()
    };
    VkResult result = vkQueueSubmit(device->ComputeQueue(), 1, &submit_info, VK_NULL_HANDLE);
    VkAssert(result);

}

bool tryImageAcquire(vtf_frame_data_t& frame, uint64_t timeout = 0u) {
    auto* device = RenderingContext::Get().Device();
    auto* swapchain = RenderingContext::Get().Swapchain();
    VkResult result = vkAcquireNextImageKHR(device->vkHandle(), swapchain->vkHandle(), timeout, frame.semaphores.at("ImageAcquire")->vkHandle(), VK_NULL_HANDLE, &frame.imageIdx);
    if (result == VK_SUCCESS) {
        return true;
    }
    else {
        if (result != VK_NOT_READY && timeout == 0u) {
            VkAssert(result); // something besides what we plan to handle occured
        }
        return false;
    }
}

void getClusterSamples(vtf_frame_data_t& frame) {

    constexpr static VkDebugUtilsLabelEXT debug_label{
        VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        nullptr,
        "DepthPrePassAndClusterSamples",
        { 244.0f / 255.0f, 66.0f / 255.0f, 66.0f / 255.0f, 1.0f }
    };

    auto* renderpass = frame.renderPasses.at("DepthAndClusterSamplesPass").get();
    renderpass->UpdateBeginInfo(frame.clusterSamplesFramebuffer->vkHandle());

    VkCommandBuffer cmd = frame.graphicsPool->GetCmdBuffer(0u);
    constexpr static VkCommandBufferBeginInfo begin_info{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        nullptr,
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        nullptr
    };

    auto& cluster_flags = frame.rsrcMap.at("ClusterFlags");

    const VkBufferMemoryBarrier cluster_flags_barrier{
        VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        nullptr,
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_QUEUE_FAMILY_IGNORED,
        VK_QUEUE_FAMILY_IGNORED,
        (VkBuffer)cluster_flags->Handle,
        0u,
        reinterpret_cast<const VkBufferCreateInfo*>(cluster_flags->Info)->size
    };

    vkBeginCommandBuffer(cmd, &begin_info);
    if constexpr (VTF_VALIDATION_ENABLED) {
        frame.vkDebugFns.vkCmdBeginDebugUtilsLabel(cmd, &debug_label);
    }
    vkCmdFillBuffer(cmd, (VkBuffer)cluster_flags->Handle, 0u, reinterpret_cast<const VkBufferCreateInfo*>(cluster_flags->Info)->size, 0u);
    auto& binder0 = frame.GetBinder("DepthPrePass");
	binder0.Update();
    auto& binder1 = frame.GetBinder("ClusterSamples");
	binder1.Update();
	assert(frame.renderFns.size() == frame.bindFns.size());
    vkCmdBeginRenderPass(cmd, &renderpass->BeginInfo(), VK_SUBPASS_CONTENTS_INLINE);
        // first run depth pre-pass, then run cluster sampling
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, frame.graphicsPipelines.at("DepthPrePassPipeline")->vkHandle());
        binder0.Bind(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS);
		for (size_t i = 0; i < frame.renderFns.size(); ++i)
		{
			frame.renderFns[i](cmd, &binder0, vtf_frame_data_t::render_type::Opaque); // opaque for depth
		}
    vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_INLINE);
        // now run cluster sampling
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, frame.graphicsPipelines.at("ClusterSamplesPipeline")->vkHandle());
        binder1.Bind(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS);
		for (size_t i = 0; i < frame.renderFns.size(); ++i)
		{
			frame.renderFns[i](cmd, &binder0, vtf_frame_data_t::render_type::Opaque); // opaque for depth
		}
    vkCmdEndRenderPass(cmd);
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0u, nullptr,
        1u, &cluster_flags_barrier, 0u, nullptr); // make sure writes to cluster flags complete before it's used by FindUniqueClusters
    if constexpr (VTF_VALIDATION_ENABLED) {
        frame.vkDebugFns.vkCmdEndDebugUtilsLabel(cmd);
    }

}

void findUniqueClusters(vtf_frame_data_t& frame) {

    constexpr static VkDebugUtilsLabelEXT debug_label{
        VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        nullptr,
        "FindUniqueClusters",
        { 244.0f / 255.0f, 134.0f / 255.0f, 66.0f / 255.0f, 1.0f }
    };

    auto& unique_clusters = frame.rsrcMap.at("UniqueClusters");
    const VkDeviceSize unique_clusters_size = reinterpret_cast<const VkBufferCreateInfo*>(unique_clusters->Info)->size;
    auto& unique_clusters_counter = frame.rsrcMap.at("UniqueClustersCounter");
    VkCommandBuffer cmd = frame.graphicsPool->GetCmdBuffer(0u);
    if constexpr (VTF_VALIDATION_ENABLED) {
        frame.vkDebugFns.vkCmdBeginDebugUtilsLabel(cmd, &debug_label);
    }
    vkCmdFillBuffer(cmd, (VkBuffer)unique_clusters->Handle, 0u, unique_clusters_size, 0u);
    vkCmdFillBuffer(cmd, (VkBuffer)unique_clusters->Handle, 0u, sizeof(uint32_t), 0u);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, frame.computePipelines.at("FindUniqueClustersPipeline").Handle);
    auto& binder = frame.GetBinder("FindUniqueClusters");
	binder.Update();
    binder.Bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);

    uint32_t max_clusters = frame.ClusterData.GridDim.x * frame.ClusterData.GridDim.y * frame.ClusterData.GridDim.z;
    uint32_t num_thread_groups = static_cast<uint32_t>(glm::ceil((float)max_clusters / 1024.0f));
    vkCmdDispatch(cmd, num_thread_groups, 1u, 1u);

    const VkBufferMemoryBarrier unique_clusters_counter_barrier {
        VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        nullptr,
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_QUEUE_FAMILY_IGNORED,
        VK_QUEUE_FAMILY_IGNORED,
        (VkBuffer)unique_clusters_counter->Handle,
        0u,
        sizeof(uint32_t)
    };
    // gotta flush writes before executing update indirect args shader
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0u, nullptr, 1u, &unique_clusters_counter_barrier, 0u, nullptr);
    if constexpr (VTF_VALIDATION_ENABLED) {
        frame.vkDebugFns.vkCmdEndDebugUtilsLabel(cmd);
    }

}

void updateClusterIndirectArgs(vtf_frame_data_t& frame) {

    constexpr static VkDebugUtilsLabelEXT debug_label{
        VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        nullptr,
        "UpdateClusterIndirectArgs",
        { 244.0f / 255.0f, 176.0f / 255.0f, 66.0f / 255.0f, 1.0f }
    };

    auto& indir_args_buffer = frame.rsrcMap.at("IndirectArgs");
    const VkBufferMemoryBarrier indir_args_buffer_barrier{
        VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        nullptr,
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
        VK_QUEUE_FAMILY_IGNORED,
        VK_QUEUE_FAMILY_IGNORED,
        (VkBuffer)indir_args_buffer->Handle,
        0u,
        sizeof(VkDispatchIndirectCommand)
    };

    VkCommandBuffer cmd = frame.graphicsPool->GetCmdBuffer(0u);
    if constexpr (VTF_VALIDATION_ENABLED) {
        frame.vkDebugFns.vkCmdBeginDebugUtilsLabel(cmd, &debug_label);
    }
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, frame.computePipelines.at("UpdateIndirectArgsPipeline").Handle);
    auto& binder = frame.GetBinder("UpdateClusterIndirectArgs");
	binder.Update();
    binder.Bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
    vkCmdDispatch(cmd, 1u, 1u, 1u);
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0, 0u, nullptr, 1u, &indir_args_buffer_barrier, 0u, nullptr);
    if constexpr (VTF_VALIDATION_ENABLED) {
        frame.vkDebugFns.vkCmdEndDebugUtilsLabel(cmd);
    }

}

void assignLightsToClusters(vtf_frame_data_t& frame) {
    auto& rsrc_ctxt = ResourceContext::Get();

    constexpr static const char* label_name{ "AssignLightsToClusters" };
    constexpr static VkDebugUtilsLabelEXT debug_label{
        VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        nullptr,
        label_name,
        { 244.0f / 255.0f, 229.0f / 255.0f, 66.0f / 255.0f, 1.0f }
    };

    const VulkanResource* point_light_index_counter = frame.rsrcMap.at("PointLightIndexCounter");
    const VulkanResource* spot_light_index_counter = frame.rsrcMap.at("SpotLightIndexCounter");
    const VulkanResource* point_light_grid = frame.rsrcMap.at("PointLightGrid");
    const VulkanResource* spot_light_grid = frame.rsrcMap.at("SpotLightGrid");
    const VulkanResource* point_light_index_list = frame.rsrcMap.at("PointLightIndexList");
    const VulkanResource* spot_light_index_list = frame.rsrcMap.at("SpotLightIndexList");

    /*
        These resources are all written in this stage, then read by the rendering pass
    */
    const VkBufferMemoryBarrier assign_lights_barriers[4]{
        VkBufferMemoryBarrier{
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            nullptr,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkBuffer)point_light_index_list->Handle,
            0u,
            reinterpret_cast<const VkBufferCreateInfo*>(point_light_index_list->Info)->size
        },
        VkBufferMemoryBarrier{
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            nullptr,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkBuffer)spot_light_index_list->Handle,
            0u,
            reinterpret_cast<const VkBufferCreateInfo*>(spot_light_index_list->Info)->size
        },
        VkBufferMemoryBarrier{
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            nullptr,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkBuffer)point_light_grid->Handle,
            0u,
            reinterpret_cast<const VkBufferCreateInfo*>(point_light_grid->Info)->size
        },
        VkBufferMemoryBarrier{
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            nullptr,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkBuffer)spot_light_grid->Handle,
            0u,
            reinterpret_cast<const VkBufferCreateInfo*>(spot_light_grid->Info)->size
        }
    };

    frame.BVH_Params.PointLightLevels = GetNumLevelsBVH(frame.LightCounts.NumPointLights);
    frame.BVH_Params.SpotLightLevels = GetNumLevelsBVH(frame.LightCounts.NumSpotLights);

    const gpu_resource_data_t bvh_params_update {
        &frame.LightCounts,
        sizeof(LightCountsData),
        0u, 0u, 0u
    };

    VulkanResource* bvh_params_rsrc = frame.rsrcMap.at("BVHParams");
    rsrc_ctxt.SetBufferData(bvh_params_rsrc, 1u, &bvh_params_update);
    
    VkCommandBuffer cmd = frame.graphicsPool->GetCmdBuffer(0u);
    if constexpr (VTF_VALIDATION_ENABLED) {
        frame.vkDebugFns.vkCmdBeginDebugUtilsLabel(cmd, &debug_label);
    }
    vkCmdFillBuffer(cmd, (VkBuffer)point_light_index_counter->Handle, 0u, sizeof(uint32_t), 0u);
    vkCmdFillBuffer(cmd, (VkBuffer)spot_light_index_counter->Handle, 0u, sizeof(uint32_t), 0u);
    vkCmdFillBuffer(cmd, (VkBuffer)point_light_grid->Handle, 0u, reinterpret_cast<const VkBufferCreateInfo*>(point_light_grid->Info)->size, 0u);
    vkCmdFillBuffer(cmd, (VkBuffer)spot_light_grid->Handle, 0u, reinterpret_cast<const VkBufferCreateInfo*>(spot_light_grid->Info)->size, 0u);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, frame.computePipelines.at("AssignLightsToClustersBVH").Handle);
    auto& descriptor = frame.GetBinder("AssignLightsToClustersBVH");
    descriptor.Bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
    VulkanResource* indirect_buffer = frame.rsrcMap.at("IndirectArgs");
    vkCmdDispatchIndirect(cmd, (VkBuffer)indirect_buffer->Handle, 0u);
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0u, nullptr, 4u, assign_lights_barriers, 0u, nullptr);
    if constexpr (VTF_VALIDATION_ENABLED) {
        frame.vkDebugFns.vkCmdEndDebugUtilsLabel(cmd);
    }
    VkResult result = vkEndCommandBuffer(cmd);
    VkAssert(result);

}

void submitPreSwapchainWritingWork(vtf_frame_data_t& frame) {
    // We're going to submit all work so far, as it doesn't write to the swapchain 
    // and the next chunk of work relies on acquire completing

    const std::array<VkSemaphore, 1> wait_semaphores{
        frame.semaphores.at("ComputeUpdateComplete")->vkHandle()
    };

    const std::array<VkSemaphore, 1> signal_semaphores{
        frame.semaphores.at("PreBackbufferWorkComplete")->vkHandle()
    };

    constexpr static VkPipelineStageFlags wait_mask = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT; // we're waiting a bit early tbh

    const VkSubmitInfo submit_info{
        VK_STRUCTURE_TYPE_SUBMIT_INFO,
        nullptr,
        static_cast<uint32_t>(wait_semaphores.size()),
        wait_semaphores.data(),
        &wait_mask,
        1u,
        &frame.graphicsPool->GetCmdBuffer(0u),
        static_cast<uint32_t>(signal_semaphores.size()),
        signal_semaphores.data()
    };

    vpr::Device* dvc = RenderingContext::Get().Device();

    VkResult result = vkQueueSubmit(dvc->GraphicsQueue(), 1u, &submit_info, VK_NULL_HANDLE);
    VkAssert(result);

}

void vtfMainRenderPass(vtf_frame_data_t& frame) {

    constexpr static VkDebugUtilsLabelEXT debug_label{
        VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        nullptr, 
        "DrawPass",
        { 66.0f / 255.0f, 244.0f / 255.0f, 95.0f / 255.0f, 1.0f }
    };

    auto& rsrc_ctxt = ResourceContext::Get();
    auto& binder = frame.GetBinder("DrawPass");

    // get diff command buffer
    VkCommandBuffer cmd = frame.graphicsPool->GetCmdBuffer(1u);

    const VkCommandBufferBeginInfo begin_info{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        nullptr,
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        nullptr
    };

    VkResult result = vkBeginCommandBuffer(cmd, &begin_info);
    if constexpr (VTF_VALIDATION_ENABLED) {
        frame.vkDebugFns.vkCmdBeginDebugUtilsLabel(cmd, &debug_label);
    }
    VkAssert(result);
    frame.renderPasses.at("DrawPass")->UpdateBeginInfo(frame.drawFramebuffer->vkHandle());
    vkCmdBeginRenderPass(cmd, &frame.renderPasses.at("DrawPass")->BeginInfo(), VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, frame.graphicsPipelines.at("OpaqueDrawPipeline")->vkHandle());
            auto& binder0 = frame.GetBinder("DrawPass");
            binder0.Bind(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS);
			for (size_t i = 0u; i < frame.renderFns.size(); ++i)
			{
				frame.renderFns[i](cmd, &binder0, vtf_frame_data_t::render_type::Opaque);
			}
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, frame.graphicsPipelines.at("TransparentDrawPipeline")->vkHandle());
			for (size_t i = 0u; i < frame.renderFns.size(); ++i)
			{
				frame.renderFns[i](cmd, &binder0, vtf_frame_data_t::render_type::Transparent);
			}
    vkCmdEndRenderPass(cmd);
    if constexpr (VTF_VALIDATION_ENABLED) {
        frame.vkDebugFns.vkCmdEndDebugUtilsLabel(cmd);
    }
    result = vkEndCommandBuffer(cmd);

    // debug passes would also go here

}

static std::array<size_t, 8> acquisitionFrequencies{ 0u, 0u, 0u, 0u };

#ifdef NDEBUG
constexpr static bool DEBUG_MODE{ false };
#else
constexpr static bool DEBUG_MODE{ true };
#endif

void RenderVtf(vtf_frame_data_t& frame) {
    /*
        Trying primitive acquisition setup till I find something better:
        1. try with zero timeout (non blocking)
        2. non blocking try 2
        3. block for 0.25ms
        4. block until acquired, last resort

        Want to find out how frequently we hit these: should take a histogram
    */
    bool acquired{ false };
    acquired = tryImageAcquire(frame);
    if constexpr (DEBUG_MODE) {
        if (acquired) {
            acquisitionFrequencies[0]++;
        }
    }

    getClusterSamples(frame);
    findUniqueClusters(frame);

    if (!acquired) {
        acquired = tryImageAcquire(frame);
        if constexpr (DEBUG_MODE) {
            if (acquired) {
                acquisitionFrequencies[1]++;
            }
        }
    }

    updateClusterIndirectArgs(frame);
    assignLightsToClusters(frame);

    if (!acquired) {
        // up to using slight timeout (wait 0.25ms)
        acquired = tryImageAcquire(frame, uint64_t(2.5e5));
        if constexpr (DEBUG_MODE) {
            if (acquired) {
                acquisitionFrequencies[2]++;
            }
        }
    }

    // submit all work that doesn't rely on the swapchain image
    submitPreSwapchainWritingWork(frame);
    
    if (!acquired) {
        acquired = tryImageAcquire(frame, UINT64_MAX);
        if constexpr (DEBUG_MODE) {
            std::cerr << "Had to acquire swapchain iamge by blocking!!";
            acquisitionFrequencies[3]++;
        }
    }

    createDrawFrameBuffer(frame);
    vtfMainRenderPass(frame);
}

void SubmitGraphicsWork(vtf_frame_data_t& frame) {

    const VkSemaphore wait_semaphores[2]{
        frame.semaphores.at("PreBackbufferWorkComplete")->vkHandle(),
        frame.semaphores.at("ImageAcquire")->vkHandle()
    };

    auto* device = RenderingContext::Get().Device();

    // Need to wait for these as we use the depth pre-pass' output iirc?
    constexpr static VkPipelineStageFlags wait_mask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;

    const VkSubmitInfo submit_info{
        VK_STRUCTURE_TYPE_SUBMIT_INFO,
        nullptr,
        2u,
        wait_semaphores,
        &wait_mask,
        1u,
        &frame.graphicsPool->GetCmdBuffer(0u),
        1u,
        &frame.semaphores.at("RenderComplete")->vkHandle()
    };

    VkResult result = vkQueueSubmit(device->GraphicsQueue(), 1u, &submit_info, VK_NULL_HANDLE);
    VkAssert(result);

    frame.lastImageIdx = frame.imageIdx;
}

void destroyFrameResources(vtf_frame_data_t & frame) {
    auto& rsrc_context = ResourceContext::Get();

    for (auto& rsrc : frame.rsrcMap) {
        rsrc_context.DestroyResource(rsrc.second);
    }

    for (auto& semaphore : frame.semaphores) {
        semaphore.second.reset();
    }

    frame.computePool.reset();
    frame.graphicsPool.reset();
    frame.clusterSamplesFramebuffer.reset();
    frame.drawFramebuffer.reset();

}

void destroyRenderpasses(vtf_frame_data_t & frame) {

    for (auto& pass : frame.renderPasses) {
        pass.second.reset();
    }
}

void destroyPipelines(vtf_frame_data_t & frame) {

    for (auto& compute : frame.computePipelines) {
        compute.second.Destroy();
    }

    for (auto& graphics : frame.graphicsPipelines) {
        graphics.second.reset();
    }

}

void DestroyFrame(vtf_frame_data_t& frame) {
    destroyFrameResources(frame); // gets semaphores and framebuffers too
    destroyRenderpasses(frame);
    destroyPipelines(frame);
}

void DestroyShaders(vtf_frame_data_t & frame) {

    for (auto& cache : frame.pipelineCaches) {
        cache.second.reset();
    }

    for (auto& shader : frame.shaderModules) {
        shader.second.reset();
    }

}

void MergeSort(vtf_frame_data_t& frame, VkCommandBuffer cmd, VulkanResource* src_keys, VulkanResource* src_values, VulkanResource* dst_keys, VulkanResource* dst_values,
    uint32_t total_values, uint32_t chunk_size, DescriptorBinder dscr_binder) {

	constexpr static uint32_t num_values_per_thread_group = SORT_NUM_THREADS_PER_THREAD_GROUP * SORT_ELEMENTS_PER_THREAD;
	uint32_t num_chunks = static_cast<uint32_t>(glm::ceil(total_values / static_cast<float>(chunk_size)));
	if (num_chunks <= 1u)
	{
		// Not gonna sort, so don't create any resources or do any of the below as it'll be a waste
		return;
	}
	uint32_t pass = 0u;

    constexpr static VkDebugUtilsLabelEXT debug_label{
        VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        nullptr,
        "MergeSort",
        { 66.0f / 255.0f, 244.0f / 255.0f, 72.0f / 255.0f, 1.0f } // green, distinct from rest
    };

    // Create a new mergePathPartitions for this invocation.
    VulkanResource* merge_path_partitions = CreateMergePathPartitions(frame);

	constexpr static VkBufferCreateInfo sort_params_info{
		VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		nullptr,
		0,
		sizeof(uint32_t) * 2u,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_SHARING_MODE_EXCLUSIVE,
		0u,
		nullptr
	};


	SortParams sort_params_local;
	const gpu_resource_data_t sort_params_copy{ &sort_params_local, sizeof(SortParams), 0u, 0u, 0u };
	auto& rsrc_context = ResourceContext::Get();
	VulkanResource* sort_params_rsrc = rsrc_context.CreateBuffer(&sort_params_info, nullptr, 1u, &sort_params_copy, resource_usage::CPU_ONLY, ResourceCreateUserDataAsString, "MergeSortSortParams");
	frame.transientResources.emplace_back(sort_params_rsrc); // will be cleared at end of frame

    const uint32_t compute_idx = RenderingContext::Get().Device()->QueueFamilyIndices().Compute;

    Descriptor* descriptor = frame.descriptorPack->RetrieveDescriptor("MergeSortResources");
    const size_t merge_pp_loc = descriptor->BindingLocation("MergePathPartitions");
    const size_t sort_params_loc = frame.descriptorPack->RetrieveDescriptor("SortResources")->BindingLocation("SortParams");
    const size_t input_keys_loc = descriptor->BindingLocation("InputKeys");
    const size_t output_keys_loc = descriptor->BindingLocation("OutputKeys");
    const size_t input_values_loc = descriptor->BindingLocation("InputValues");
    const size_t output_values_loc = descriptor->BindingLocation("OutputValues");

    dscr_binder.BindResourceToIdx("MergeSortResources", merge_pp_loc, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, merge_path_partitions);
    dscr_binder.BindResourceToIdx("SortResources", sort_params_loc, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, sort_params_rsrc);
    dscr_binder.Update();

    std::array<VkBufferMemoryBarrier, 5> merge_sort_barriers {
        // Input keys
        VkBufferMemoryBarrier{
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            nullptr,
            VK_ACCESS_SHADER_READ_BIT,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            VK_NULL_HANDLE,
            0u,
            VK_WHOLE_SIZE
        },
        // Input values
        VkBufferMemoryBarrier{
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            nullptr,
            VK_ACCESS_SHADER_READ_BIT,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            VK_NULL_HANDLE,
            0u,
            VK_WHOLE_SIZE
        },
        // Output keys
        VkBufferMemoryBarrier{
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            nullptr,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            VK_NULL_HANDLE,
            0u,
            VK_WHOLE_SIZE
        },
        // Output values
        VkBufferMemoryBarrier{
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            nullptr,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            VK_NULL_HANDLE,
            0u,
            VK_WHOLE_SIZE
        },
        // Merge path partitions
        VkBufferMemoryBarrier{
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            nullptr,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkBuffer)merge_path_partitions->Handle,
            0u,
            reinterpret_cast<const VkBufferCreateInfo*>(merge_path_partitions->Info)->size
        }
    };

	const VkBufferMemoryBarrier sort_params_host_barrier{
		VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
		nullptr,
		VK_ACCESS_HOST_WRITE_BIT,
		VK_ACCESS_SHADER_READ_BIT,
		VK_QUEUE_FAMILY_IGNORED,
		VK_QUEUE_FAMILY_IGNORED,
		(VkBuffer)sort_params_rsrc->Handle,
		0u,
		reinterpret_cast<const VkBufferCreateInfo*>(sort_params_rsrc->Info)->size
	};

    if constexpr (VTF_VALIDATION_ENABLED) {
        frame.vkDebugFns.vkCmdBeginDebugUtilsLabel(cmd, &debug_label);
    }

    while (num_chunks > 1) {

		const std::string inserted_label_string = std::string{ "MergeSortPass" } + std::to_string(pass);
		const VkDebugUtilsLabelEXT debug_label_recursion {
			VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
			nullptr,
			inserted_label_string.c_str(),
			{ 66.0f / 255.0f, 244.0f / 255.0f, 72.0f / 255.0f, 1.0f } // green, distinct from rest
		}; 

		if constexpr (VTF_VALIDATION_ENABLED) {
			frame.vkDebugFns.vkCmdInsertDebugUtilsLabel(cmd, &debug_label_recursion);
		}

        ++pass;

        sort_params_local.NumElements = total_values;
        sort_params_local.ChunkSize = chunk_size;
		rsrc_context.SetBufferData(sort_params_rsrc, 1u, &sort_params_copy);
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u, 0u, nullptr, 1u, &sort_params_host_barrier, 0u, nullptr);

        uint32_t num_sort_groups = num_chunks / 2u;
        uint32_t num_thread_groups_per_sort_group = static_cast<uint32_t>(glm::ceil((chunk_size * 2) / static_cast<float>(num_values_per_thread_group)));

        dscr_binder.BindResourceToIdx("MergeSortResources", input_keys_loc, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, src_keys);
        dscr_binder.BindResourceToIdx("MergeSortResources", output_keys_loc, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, dst_keys);
        dscr_binder.BindResourceToIdx("MergeSortResources", input_values_loc, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, src_values);
        dscr_binder.BindResourceToIdx("MergeSortResources", output_values_loc, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, dst_values);
        dscr_binder.Update(); // makes sure bindings are actually proper before binding

        {
			constexpr static VkDebugUtilsLabelEXT debug_label_mpp {
				VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
				nullptr,
				"MergePathPartitions",
				{ 66.0f / 255.0f, 244.0f / 255.0f, 72.0f / 255.0f, 1.0f } // green, distinct from rest
			};

			if constexpr (VTF_VALIDATION_ENABLED) {
				frame.vkDebugFns.vkCmdInsertDebugUtilsLabel(cmd, &debug_label_mpp);
			}

            // Clear buffer
            vkCmdFillBuffer(cmd, (VkBuffer)merge_path_partitions->Handle, 0, reinterpret_cast<const VkBufferCreateInfo*>(merge_path_partitions->Info)->size, 0u);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, frame.computePipelines["MergePathPartitionsPipeline"].Handle);
            dscr_binder.Bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
            uint32_t num_merge_path_partitions_per_sort_group = num_thread_groups_per_sort_group + 1u;
            uint32_t total_merge_path_partitions = num_merge_path_partitions_per_sort_group * num_sort_groups;
            uint32_t num_thread_groups = static_cast<uint32_t>(glm::ceil(float(total_merge_path_partitions) / float(SORT_NUM_THREADS_PER_THREAD_GROUP)));
            vkCmdDispatch(cmd, num_thread_groups, 1, 1);
            merge_sort_barriers[4].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            merge_sort_barriers[4].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &merge_sort_barriers[4], 0, nullptr);
        }

        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, frame.computePipelines["MergeSortPipeline"].Handle);
            uint32_t num_values_per_sort_group = glm::min(chunk_size * 2u, total_values);
            num_thread_groups_per_sort_group = static_cast<uint32_t>(glm::ceil(float(num_values_per_sort_group) / float(num_values_per_thread_group)));
            vkCmdDispatch(cmd, num_thread_groups_per_sort_group * num_sort_groups, 1, 1);
            merge_sort_barriers[0].buffer = (VkBuffer)src_keys->Handle;
            merge_sort_barriers[1].buffer = (VkBuffer)dst_keys->Handle;
            merge_sort_barriers[2].buffer = (VkBuffer)src_values->Handle;
            merge_sort_barriers[3].buffer = (VkBuffer)dst_values->Handle;
            // swap path partitions barrier src/dst access mask: it'll be written next time
            merge_sort_barriers[4].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            merge_sort_barriers[4].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0u, nullptr,
                static_cast<uint32_t>(merge_sort_barriers.size()), merge_sort_barriers.data(), 0u, nullptr);

        }

        // ping-pong the buffers: this means next iteration, bindings are swapped (and so on, including final copy)
        std::swap(src_keys, dst_keys);
        std::swap(src_values, dst_values);

        chunk_size *= 2u;
        num_chunks = static_cast<uint32_t>(glm::ceil(float(total_values) / float(chunk_size)));
    }

    if (pass % 2u == 1u) {
        // if the pass count is odd then we have to copy the results into 
        // where they should actually be

        const VkBufferCopy copy{ 0, 0, VK_WHOLE_SIZE };
        vkCmdCopyBuffer(cmd, (VkBuffer)src_keys->Handle, (VkBuffer)dst_keys->Handle, 1, &copy);
        vkCmdCopyBuffer(cmd, (VkBuffer)src_values->Handle, (VkBuffer)dst_values->Handle, 1, &copy);
    }

    if constexpr (VTF_VALIDATION_ENABLED) {
        frame.vkDebugFns.vkCmdEndDebugUtilsLabel(cmd);
    }

	frame.transientResources.emplace_back(merge_path_partitions);

}

VulkanResource* createDepthStencilResource(const VkSampleCountFlagBits samples) {
    const auto* device = RenderingContext::Get().Device();
    const auto* swapchain = RenderingContext::Get().Swapchain();

    const VkFormat depth_format = device->FindDepthFormat();
    const uint32_t img_width = swapchain->Extent().width;
    const uint32_t img_height = swapchain->Extent().height;

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
        device->GetFormatTiling(depth_format, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT),
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
    result = rsrc.CreateImage(&image_info, &view_info, 0, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "DepthStencilImage");
    return result;
}
