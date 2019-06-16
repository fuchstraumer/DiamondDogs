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
#include <thsvs_simpler_vulkan_synchronization.h>
#include "easylogging++.h"

constexpr static uint32_t SORT_NUM_THREADS_PER_THREAD_GROUP = 256u;
constexpr static uint32_t SORT_ELEMENTS_PER_THREAD = 8u;
constexpr static uint32_t BVH_NUM_THREADS = 32u * 16u;
constexpr static uint32_t AVERAGE_OVERLAPPING_LIGHTS_PER_CLUSTER = 20u;
constexpr static uint32_t LIGHT_GRID_BLOCK_SIZE = 32u;
constexpr static uint32_t CLUSTER_GRID_BLOCK_SIZE = 64u;
constexpr static uint32_t AVERAGE_LIGHTS_PER_TILE = 16u;
constexpr static uint32_t MAX_POINT_LIGHTS = 8192u;

// The number of nodes at each level of the BVH.
constexpr static std::array<uint32_t, 6> NumLevelNodes {
    1,          // 1st level (32^0)
    32,         // 2nd level (32^1)
    1024,       // 3rd level (32^2)
    32768,      // 4th level (32^3)
    1048576,    // 5th level (32^4)
    33554432,   // 6th level (32^5)
};

// The number of nodes required to represent a BVH given the number of levels
// of the BVH.
constexpr static std::array<uint32_t, 6> NumBVHNodes {
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

constexpr static VkAccessFlags ALL_ACCESS_FLAGS_MASK =
    VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_INPUT_ATTACHMENT_READ_BIT |
    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT;

constexpr static VkMemoryBarrier GLOBAL_DEBUG_BARRIER{
    VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    nullptr,
    ALL_ACCESS_FLAGS_MASK,
    ALL_ACCESS_FLAGS_MASK
};

void InsertGlobalBarrier(VkCommandBuffer cmd) noexcept
{
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &GLOBAL_DEBUG_BARRIER, 0u, nullptr, 0u, nullptr);
}

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
    if (num_levels > 0 && num_levels < NumBVHNodes.size()) {
        num_nodes = NumBVHNodes[num_levels - 1];
    }

    return num_nodes;
}

static std::unordered_map<std::string, VkPipelineCreationFeedbackEXT> pipelineFeedbacks;
// using an array since max is always just six, but we can downsize by just setting the index properly
static std::unordered_map<std::string, std::array<VkPipelineCreationFeedbackEXT, 6>> pipelineStageFeedbacks;

std::string ShaderStageFlagBitsToStr(const VkShaderStageFlagBits bits)
{
    if (bits & VK_SHADER_STAGE_VERTEX_BIT)
    {
        return "Vertex";
    }
    else if (bits & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT)
    {
        return "Tesselation Control";
    }
    else if (bits & VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT)
    {
        return "Tesselation Evaluation";
    }
    else if (bits & VK_SHADER_STAGE_GEOMETRY_BIT)
    {
        return "Geometry";
    }
    else if (bits & VK_SHADER_STAGE_FRAGMENT_BIT)
    {
        return "Fragment";
    }
    else
    {
        return "INVALID_STAGE";
    }
}

void GraphicsPipelineCreationShim(vtf_frame_data_t& frame, const std::string& name, VkGraphicsPipelineCreateInfo& pipeline_info, const std::string& group_name)
{
    auto* device = RenderingContext::Get().Device();

    VkPipelineCreationFeedbackCreateInfoEXT feedback_create_info;

    if (device->HasExtension(VK_EXT_PIPELINE_CREATION_FEEDBACK_EXTENSION_NAME))
    {
        auto& pipeline_feedback = pipelineFeedbacks[name];
        pipeline_feedback.flags = 0;
        pipeline_feedback.duration = 0;
        auto& stage_feedbacks = pipelineStageFeedbacks[name];

        feedback_create_info = VkPipelineCreationFeedbackCreateInfoEXT{
            VK_STRUCTURE_TYPE_PIPELINE_CREATION_FEEDBACK_CREATE_INFO_EXT,
            nullptr,
            &pipeline_feedback,
            pipeline_info.stageCount,
            stage_feedbacks.data()
        };

        pipeline_info.pNext = reinterpret_cast<const void*>(&feedback_create_info);

    }

    frame.graphicsPipelines[name] = std::make_unique<vpr::GraphicsPipeline>(device->vkHandle());
    frame.graphicsPipelines.at(name)->Init(pipeline_info, vtf_frame_data_t::pipelineCaches.at(group_name)->vkHandle());

    if constexpr (VTF_VALIDATION_ENABLED && VTF_USE_DEBUG_INFO)
    {
        RenderingContext::SetObjectName(VK_OBJECT_TYPE_PIPELINE, (uint64_t)frame.graphicsPipelines.at(name)->vkHandle(), name.c_str());
    }

    // now check pipeline creation feedback
    if (device->HasExtension(VK_EXT_PIPELINE_CREATION_FEEDBACK_EXTENSION_NAME))
    {
        auto pipeline_feedback = pipelineFeedbacks.at(name);
        bool hitApplicationCache{ false };

        if (!(pipeline_feedback.flags & VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT_EXT))
        {
            LOG(ERROR) << "Pipeline creation feedback information is not valid!";
        }

        if (pipeline_feedback.flags & VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT_EXT)
        {
            LOG(INFO) << "Graphics pipeline " << name << " was able to use a pipeline cache to actively increase it's construction speed.";
            hitApplicationCache = true;
        }

        if (pipeline_feedback.flags & VK_PIPELINE_CREATION_FEEDBACK_BASE_PIPELINE_ACCELERATION_BIT_EXT)
        {
            LOG(INFO) << "Graphics pipeline " << name << " was able to use a base pipeline handle or index to increase it's construction speed.";
        }

        // Checking these is mostly redundant when application cache has been hit.
        if (!hitApplicationCache)
        {
            auto& stage_feedbacks = pipelineStageFeedbacks.at(name);
            for (uint32_t i = 0; i < pipeline_info.stageCount; ++i)
            {
                if (stage_feedbacks[i].flags & VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT_EXT)
                {
                    const std::string currStageStr = ShaderStageFlagBitsToStr(pipeline_info.pStages[i].stage);
                    if (stage_feedbacks[i].flags & VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT_EXT)
                    {
                        LOG(INFO) << "Graphics pipeline " << name << " shader stage " << currStageStr << " was able to use the pipeline cache to construct itself faster.";
                    }

                    if (stage_feedbacks[i].flags & VK_PIPELINE_CREATION_FEEDBACK_BASE_PIPELINE_ACCELERATION_BIT_EXT)
                    {
                        LOG(INFO) << "Graphics pipeline " << name << " shader stage " << currStageStr << " was able to use a derived pipeline to construct itself faster.";
                    }
                }
            }
        }
    }

}

void ComputePipelineCreationShim(vtf_frame_data_t& frame, const std::string& name, VkComputePipelineCreateInfo* pipeline_info, const std::string& group_name)
{
	auto* device = RenderingContext::Get().Device();

    VkPipelineCreationFeedbackCreateInfoEXT feedback_create_info;

    if (device->HasExtension(VK_EXT_PIPELINE_CREATION_FEEDBACK_EXTENSION_NAME))
    {
        auto& pipeline_feedback = pipelineFeedbacks[name];
        pipeline_feedback.flags = 0;
        pipeline_feedback.duration = 0;
        auto& stage_feedbacks = pipelineStageFeedbacks[name];

        feedback_create_info = VkPipelineCreationFeedbackCreateInfoEXT{
            VK_STRUCTURE_TYPE_PIPELINE_CREATION_FEEDBACK_CREATE_INFO_EXT,
            nullptr,
            &pipeline_feedback,
            1u,
            stage_feedbacks.data()
        };

        pipeline_info->pNext = reinterpret_cast<const void*>(&feedback_create_info);

    }

	frame.computePipelines[name] = ComputePipelineState(device->vkHandle());
	VkResult result = vkCreateComputePipelines(device->vkHandle(), vtf_frame_data_t::pipelineCaches.at(group_name)->vkHandle(), 1, pipeline_info, nullptr, &frame.computePipelines.at(name).Handle);
	VkAssert(result);
	if constexpr (VTF_USE_DEBUG_INFO && VTF_VALIDATION_ENABLED)
	{
		result = RenderingContext::SetObjectName(VK_OBJECT_TYPE_PIPELINE, (uint64_t)frame.computePipelines.at(name).Handle, VTF_DEBUG_OBJECT_NAME(name.c_str()));
		VkAssert(result);
	}

    // now check pipeline creation feedback
    if (device->HasExtension(VK_EXT_PIPELINE_CREATION_FEEDBACK_EXTENSION_NAME))
    {
        auto pipeline_feedback = pipelineFeedbacks.at(name);
        auto& stage_feedback = pipelineStageFeedbacks.at(name);

        const bool pipelineFeedbackValid =
            (pipeline_feedback.flags & VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT_EXT) &&
            (pipeline_feedback.flags & VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT_EXT);

        if (!pipelineFeedbackValid)
        {
            LOG(ERROR) << "Pipeline creation feedback information is not valid!";
        }

        // Combine our checks since we only have the one stage
        const bool hitApplicationCache =
            (pipeline_feedback.flags & VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT_EXT) ||
            (stage_feedback[0].flags & VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT_EXT);

        if (hitApplicationCache)
        {
            LOG(INFO) << "Compute pipeline " << name << " was able to use a pipeline cache to actively increase it's construction speed.";
        }

        const bool hitDerivedCache =
            (pipeline_feedback.flags & VK_PIPELINE_CREATION_FEEDBACK_BASE_PIPELINE_ACCELERATION_BIT_EXT) ||
            (stage_feedback[0].flags & VK_PIPELINE_CREATION_FEEDBACK_BASE_PIPELINE_ACCELERATION_BIT_EXT);

        if (hitDerivedCache)
        {
            LOG(INFO) << "Compute pipeline " << name << " was able to use a base pipeline handle or index to increase it's construction speed.";
        }

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

    vtf_frame_data_t::pipelineCaches.emplace("MergedCache", std::make_unique<vpr::PipelineCache>(device->vkHandle(), physicalDevice->vkHandle(), typeid(vtf_frame_data_t).hash_code()));
    const VkPipelineCache mergedCacheHandle = vtf_frame_data_t::pipelineCaches.at("MergedCache")->vkHandle();

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


        auto iter = vtf_frame_data_t::pipelineCaches.emplace(name, std::make_unique<vpr::PipelineCache>(device->vkHandle(), physicalDevice->vkHandle(), mergedCacheHandle, std::hash<std::string>()(name)));
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

	const auto* device = RenderingContext::Get().Device();
	const uint32_t graphics_idx = device->QueueFamilyIndices().Graphics;
	const uint32_t compute_idx = device->QueueFamilyIndices().Compute;
	const VkSharingMode sharing_mode = graphics_idx != compute_idx ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE;
	const uint32_t queue_family_indices[2]{ graphics_idx, compute_idx };

    const VkBufferCreateInfo matrices_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        sizeof(Matrices_t),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		sharing_mode,
		sharing_mode == VK_SHARING_MODE_CONCURRENT ? 2u : 0u,
		sharing_mode == VK_SHARING_MODE_CONCURRENT ? queue_family_indices : nullptr
    };

    const VkBufferCreateInfo globals_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        sizeof(GlobalsData),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		sharing_mode,
		sharing_mode == VK_SHARING_MODE_CONCURRENT ? 2u : 0u,
		sharing_mode == VK_SHARING_MODE_CONCURRENT ? queue_family_indices : nullptr
    };

    auto& rsrc_context = ResourceContext::Get();

    frame.rsrcMap["matrices"] = rsrc_context.CreateBuffer(&matrices_info, nullptr, 0u, nullptr, resource_usage::CPU_ONLY, DEF_RESOURCE_FLAGS, "matrices");
    frame.rsrcMap["debugLightsMatrices"] = rsrc_context.CreateBuffer(&matrices_info, nullptr, 0u, nullptr, resource_usage::CPU_ONLY, DEF_RESOURCE_FLAGS, "debugLightsMatrices");
    frame.rsrcMap["globals"] = rsrc_context.CreateBuffer(&globals_info, nullptr, 0u, nullptr, resource_usage::CPU_ONLY, DEF_RESOURCE_FLAGS, "globals");

    auto* descr = frame.descriptorPack->RetrieveDescriptor("GlobalResources");
    descr->BindResourceToIdx(descr->BindingLocation("matrices"), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frame.rsrcMap.at("matrices"));
    descr->BindResourceToIdx(descr->BindingLocation("globals"), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frame.rsrcMap.at("globals"));

}

void createVolumetricForwardResources(vtf_frame_data_t& frame) {

    auto& camera = PerspectiveCamera::Get();
    auto* vf_descr = frame.descriptorPack->RetrieveDescriptor("VolumetricForward");

	const auto* device = RenderingContext::Get().Device();
    std::vector<VulkanResource*> resourcesToZeroInit;
	const uint32_t graphics_idx = device->QueueFamilyIndices().Graphics;
	const uint32_t compute_idx = device->QueueFamilyIndices().Compute;
    const uint32_t transfer_idx = device->QueueFamilyIndices().Transfer;
	const VkSharingMode sharing_mode = graphics_idx != compute_idx ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE;
    const std::array<uint32_t, 3> queue_family_indices{ graphics_idx, compute_idx, transfer_idx };

    float fov_y = camera.FOV() * 0.5f;
    float z_near = camera.NearPlane();
    float z_far = camera.FarPlane();
    frame.Globals.depthRange = glm::vec2(z_near, z_far);

    auto& ctxt = RenderingContext::Get();
    const uint32_t window_width = ctxt.Swapchain()->Extent().width;
    const uint32_t window_height = ctxt.Swapchain()->Extent().height;
    frame.Globals.windowSize = glm::vec2(window_width, window_height);

    uint32_t cluster_dim_x = static_cast<uint32_t>(glm::ceil(window_width / float(CLUSTER_GRID_BLOCK_SIZE)));
    uint32_t cluster_dim_y = static_cast<uint32_t>(glm::ceil(window_height / float(CLUSTER_GRID_BLOCK_SIZE)));

    float sD = (2.0f * glm::tan(fov_y)) / float(cluster_dim_y);
    float log_dim_y = 1.0f / glm::log(1.0f + sD);
    float log_depth = glm::log(z_far / z_near);

    uint32_t cluster_dim_z = static_cast<uint32_t>(glm::floor(log_depth * log_dim_y));
    const uint32_t CLUSTER_SIZE = cluster_dim_x * cluster_dim_y * cluster_dim_z;
    const uint32_t LIGHT_INDEX_LIST_SIZE = CLUSTER_SIZE * AVERAGE_LIGHTS_PER_TILE;

    frame.ClusterData.GridDim = glm::uvec3{ cluster_dim_x, cluster_dim_y, cluster_dim_z };
    frame.ClusterData.ViewNear = z_near;
    frame.ClusterData.ScreenSize = glm::uvec2{ CLUSTER_GRID_BLOCK_SIZE, CLUSTER_GRID_BLOCK_SIZE };
    frame.ClusterData.NearK = 1.0f + sD;
    frame.ClusterData.LogGridDimY = log_dim_y;

    auto& rsrc_context = ResourceContext::Get();
    const VkBufferCreateInfo cluster_data_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        sizeof(ClusterData_t),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		sharing_mode,
		sharing_mode == VK_SHARING_MODE_CONCURRENT ? static_cast<uint32_t>(queue_family_indices.size()) : 0u,
		sharing_mode == VK_SHARING_MODE_CONCURRENT ? queue_family_indices.data() : nullptr
    };

    const gpu_resource_data_t cluster_data_update{
        &frame.ClusterData,
        sizeof(frame.ClusterData),
        0u, VK_QUEUE_FAMILY_IGNORED
    };

    auto& cluster_data = frame.rsrcMap["ClusterData"];
    if (!cluster_data) {
        cluster_data = rsrc_context.CreateBuffer(&cluster_data_info, nullptr, 1, &cluster_data_update, resource_usage::CPU_ONLY, DEF_RESOURCE_FLAGS, "ClusterData");
    }
    else {
        rsrc_context.SetBufferData(cluster_data, 1, &cluster_data_update);
    }

    vf_descr->BindResourceToIdx(vf_descr->BindingLocation("ClusterData"), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frame.rsrcMap.at("ClusterData"));

    const VkBufferCreateInfo cluster_flags_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        sizeof(uint32_t) * CLUSTER_SIZE,
        VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		sharing_mode,
        sharing_mode == VK_SHARING_MODE_CONCURRENT ? static_cast<uint32_t>(queue_family_indices.size()) : 0u,
        sharing_mode == VK_SHARING_MODE_CONCURRENT ? queue_family_indices.data() : nullptr
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

    resourcesToZeroInit.emplace_back(cluster_flags);
    resourcesToZeroInit.emplace_back(unique_clusters);
    resourcesToZeroInit.emplace_back(prev_unique_clusters);

    const VkBufferCreateInfo indir_args_buffer{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        sizeof(VkDispatchIndirectCommand),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
		sharing_mode,
        sharing_mode == VK_SHARING_MODE_CONCURRENT ? static_cast<uint32_t>(queue_family_indices.size()) : 0u,
        sharing_mode == VK_SHARING_MODE_CONCURRENT ? queue_family_indices.data() : nullptr
    };

    auto& assign_lights_args_buffer = frame.rsrcMap["AssignLightsToClustersArgumentBuffer"];
    if (assign_lights_args_buffer) {
        rsrc_context.DestroyResource(assign_lights_args_buffer);
    }
    assign_lights_args_buffer = rsrc_context.CreateBuffer(&indir_args_buffer, nullptr, 0, nullptr, resource_usage::CPU_ONLY, DEF_RESOURCE_FLAGS, "AssignLightsToClustersArgumentBuffer");

    const VkBufferCreateInfo debug_indirect_draw_buffer_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        sizeof(VkDrawIndexedIndirectCommand),
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
		sharing_mode,
        sharing_mode == VK_SHARING_MODE_CONCURRENT ? static_cast<uint32_t>(queue_family_indices.size()) : 0u,
        sharing_mode == VK_SHARING_MODE_CONCURRENT ? queue_family_indices.data() : nullptr
    };

    auto& debug_clusters_indir_draw_buffer = frame.rsrcMap["DebugClustersDrawIndirectArgumentBuffer"];
    if (debug_clusters_indir_draw_buffer) {
        rsrc_context.DestroyResource(debug_clusters_indir_draw_buffer);
    }
    debug_clusters_indir_draw_buffer = rsrc_context.CreateBuffer(&debug_indirect_draw_buffer_info, nullptr, 0, nullptr, resource_usage::CPU_ONLY, DEF_RESOURCE_FLAGS, "DebugClustersDrawIndirectArgumentBuffer");

    const VkBufferCreateInfo cluster_aabbs_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        CLUSTER_SIZE * sizeof(AABB),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		sharing_mode,
        sharing_mode == VK_SHARING_MODE_CONCURRENT ? static_cast<uint32_t>(queue_family_indices.size()) : 0u,
        sharing_mode == VK_SHARING_MODE_CONCURRENT ? queue_family_indices.data() : nullptr
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
		sharing_mode,
        sharing_mode == VK_SHARING_MODE_CONCURRENT ? static_cast<uint32_t>(queue_family_indices.size()) : 0u,
        sharing_mode == VK_SHARING_MODE_CONCURRENT ? queue_family_indices.data() : nullptr
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

    const VkBufferCreateInfo indices_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        LIGHT_INDEX_LIST_SIZE * sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        sharing_mode,
        sharing_mode == VK_SHARING_MODE_CONCURRENT ? static_cast<uint32_t>(queue_family_indices.size()) : 0u,
        sharing_mode == VK_SHARING_MODE_CONCURRENT ? queue_family_indices.data() : nullptr
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

    const VkBufferCreateInfo counter_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        256u,
        VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        sharing_mode,
        sharing_mode == VK_SHARING_MODE_CONCURRENT ? static_cast<uint32_t>(queue_family_indices.size()) : 0u,
        sharing_mode == VK_SHARING_MODE_CONCURRENT ? queue_family_indices.data() : nullptr
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

    const bool havePointLights = !SceneLightsState().PointLights.empty();
    const bool haveSpotLights = !SceneLightsState().SpotLights.empty();

    if (havePointLights) 
    {

        auto& point_light_grid = frame.rsrcMap["PointLightGrid"];
        if (point_light_grid) {
            rsrc_context.DestroyResource(point_light_grid);
        }
        point_light_grid = rsrc_context.CreateBuffer(&point_light_grid_info, &point_light_grid_view_info, 0, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "PointLightGrid");
        vf_descr->BindResourceToIdx(vf_descr->BindingLocation("PointLightGrid"), VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, point_light_grid);
        resourcesToZeroInit.emplace_back(point_light_grid);

        auto& point_light_idx_list = frame.rsrcMap["PointLightIndexList"];
        if (point_light_idx_list) {
            rsrc_context.DestroyResource(point_light_idx_list);
        }
        point_light_idx_list = rsrc_context.CreateBuffer(&indices_info, &indices_view_info, 0, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "PointLightIndexList");
        vf_descr->BindResourceToIdx(vf_descr->BindingLocation("PointLightIndexList"), VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, point_light_idx_list);
        resourcesToZeroInit.emplace_back(point_light_idx_list);

        frame.rsrcMap["PointLightIndexCounter"] = rsrc_context.CreateBuffer(&counter_info, &counter_view_info, 0, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "PointLightIndexCounter");
        vf_descr->BindResourceToIdx(vf_descr->BindingLocation("PointLightIndexCounter"), VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, frame.rsrcMap.at("PointLightIndexCounter"));
        resourcesToZeroInit.emplace_back(frame.rsrcMap.at("PointLightIndexCounter"));

    }

    if (haveSpotLights)
    {

        auto& spot_light_grid = frame.rsrcMap["SpotLightGrid"];
        if (spot_light_grid) {
            rsrc_context.DestroyResource(spot_light_grid);
        }
        spot_light_grid = rsrc_context.CreateBuffer(&point_light_grid_info, &point_light_grid_view_info, 0, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "SpotLightGrid");
        vf_descr->BindResourceToIdx(vf_descr->BindingLocation("SpotLightGrid"), VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, spot_light_grid);
        resourcesToZeroInit.emplace_back(spot_light_grid);

        auto& spot_light_index_list = frame.rsrcMap["SpotLightIndexList"];
        if (spot_light_index_list) {
            rsrc_context.DestroyResource(spot_light_index_list);
        }
        spot_light_index_list = rsrc_context.CreateBuffer(&indices_info, &indices_view_info, 0, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "SpotLightIndexList");
        vf_descr->BindResourceToIdx(vf_descr->BindingLocation("SpotLightIndexList"), VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, spot_light_index_list);
        resourcesToZeroInit.emplace_back(spot_light_index_list);

        frame.rsrcMap["SpotLightIndexCounter"] = rsrc_context.CreateBuffer(&counter_info, &counter_view_info, 0, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "SpotLightIndexCounter");
        vf_descr->BindResourceToIdx(vf_descr->BindingLocation("SpotLightIndexCounter"), VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, frame.rsrcMap.at("SpotLightIndexCounter"));
        resourcesToZeroInit.emplace_back(frame.rsrcMap.at("SpotLightIndexCounter"));

    }

    frame.rsrcMap["UniqueClustersCounter"] = rsrc_context.CreateBuffer(&counter_info, &counter_view_info, 0, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "UniqueClustersCounter");
    vf_descr->BindResourceToIdx(vf_descr->BindingLocation("UniqueClustersCounter"), VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, frame.rsrcMap.at("UniqueClustersCounter"));
    resourcesToZeroInit.emplace_back(frame.rsrcMap.at("UniqueClustersCounter"));

    for (auto& resource : resourcesToZeroInit)
    {
        rsrc_context.FillBuffer(resource, 0u, 0u, VK_WHOLE_SIZE);
    }

}

void uploadLightsToGPU(vtf_frame_data_t& frame)
{

    const uint32_t compute_queue_idx = RenderingContext::Get().Device()->QueueFamilyIndices().Compute;
    const uint32_t graphics_queue_idx = RenderingContext::Get().Device()->QueueFamilyIndices().Graphics;

    const gpu_resource_data_t point_lights_data{
        SceneLightsState().PointLights.data(),
        SceneLightsState().PointLights.size() * sizeof(PointLight),
        0u, compute_queue_idx
    };

    const gpu_resource_data_t spot_lights_data{
        SceneLightsState().SpotLights.data(),
        SceneLightsState().SpotLights.size() * sizeof(SpotLight),
        0u, compute_queue_idx
    };

    const gpu_resource_data_t dir_lights_data{
        SceneLightsState().DirectionalLights.data(),
        SceneLightsState().DirectionalLights.size() * sizeof(DirectionalLight),
        0u, compute_queue_idx
    };

    auto& resource_context = ResourceContext::Get();
    if (!SceneLightsState().PointLights.empty())
    {
        resource_context.SetBufferData(frame.rsrcMap.at("PointLights"), 1u, &point_lights_data);
    }
    if (!SceneLightsState().SpotLights.empty())
    {
        resource_context.SetBufferData(frame.rsrcMap.at("SpotLights"), 1u, &spot_lights_data);
    }
    if (!SceneLightsState().DirectionalLights.empty())
    {
        resource_context.SetBufferData(frame.rsrcMap.at("DirectionalLights"), 1u, &dir_lights_data);
    }

    auto& cluster_colors_vec = SceneLightsState().ClusterColors;
    VkDeviceSize required_mem = cluster_colors_vec.size() * sizeof(glm::u8vec4);
    const gpu_resource_data_t buffer_data{
        cluster_colors_vec.data(), required_mem, 0u, graphics_queue_idx
    };
    resource_context.SetBufferData(frame.rsrcMap.at("DebugClusterColors"), 1u, &buffer_data);

}

void createLightResources(vtf_frame_data_t& frame) {
    auto& rsrc_context = ResourceContext::Get();
    auto* descr = frame.descriptorPack->RetrieveDescriptor("VolumetricForwardLights");

	// set sharing parameters: concurrent costs more but makes our lives a lot easier. might need to go exclusive if perf bombs
	const auto* device = RenderingContext::Get().Device();
    const uint32_t graphics_idx = device->QueueFamilyIndices().Graphics;
    const uint32_t transfer_idx = device->QueueFamilyIndices().Transfer;
    const uint32_t compute_idx = device->QueueFamilyIndices().Compute;
    const std::array<uint32_t, 3> queue_family_indices{ graphics_idx, transfer_idx, compute_idx };
    const VkSharingMode sharing_mode = graphics_idx != compute_idx ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE;

    const bool havePointLights = !SceneLightsState().PointLights.empty();
    const bool haveSpotLights = !SceneLightsState().SpotLights.empty();
    const bool haveDirLights = !SceneLightsState().DirectionalLights.empty();

    VkBufferCreateInfo lights_buffer_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        sizeof(PointLight) * SceneLightsState().PointLights.size(),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		sharing_mode,
        sharing_mode == VK_SHARING_MODE_CONCURRENT ? static_cast<uint32_t>(queue_family_indices.size()) : 0u,
        sharing_mode == VK_SHARING_MODE_CONCURRENT ? queue_family_indices.data() : nullptr
    };

	const uint32_t compute_queue_idx = RenderingContext::Get().Device()->QueueFamilyIndices().Compute;

    if (havePointLights)
    {
        const gpu_resource_data_t point_lights_data{
            SceneLightsState().PointLights.data(),
            SceneLightsState().PointLights.size() * sizeof(PointLight),
            0u, compute_queue_idx
        };

        frame.rsrcMap["PointLights"] = rsrc_context.CreateBuffer(&lights_buffer_info, nullptr, 1, &point_lights_data, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "PointLights");
        descr->BindResourceToIdx(descr->BindingLocation("PointLights"), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, frame.rsrcMap.at("PointLights"));
    }

    if (haveSpotLights)
    {
        const gpu_resource_data_t spot_lights_data{
            SceneLightsState().SpotLights.data(),
            SceneLightsState().SpotLights.size() * sizeof(SpotLight),
            0u, compute_queue_idx
        };

        lights_buffer_info.size = spot_lights_data.DataSize;
        frame.rsrcMap["SpotLights"] = rsrc_context.CreateBuffer(&lights_buffer_info, nullptr, 1, &spot_lights_data, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "SpotLights");
        descr->BindResourceToIdx(descr->BindingLocation("SpotLights"), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, frame.rsrcMap.at("SpotLights"));
    }

    if (haveDirLights)
    {
        const gpu_resource_data_t dir_lights_data{
            SceneLightsState().DirectionalLights.data(),
            SceneLightsState().DirectionalLights.size() * sizeof(DirectionalLight),
            0u, compute_queue_idx
        };
        lights_buffer_info.size = dir_lights_data.DataSize;

        frame.rsrcMap["DirectionalLights"] = rsrc_context.CreateBuffer(&lights_buffer_info, nullptr, 1, &dir_lights_data, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "DirectionalLights");
        descr->BindResourceToIdx(descr->BindingLocation("DirectionalLights"), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, frame.rsrcMap.at("DirectionalLights"));
    }

    const VkBufferCreateInfo light_counts_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        sizeof(LightCountsData),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        sharing_mode,
        sharing_mode == VK_SHARING_MODE_CONCURRENT ? static_cast<uint32_t>(queue_family_indices.size()) : 0u,
        sharing_mode == VK_SHARING_MODE_CONCURRENT ? queue_family_indices.data() : nullptr
    };

    frame.LightCounts.NumPointLights = static_cast<uint32_t>(SceneLightsState().PointLights.size());
    frame.LightCounts.NumSpotLights = static_cast<uint32_t>(SceneLightsState().SpotLights.size());
    frame.LightCounts.NumDirectionalLights = static_cast<uint32_t>(SceneLightsState().DirectionalLights.size());

    const gpu_resource_data_t light_counts_data{
        &frame.LightCounts,
        sizeof(LightCountsData),
        0u, VK_QUEUE_FAMILY_IGNORED
    };

    frame.rsrcMap["LightCounts"] = rsrc_context.CreateBuffer(&light_counts_info, nullptr, 1, &light_counts_data, resource_usage::CPU_ONLY, DEF_RESOURCE_FLAGS, "LightCounts");
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

    frame.rsrcMap["IndirectArgs"] = rsrc_context.CreateBuffer(&indir_args_info, nullptr, 0, nullptr, resource_usage::CPU_ONLY, DEF_RESOURCE_FLAGS, "IndirectArgs");
    descr->BindResourceToIdx(descr->BindingLocation("IndirectArgs"), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, frame.rsrcMap.at("IndirectArgs"));

}

void createSortResources(vtf_frame_data_t& frame) {

    auto& rsrc_context = ResourceContext::Get();
    auto* descr = frame.descriptorPack->RetrieveDescriptor("SortResources");
	const auto* device = RenderingContext::Get().Device();
	const uint32_t graphics_idx = device->QueueFamilyIndices().Graphics;
    const uint32_t transfer_idx = device->QueueFamilyIndices().Transfer;
	const uint32_t compute_idx = device->QueueFamilyIndices().Compute;
	const std::array<uint32_t, 3> queue_family_indices{ graphics_idx, transfer_idx, compute_idx };
    const VkSharingMode sharing_mode = graphics_idx != compute_idx ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE;

    const VkBufferCreateInfo light_aabbs_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        sizeof(AABB) * 512u,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		sharing_mode,
        sharing_mode == VK_SHARING_MODE_CONCURRENT ? static_cast<uint32_t>(queue_family_indices.size()) : 0u,
        sharing_mode == VK_SHARING_MODE_CONCURRENT ? queue_family_indices.data() : nullptr
    };

    frame.rsrcMap["LightAABBs"] = rsrc_context.CreateBuffer(&light_aabbs_info, nullptr, 0u, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "LightAABBs");
    descr->BindResourceToIdx(descr->BindingLocation("LightAABBs"), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, frame.rsrcMap.at("LightAABBs"));

    const bool havePointLights = !SceneLightsState().PointLights.empty();
    const bool haveSpotLights = !SceneLightsState().SpotLights.empty();

    VkBufferCreateInfo sort_buffers_create_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        sizeof(uint32_t) * frame.LightCounts.NumPointLights,
        VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		sharing_mode,
        sharing_mode == VK_SHARING_MODE_CONCURRENT ? static_cast<uint32_t>(queue_family_indices.size()) : 0u,
        sharing_mode == VK_SHARING_MODE_CONCURRENT ? queue_family_indices.data() : nullptr
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

    if (havePointLights)
    {
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
    }

    if (haveSpotLights)
    {
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

}

VulkanResource* CreateMergePathPartitions(vtf_frame_data_t& frame, bool dummy_buffer = false) {
    // merge sort resources are mostly bound before execution, except "MergePathPartitions"

    uint32_t num_chunks = static_cast<uint32_t>(glm::ceil(MAX_POINT_LIGHTS / SORT_NUM_THREADS_PER_THREAD_GROUP));
    uint32_t max_sort_groups = num_chunks / 2u;
    uint32_t path_partitions = static_cast<uint32_t>(glm::ceil((SORT_NUM_THREADS_PER_THREAD_GROUP * 2u) / (SORT_ELEMENTS_PER_THREAD * SORT_NUM_THREADS_PER_THREAD_GROUP) + 1u));
	// create it with some small but nonzero size, or with the proper size. dummy buffer is for a single case where we just need something bound in that spot
    uint32_t merge_path_partitions_buffer_sz = dummy_buffer ? sizeof(uint32_t) * 64u : path_partitions * max_sort_groups * sizeof(uint32_t);

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
	
	const auto* device = RenderingContext::Get().Device();
	const uint32_t graphics_idx = device->QueueFamilyIndices().Graphics;
	const uint32_t compute_idx = device->QueueFamilyIndices().Compute;
	const VkSharingMode sharing_mode = graphics_idx != compute_idx ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE;
	const uint32_t queue_family_indices[2]{ graphics_idx, compute_idx };

    const VkBufferCreateInfo bvh_params_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        sizeof(BVH_Params_t),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        sharing_mode,
        sharing_mode == VK_SHARING_MODE_CONCURRENT ? 2u : 0u,
        sharing_mode == VK_SHARING_MODE_CONCURRENT ? queue_family_indices : nullptr
    };

    frame.rsrcMap["BVHParams"] = rsrc_context.CreateBuffer(&bvh_params_info, nullptr, 0u, nullptr, resource_usage::CPU_ONLY, DEF_RESOURCE_FLAGS, "BVHParams");
    descr->BindResourceToIdx(descr->BindingLocation("BVHParams"), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frame.rsrcMap.at("BVHParams"));

    const bool havePointLights = !SceneLightsState().PointLights.empty();
    const bool haveSpotLights = !SceneLightsState().SpotLights.empty();

    VkBufferCreateInfo bvh_buffer_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        0u,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		sharing_mode,
		sharing_mode == VK_SHARING_MODE_CONCURRENT ? 2u : 0u,
		sharing_mode == VK_SHARING_MODE_CONCURRENT ? queue_family_indices : nullptr
    };

    if (havePointLights)
    {
        const uint32_t point_light_nodes = GetNumNodesBVH(frame.LightCounts.NumPointLights);
        bvh_buffer_info.size = sizeof(AABB) * point_light_nodes;
        frame.rsrcMap["PointLightBVH"] = rsrc_context.CreateBuffer(&bvh_buffer_info, nullptr, 0, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "PointLightBVH");
        descr->BindResourceToIdx(descr->BindingLocation("PointLightBVH"), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, frame.rsrcMap.at("PointLightBVH"));
    }

    if (haveSpotLights)
    {
        const uint32_t spot_light_nodes = GetNumNodesBVH(frame.LightCounts.NumSpotLights);
        bvh_buffer_info.size = sizeof(AABB) * spot_light_nodes;
        frame.rsrcMap["SpotLightBVH"] = rsrc_context.CreateBuffer(&bvh_buffer_info, nullptr, 0, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "SpotLightBVH");
        descr->BindResourceToIdx(descr->BindingLocation("SpotLightBVH"), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, frame.rsrcMap.at("SpotLightBVH"));
    }

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

	if constexpr (VTF_VALIDATION_ENABLED && VTF_USE_DEBUG_INFO)
	{
		static const std::string compute_pool_name{ "ComputePool" };
		RenderingContext::SetObjectName(VK_OBJECT_TYPE_COMMAND_POOL, (uint64_t)frame.computePool->vkHandle(), compute_pool_name.c_str());
		for (size_t i = 0u; i < frame.computePool->size(); ++i)
		{
			std::string compute_buffer_name = compute_pool_name + std::string("_CmdBuffer") + std::to_string(i);
			RenderingContext::SetObjectName(VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t)frame.computePool->GetCmdBuffer(i), compute_buffer_name.c_str());
		}
		static const std::string graphics_pool_name{ "GraphicsPool" };
		RenderingContext::SetObjectName(VK_OBJECT_TYPE_COMMAND_POOL, (uint64_t)frame.graphicsPool->vkHandle(), graphics_pool_name.c_str());
		for (size_t i = 0u; i < frame.graphicsPool->size(); ++i)
		{
			std::string graphics_buffer_name = graphics_pool_name + std::string("_CmdBuffer") + std::to_string(i);
			RenderingContext::SetObjectName(VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t)frame.graphicsPool->GetCmdBuffer(i), graphics_buffer_name.c_str());
		}
	}
}

void createDebugResources(vtf_frame_data_t& frame)
{
    const auto* device = RenderingContext::Get().Device();
    const uint32_t graphics_idx = device->QueueFamilyIndices().Graphics;
    const uint32_t transfer_idx = device->QueueFamilyIndices().Transfer;
    const uint32_t compute_idx = device->QueueFamilyIndices().Compute;
    const std::array<uint32_t, 3> queue_family_indices{ graphics_idx, compute_idx, transfer_idx };
    const VkSharingMode sharing_mode = graphics_idx != compute_idx ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE;
	auto& cluster_colors_vec = SceneLightsState().ClusterColors;
    VkDeviceSize required_mem = cluster_colors_vec.size() * sizeof(glm::u8vec4);

	const VkBufferCreateInfo buffer_info{
		VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		nullptr,
		0,
		required_mem,
		VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        sharing_mode,
        sharing_mode == VK_SHARING_MODE_CONCURRENT ? static_cast<uint32_t>(queue_family_indices.size()) : 0u,
        sharing_mode == VK_SHARING_MODE_CONCURRENT ? queue_family_indices.data() : nullptr
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

	const uint32_t graphics_queue_idx = RenderingContext::Get().Device()->QueueFamilyIndices().Graphics;
	const gpu_resource_data_t buffer_data{
		cluster_colors_vec.data(), required_mem, 0u, graphics_queue_idx
	};
	
	auto& ctxt = ResourceContext::Get();
	frame.rsrcMap["DebugClusterColors"] = ctxt.CreateBuffer(&buffer_info, &buffer_view_info, 1u, &buffer_data, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "DebugClusterColors");

    const VkBufferCreateInfo indirect_args_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        sizeof(VkDrawIndirectCommand),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
        sharing_mode,
        sharing_mode == VK_SHARING_MODE_CONCURRENT ? static_cast<uint32_t>(queue_family_indices.size()) : 0u,
        sharing_mode == VK_SHARING_MODE_CONCURRENT ? queue_family_indices.data() : nullptr
    };

    frame.rsrcMap["DebugClustersIndirectArgs"] = ctxt.CreateBuffer(&indirect_args_info, nullptr, 0u, nullptr, resource_usage::GPU_ONLY, DEF_RESOURCE_FLAGS, "DebugClustersIndirectArgs");

    const VkBufferCreateInfo debug_matrices_info {
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        sizeof(glm::mat4) * 2u,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        sharing_mode,
        sharing_mode == VK_SHARING_MODE_CONCURRENT ? static_cast<uint32_t>(queue_family_indices.size()) : 0u,
        sharing_mode == VK_SHARING_MODE_CONCURRENT ? queue_family_indices.data() : nullptr
    };

    frame.rsrcMap["DebugClustersMatrices"] = ctxt.CreateBuffer(&debug_matrices_info, nullptr, 0u, nullptr, resource_usage::CPU_ONLY, DEF_RESOURCE_FLAGS, "DebugClustersMatrices");

	auto* descr = frame.descriptorPack->RetrieveDescriptor("Debug");
	const size_t colors_loc = descr->BindingLocation("ClusterColors");
    const size_t args_loc = descr->BindingLocation("DebugClustersIndirectArgs");
    const size_t matrices_loc = descr->BindingLocation("DebugClustersMatrices");

	descr->BindResourceToIdx(colors_loc, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, frame.rsrcMap.at("DebugClusterColors"));
    descr->BindResourceToIdx(args_loc, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, frame.rsrcMap.at("DebugClustersIndirectArgs"));
    descr->BindResourceToIdx(matrices_loc, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frame.rsrcMap.at("DebugClustersMatrices"));

}

void createFences(vtf_frame_data_t& frame)
{
	auto* device = RenderingContext::Get().Device();
	frame.graphicsPoolUsageFence = std::make_unique<vpr::Fence>(device->vkHandle(), 0);
	frame.computePoolUsageFence = std::make_unique<vpr::Fence>(device->vkHandle(), 0);
    frame.preRenderGraphicsFence = std::make_unique<vpr::Fence>(device->vkHandle(), 0);
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
    CreateSemaphores(frame);
	setupCommandPools(frame);
	createDebugResources(frame);
	createFences(frame);
	auto* device = RenderingContext::Get().Device();
	auto& limits = RenderingContext::Get().PhysicalDevice(0)->GetProperties().limits;
	assert(limits.timestampComputeAndGraphics);
	uint32_t nanoseconds_period = static_cast<uint32_t>(limits.timestampPeriod);
	frame.queryPool = std::make_unique<QueryPool>(device->vkHandle(), nanoseconds_period);
}

void CreateSemaphores(vtf_frame_data_t & frame) {
    auto* device = RenderingContext::Get().Device();
    frame.semaphores["ImageAcquire"] = std::make_unique<vpr::Semaphore>(device->vkHandle());
    frame.semaphores["ComputeUpdateComplete"] = std::make_unique<vpr::Semaphore>(device->vkHandle());
    frame.semaphores["PreBackbufferWorkComplete"] = std::make_unique<vpr::Semaphore>(device->vkHandle());
    frame.semaphores["RenderComplete"] = std::make_unique<vpr::Semaphore>(device->vkHandle());
}

void createUpdateLightsPipeline(vtf_frame_data_t& frame) {

    const static std::string groupName{ "UpdateLights" };
    const st::ShaderStage& update_lights_stage = vtf_frame_data_t::groupStages.at(groupName).front();

    VkComputePipelineCreateInfo pipeline_info{
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

    const static std::string groupName{ "ReduceLights" };
    const st::ShaderStage& reduce_lights_stage = vtf_frame_data_t::groupStages.at(groupName).front();

    VkPipelineShaderStageCreateInfo pipeline_shader_info = vtf_frame_data_t::shaderModules.at(reduce_lights_stage)->PipelineInfo();

    VkComputePipelineCreateInfo reduce_lights_0_info{
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        nullptr,
        VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT,
        pipeline_shader_info,
        frame.descriptorPack->PipelineLayout(groupName),
        VK_NULL_HANDLE,
        -1
    };

	ComputePipelineCreationShim(frame, "ReduceLightsAABB0", &reduce_lights_0_info, groupName);

    constexpr static VkSpecializationMapEntry reduce_lights_entries[1] {
        VkSpecializationMapEntry{
            0,
            0,
            sizeof(uint32_t)
        }
    };

    constexpr static uint32_t reduce_lights_1_spc_values[1]{ 1u };

    const VkSpecializationInfo reduce_lights_1_specialization_info{
        1u,
        reduce_lights_entries,
        sizeof(uint32_t) * 1u,
        reduce_lights_1_spc_values
    };

    pipeline_shader_info.pSpecializationInfo = &reduce_lights_1_specialization_info;

    VkComputePipelineCreateInfo reduce_lights_1_info{
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

    const static std::string groupName{ "ComputeMortonCodes" };
    const st::ShaderStage& morton_stage = vtf_frame_data_t::groupStages.at(groupName).front();

    VkComputePipelineCreateInfo pipeline_info{
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

    const static std::string groupName{ "RadixSort" };
    const st::ShaderStage& radix_stage = vtf_frame_data_t::groupStages.at(groupName).front();

    VkComputePipelineCreateInfo pipeline_info{
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
    const st::ShaderStage& compute_stage = vtf_frame_data_t::groupStages.at(groupName).front();

    VkComputePipelineCreateInfo pipeline_info{
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

    const static std::string groupName{ "UpdateClusterIndirectArgs" };
    const st::ShaderStage& indir_stage = vtf_frame_data_t::groupStages.at(groupName).front();

    VkComputePipelineCreateInfo pipeline_info{
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

void createAssignLightsToClustersBVHPipeline(vtf_frame_data_t& frame)
{
	const static std::string groupName{ "AssignLightsToClustersBVH" };
	const st::ShaderStage& shader_stage = vtf_frame_data_t::groupStages.at(groupName).front();

	VkComputePipelineCreateInfo pipeline_info{
		VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		nullptr,
		0,
		vtf_frame_data_t::shaderModules.at(shader_stage)->PipelineInfo(),
		frame.descriptorPack->PipelineLayout(groupName),
		VK_NULL_HANDLE,
		-1
	};

	ComputePipelineCreationShim(frame, "AssignLightsToClustersBVHPipeline", &pipeline_info, groupName);

}

void createAssignLightsToClustersPipeline(vtf_frame_data_t& frame)
{
    const static std::string groupName{ "AssignLightsToClusters" };
    const st::ShaderStage& shader_stage = vtf_frame_data_t::groupStages.at(groupName).front();

    VkComputePipelineCreateInfo pipeline_info{
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        nullptr,
        0,
        vtf_frame_data_t::shaderModules.at(shader_stage)->PipelineInfo(),
        frame.descriptorPack->PipelineLayout(groupName),
        VK_NULL_HANDLE,
        -1
    };

    ComputePipelineCreationShim(frame, "AssignLightsToClustersPipeline", &pipeline_info, groupName);
}

void createMergeSortPipelines(vtf_frame_data_t& frame) {

    const static std::string groupName{ "MergeSort" };
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
    VkComputePipelineCreateInfo pipeline_infos[2]{
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
            -1
        }
    };

	const static std::string mppPipelineName{ "MergePathPartitionsPipeline" };
	const static std::string mergePipelineName{ "MergeSortPipeline" };

    ComputePipelineCreationShim(frame, mppPipelineName, &pipeline_infos[0], groupName);

    pipeline_infos[1].basePipelineHandle = frame.computePipelines.at(mppPipelineName).Handle;
    ComputePipelineCreationShim(frame, mergePipelineName, &pipeline_infos[1], groupName);

}

void createFindUniqueClustersPipeline(vtf_frame_data_t& frame)
{

	static const std::string groupName{ "FindUniqueClusters" };

	const st::ShaderStage& shader_stage = vtf_frame_data_t::groupStages.at(groupName).front();

	VkComputePipelineCreateInfo create_info{
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
	createAssignLightsToClustersPipeline(frame);
    createAssignLightsToClustersBVHPipeline(frame);
    createMergeSortPipelines(frame);
	createFindUniqueClustersPipeline(frame);
}

void createDepthAndClusterSamplesPass(vtf_frame_data_t& frame) {

    static const std::string passName{ "DepthAndClusterSamplesPass" };
    std::array<VkSubpassDependency, 2> depthAndClusterDependencies;
    std::array<VkSubpassDescription, 2> depthAndSamplesPassDescriptions;
    const auto* device = RenderingContext::Get().Device();
    const auto* swapchain = RenderingContext::Get().Swapchain();

    const VkAttachmentDescription clusterSampleAttachmentDescriptions[2]{
        VkAttachmentDescription{
            0,
            swapchain->ColorFormat(),
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
        VK_SUBPASS_EXTERNAL,
        0,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT,
        VK_ACCESS_MEMORY_READ_BIT,
        VK_DEPENDENCY_BY_REGION_BIT
    };

    depthAndClusterDependencies[1] = VkSubpassDependency{
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

    frame.renderPasses[passName] = std::make_unique<vpr::Renderpass>(device->vkHandle(), create_info);
    frame.renderPasses.at(passName)->SetupBeginInfo(DepthPrePassAndClusterSamplesClearValues.data(), DepthPrePassAndClusterSamplesClearValues.size(), swapchain->Extent());

    if constexpr (VTF_VALIDATION_ENABLED && VTF_USE_DEBUG_INFO)
    {
        RenderingContext::SetObjectName(VK_OBJECT_TYPE_RENDER_PASS, (uint64_t)frame.renderPasses.at(passName)->vkHandle(), passName.c_str());
    }

    // create image for this pass
    frame.rsrcMap["DepthPrePassImage"] = createDepthStencilResource(VK_SAMPLE_COUNT_1_BIT);

}

void createClusterSamplesResources(vtf_frame_data_t& frame) {

    const auto* device = RenderingContext::Get().Device();
    const auto* swapchain = RenderingContext::Get().Swapchain();
    const VkFormat cluster_samples_color_format = swapchain->ColorFormat();
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

    if constexpr (VTF_VALIDATION_ENABLED && VTF_USE_DEBUG_INFO)
    {
        RenderingContext::SetObjectName(VK_OBJECT_TYPE_FRAMEBUFFER, (uint64_t)frame.clusterSamplesFramebuffer->vkHandle(), "ClusterSamplesFramebuffer");
    }

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
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
            VK_ACCESS_MEMORY_READ_BIT,
            VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_SHADER_READ_BIT,
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

    if constexpr (VTF_VALIDATION_ENABLED && VTF_USE_DEBUG_INFO)
    {
        RenderingContext::SetObjectName(VK_OBJECT_TYPE_RENDER_PASS, (uint64_t)frame.renderPasses.at("DrawPass")->vkHandle(), "DrawPass");
    }

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
        (VkImageView)frame.rsrcMap["DrawMultisampleImage"]->ViewHandle, 
		(VkImageView)frame.rsrcMap["DepthRendertargetImage"]->ViewHandle,
        swapchain->ImageView(size_t(frame.imageIdx))
    };
    
    const VkFramebufferCreateInfo framebuffer_info{
        VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        nullptr,
        0,
        frame.renderPasses.at("DrawPass")->vkHandle(),
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

    static const std::string groupName{ "DepthPrePass" };
	static const std::string pipelineName{ "DepthPrePassPipeline" };

    const st::ShaderStage& depth_vert = vtf_frame_data_t::groupStages.at(groupName).front();

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
    const VkPipelineShaderStageCreateInfo shader_stages[1]{ 
        vtf_frame_data_t::shaderModules.at(depth_vert)->PipelineInfo()
        //vtf_frame_data_t::shaderModules.at(depth_frag)->PipelineInfo() 
    };
    create_info.stageCount = 1u;
    create_info.pStages = shader_stages;
    create_info.subpass = 0;
    create_info.renderPass = frame.renderPasses.at("DepthAndClusterSamplesPass")->vkHandle();
    create_info.layout = frame.descriptorPack->PipelineLayout(groupName);

    GraphicsPipelineCreationShim(frame, pipelineName, create_info, groupName);

}

void createClusterSamplesPipeline(vtf_frame_data_t& frame) {

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

    GraphicsPipelineCreationShim(frame, pipelineName, create_info, groupName);

}

void createDrawPipelines(vtf_frame_data_t& frame) {

    static const std::string groupName{ "DrawPass" };
	static const std::string opaquePipelineName{ "OpaqueDrawPipeline" };
	static const std::string transPipelineName{ "TransparentDrawPipeline" };

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

    GraphicsPipelineCreationShim(frame, opaquePipelineName, opaque_create_info, groupName);
    
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

    GraphicsPipelineCreationShim(frame, transPipelineName, transparent_create_info, groupName);

}

void createDebugClustersPipeline(vtf_frame_data_t& frame)
{
	static const std::string groupName{ "DebugClusters" };
	static const std::string pipelineName = groupName + std::string("Pipeline");

	std::vector<st::ShaderStage> stages;

	for (auto& stage : vtf_frame_data_t::groupStages.at(groupName))
	{
		stages.emplace_back(stage);
	}

	const VkPipelineShaderStageCreateInfo shader_stages[3]{
		vtf_frame_data_t::shaderModules.at(stages[0])->PipelineInfo(),
		vtf_frame_data_t::shaderModules.at(stages[1])->PipelineInfo(),
		vtf_frame_data_t::shaderModules.at(stages[2])->PipelineInfo()
	};

	vpr::GraphicsPipelineInfo pipeline_info;
	pipeline_info.AssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
	pipeline_info.RasterizationInfo.cullMode = VK_CULL_MODE_BACK_BIT;
	pipeline_info.ColorBlendInfo.attachmentCount = 1;
	pipeline_info.ColorBlendInfo.pAttachments = &AdditiveBlendingAttachmentState;
	pipeline_info.ColorBlendInfo.logicOpEnable = VK_FALSE;
	pipeline_info.DynamicStateInfo.dynamicStateCount = 2;
	static constexpr VkDynamicState States[2]{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	pipeline_info.DynamicStateInfo.pDynamicStates = States;
	pipeline_info.DepthStencilInfo.depthWriteEnable = VK_FALSE;
	pipeline_info.DepthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	pipeline_info.MultisampleInfo.rasterizationSamples = SceneConfig.MSAA_SampleCount;

	VkGraphicsPipelineCreateInfo create_info = pipeline_info.GetPipelineCreateInfo();
	create_info.stageCount = 3u;
	create_info.pStages = shader_stages;
	create_info.subpass = 0u;
	create_info.layout = frame.descriptorPack->PipelineLayout(groupName);
	// is rendered normally as part of drawpass, so uses that renderpass
	create_info.renderPass = frame.renderPasses.at("DrawPass")->vkHandle();
	create_info.basePipelineHandle = VK_NULL_HANDLE;
	create_info.basePipelineIndex = -1;

    GraphicsPipelineCreationShim(frame, pipelineName, create_info, groupName);

}

void createDebugLightsPipelines(vtf_frame_data_t& frame)
{
    static const std::string groupName{ "DebugLights" };
    static const std::string pipelineName0{ "DebugPointLights" };
    static const std::string pipelineName1{ "DebugSpotLights" };
    auto* device = RenderingContext::Get().Device();

    std::vector<st::ShaderStage> stages;

    for (auto& stage : vtf_frame_data_t::groupStages.at(groupName))
    {
        stages.emplace_back(stage);
    }

    VkPipelineShaderStageCreateInfo shader_stages[2]{
        vtf_frame_data_t::shaderModules.at(stages[0])->PipelineInfo(),
        vtf_frame_data_t::shaderModules.at(stages[1])->PipelineInfo()
    };

    constexpr static VkSpecializationMapEntry stage_entry{
        0,
        0,
        sizeof(uint32_t)
    };

    constexpr static uint32_t specialization_value{ VK_FALSE };

    const VkSpecializationInfo specialization_info{
        1,
        &stage_entry,
        sizeof(uint32_t),
        &specialization_value
    };

    vpr::GraphicsPipelineInfo pipeline_info;
    pipeline_info.AssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pipeline_info.RasterizationInfo.cullMode = VK_CULL_MODE_BACK_BIT;
    pipeline_info.ColorBlendInfo.attachmentCount = 1;
    pipeline_info.ColorBlendInfo.pAttachments = &AlphaBlendingAttachmentState;
    pipeline_info.ColorBlendInfo.logicOpEnable = VK_FALSE;
    pipeline_info.DynamicStateInfo.dynamicStateCount = 2;
    static constexpr VkDynamicState States[2]{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    pipeline_info.DynamicStateInfo.pDynamicStates = States;
    pipeline_info.DepthStencilInfo.depthWriteEnable = VK_FALSE;
    pipeline_info.DepthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    pipeline_info.MultisampleInfo.rasterizationSamples = SceneConfig.MSAA_SampleCount;

    pipeline_info.VertexInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(VertexBindingDescriptions.size());
    pipeline_info.VertexInfo.pVertexBindingDescriptions = VertexBindingDescriptions.data();
    pipeline_info.VertexInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(VertexAttributes.size());
    pipeline_info.VertexInfo.pVertexAttributeDescriptions = VertexAttributes.data();

    VkGraphicsPipelineCreateInfo create_info = pipeline_info.GetPipelineCreateInfo();
    create_info.stageCount = 2u;
    create_info.pStages = shader_stages;
    create_info.subpass = 0u;
    create_info.layout = frame.descriptorPack->PipelineLayout(groupName);
    // is rendered normally as part of drawpass, so uses that renderpass
    create_info.renderPass = frame.renderPasses.at("DrawPass")->vkHandle();
    create_info.basePipelineHandle = VK_NULL_HANDLE;
    create_info.basePipelineIndex = -1;

    GraphicsPipelineCreationShim(frame, pipelineName0, create_info, groupName);

}

void CreateGraphicsPipelines(vtf_frame_data_t & frame)
{
    createDepthPrePassPipeline(frame);
    createClusterSamplesPipeline(frame);
    createDrawPipelines(frame);
	createDebugClustersPipeline(frame);
    createDebugLightsPipelines(frame);
}

void miscSetup(vtf_frame_data_t& frame)
{

    if (VTF_VALIDATION_ENABLED)
    {
        frame.vkDebugFns = RenderingContext::Get().Device()->DebugUtilsHandler();
    }

}

void FullFrameSetup(vtf_frame_data_t* frame)
{
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
	float fov_y = camera.FOV() * 0.50f;
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

void computeUpdateLights(vtf_frame_data_t& frame, VkCommandBuffer cmd)
{

    constexpr static VkDebugUtilsLabelEXT debug_label{
        VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        nullptr,
        "UpdateLights",
        { 241.0f / 255.0f, 66.0f / 255.0f, 244.0f / 255.0f, 1.0f }
    };

    ScopedRenderSection profiler(*frame.queryPool, cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, "UpdateLights");

    auto& rsrc_context = ResourceContext::Get();
    VulkanResource* light_counts_buffer = frame.rsrcMap.at("LightCounts");
    VulkanResource* point_lights = frame.rsrcMap.at("PointLights");
    VulkanResource* spot_lights = frame.rsrcMap.at("SpotLights");
    VulkanResource* dir_lights = frame.rsrcMap.at("DirectionalLights");
    auto binder = frame.descriptorPack->RetrieveBinder("UpdateLights");
    binder.BindResourceToIdx("GlobalResources", "matrices", VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frame.rsrcMap.at("matrices"));
    binder.Update();

    // update light counts
    frame.LightCounts.NumPointLights = static_cast<uint32_t>(SceneLightsState().PointLights.size());
    frame.LightCounts.NumSpotLights = static_cast<uint32_t>(SceneLightsState().SpotLights.size() <= 1u ? 0u : SceneLightsState().SpotLights.size());
    frame.LightCounts.NumDirectionalLights = static_cast<uint32_t>(SceneLightsState().DirectionalLights.size());

    const gpu_resource_data_t lcb_update{
        &frame.LightCounts,
        sizeof(LightCountsData)
    };

    rsrc_context.SetBufferData(light_counts_buffer, 1, &lcb_update);

    constexpr static std::array<ThsvsAccessType, 1> host_write_type {
        THSVS_ACCESS_TRANSFER_WRITE
    };

    constexpr static std::array<ThsvsAccessType, 3> write_access_type {
        THSVS_ACCESS_COMPUTE_SHADER_WRITE,
        THSVS_ACCESS_COMPUTE_SHADER_READ_OTHER,
        THSVS_ACCESS_COMPUTE_SHADER_READ_UNIFORM_BUFFER
    };

    // Make sure transfers flush fully and are visible, plus once we move back
    // to a rendergraph and explicit queue ownership this will be a template
    constexpr static ThsvsBufferBarrier pre_dispatch_barrier {
        static_cast<uint32_t>(host_write_type.size()),
        host_write_type.data(),
        static_cast<uint32_t>(write_access_type.size()),
        write_access_type.data(),
        VK_QUEUE_FAMILY_IGNORED,
        VK_QUEUE_FAMILY_IGNORED,
        VK_NULL_HANDLE,
        0u,
        0u
    };

    // Prev-Next access types are gonna be the same: compute shaders use these next
    constexpr static ThsvsBufferBarrier post_dispatch_barrier {
        static_cast<uint32_t>(write_access_type.size()),
        write_access_type.data(),
        static_cast<uint32_t>(write_access_type.size()),
        write_access_type.data(),
        VK_QUEUE_FAMILY_IGNORED,
        VK_QUEUE_FAMILY_IGNORED,
        VK_NULL_HANDLE,
        0u,
        0u
    };

    std::array<ThsvsBufferBarrier, 3> pre_dispatch_buffer_barriers;
    pre_dispatch_buffer_barriers.fill(pre_dispatch_barrier);
    std::array<ThsvsBufferBarrier, 3> post_dispatch_buffer_barriers;
    post_dispatch_buffer_barriers.fill(post_dispatch_barrier);

    pre_dispatch_buffer_barriers[0].buffer = (VkBuffer)point_lights->Handle;
    pre_dispatch_buffer_barriers[1].buffer = (VkBuffer)spot_lights->Handle;
    pre_dispatch_buffer_barriers[2].buffer = (VkBuffer)dir_lights->Handle;
    pre_dispatch_buffer_barriers[0].size = reinterpret_cast<const VkBufferCreateInfo*>(point_lights->Info)->size;
    pre_dispatch_buffer_barriers[1].size = reinterpret_cast<const VkBufferCreateInfo*>(spot_lights->Info)->size;
    pre_dispatch_buffer_barriers[2].size = reinterpret_cast<const VkBufferCreateInfo*>(dir_lights->Info)->size;
    post_dispatch_buffer_barriers[0].buffer = pre_dispatch_buffer_barriers[0].buffer;
    post_dispatch_buffer_barriers[1].buffer = pre_dispatch_buffer_barriers[1].buffer;
    post_dispatch_buffer_barriers[2].buffer = pre_dispatch_buffer_barriers[2].buffer;
    post_dispatch_buffer_barriers[0].size = pre_dispatch_buffer_barriers[0].size;
    post_dispatch_buffer_barriers[1].size = pre_dispatch_buffer_barriers[1].size;
    post_dispatch_buffer_barriers[2].size = pre_dispatch_buffer_barriers[2].size;

    // update light positions etc
    if constexpr (VTF_VALIDATION_ENABLED) {
        frame.vkDebugFns.vkCmdBeginDebugUtilsLabel(cmd, &debug_label);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, frame.computePipelines["UpdateLightsPipeline"].Handle);
    thsvsCmdPipelineBarrier(cmd, nullptr, static_cast<uint32_t>(pre_dispatch_buffer_barriers.size()), pre_dispatch_buffer_barriers.data(), 0u, nullptr);
    binder.Bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
    uint32_t num_groups_x = glm::max(frame.LightCounts.NumPointLights, glm::max(frame.LightCounts.NumDirectionalLights, frame.LightCounts.NumSpotLights));
    num_groups_x = static_cast<uint32_t>(glm::ceil(num_groups_x / 1024.0f));
    vkCmdDispatch(cmd, num_groups_x, 1, 1);
	thsvsCmdPipelineBarrier(cmd, nullptr, static_cast<uint32_t>(post_dispatch_buffer_barriers.size()), post_dispatch_buffer_barriers.data(), 0u, nullptr);

	if constexpr (VTF_VALIDATION_ENABLED)
    {
		frame.vkDebugFns.vkCmdEndDebugUtilsLabel(cmd);
	}

}

void reduceLights(vtf_frame_data_t& frame, VkCommandBuffer cmd)
{

    constexpr static VkDebugUtilsLabelEXT debug_label0 {
        VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        nullptr,
        "ReduceLightsStep0",
        { 182.0f / 255.0f, 66.0f / 255.0f, 244.0f / 255.0f, 1.0f }
    };

	constexpr static VkDebugUtilsLabelEXT debug_label1{
		VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
		nullptr,
		"ReduceLightsStep1",
		{ 192.0f / 255.0f, 66.0f / 255.0f, 1.0f, 1.0f }
	};

    auto& rsrc = ResourceContext::Get();
    // required to use push constants
    const VkPipelineLayout required_layout = frame.descriptorPack->PipelineLayout("ReduceLights");

    uint32_t num_thread_groups = glm::min<uint32_t>(static_cast<uint32_t>(glm::ceil(glm::max(frame.LightCounts.NumPointLights, frame.LightCounts.NumSpotLights) / 512.0f)), uint32_t(512));
    frame.DispatchParams.NumThreadGroups = glm::uvec3(num_thread_groups, 1u, 1u);
    frame.DispatchParams.NumThreads = glm::uvec3(num_thread_groups * 512, 1u, 1u);

    uint32_t push_constants_array[2]{
        frame.DispatchParams.NumThreadGroups.x,
        frame.DispatchParams.NumThreadGroups.x
    };
        
    // with the change to push constants, we should only need this once
    auto binder0 = frame.descriptorPack->RetrieveBinder("ReduceLights");
    binder0.Update();

	{
		ScopedRenderSection profiler_0(*frame.queryPool, cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, "ReduceLightsStep0");

		if constexpr (VTF_VALIDATION_ENABLED)
        {
			frame.vkDebugFns.vkCmdBeginDebugUtilsLabel(cmd, &debug_label0);
		}

		binder0.Bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, frame.computePipelines.at("ReduceLightsAABB0").Handle);
        vkCmdPushConstants(cmd, required_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0u, sizeof(uint32_t) * 2, push_constants_array);
		vkCmdDispatch(cmd, num_thread_groups, 1u, 1u);

		if constexpr (VTF_VALIDATION_ENABLED)
        {
			frame.vkDebugFns.vkCmdEndDebugUtilsLabel(cmd);
		}
	}

	// Barrier between first and second stages
	constexpr static ThsvsAccessType barrier_access_types[2]{
		THSVS_ACCESS_COMPUTE_SHADER_WRITE,
		THSVS_ACCESS_COMPUTE_SHADER_READ_OTHER
	};

	constexpr ThsvsGlobalBarrier global_barrier{
		2u,
		barrier_access_types,
		2u,
		barrier_access_types
	};

    // Global barriers are also just execution barriers, so we are making sure
    // that the last dispatch completes fully here is all
	thsvsCmdPipelineBarrier(cmd, &global_barrier, 0u, nullptr, 0u, nullptr);

    // second step of reduction
    frame.DispatchParams.NumThreadGroups = glm::uvec3{ 1u, 1u, 1u };
    frame.DispatchParams.NumThreads = glm::uvec3{ 512u, 1u, 1u };

    push_constants_array[0] = frame.DispatchParams.NumThreadGroups.x;

	{
		ScopedRenderSection profiler_1(*frame.queryPool, cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, "ReduceLightsStep1");

		if constexpr (VTF_VALIDATION_ENABLED)
        {
			frame.vkDebugFns.vkCmdBeginDebugUtilsLabel(cmd, &debug_label1);
		}

        binder0.Bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, frame.computePipelines.at("ReduceLightsAABB1").Handle);
        vkCmdPushConstants(cmd, required_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0u, sizeof(uint32_t) * 2, push_constants_array);
		vkCmdDispatch(cmd, 1u, 1u, 1u);
        // Same thing again: make sure this shader completes before anything else executes in the compute shader stage
		thsvsCmdPipelineBarrier(cmd, &global_barrier, 0u, nullptr, 0u, nullptr);

		if constexpr (VTF_VALIDATION_ENABLED)
        {
			frame.vkDebugFns.vkCmdEndDebugUtilsLabel(cmd);
		}
	}
}

void computeMortonCodes(vtf_frame_data_t& frame, VkCommandBuffer cmd)
{

    constexpr static VkDebugUtilsLabelEXT debug_label{
        VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        nullptr,
        "ComputeMortonCodes",
        { 122.0f / 255.0f, 66.0f / 255.0f, 244.0f / 255.0f, 1.0f }
    };

    constexpr static std::array<ThsvsAccessType, 2> access_types{
        THSVS_ACCESS_COMPUTE_SHADER_READ_OTHER,
        THSVS_ACCESS_COMPUTE_SHADER_WRITE
    };

    auto& rsrc = ResourceContext::Get();
    auto binder = frame.descriptorPack->RetrieveBinder("ComputeMortonCodes");
    auto& pointLightIndices = frame.rsrcMap["PointLightIndices"];
    auto& spotLightIndices = frame.rsrcMap["SpotLightIndices"];
    auto& pointLightMortonCodes = frame.rsrcMap["PointLightMortonCodes"];
    auto& spotLightMortonCodes = frame.rsrcMap["SpotLightMortonCodes"];

    const VkDeviceSize point_light_indices_size = reinterpret_cast<const VkBufferCreateInfo*>(pointLightIndices->Info)->size;
    const VkDeviceSize spot_light_indices_size = reinterpret_cast<const VkBufferCreateInfo*>(spotLightIndices->Info)->size;
    const VkDeviceSize point_light_codes_size = reinterpret_cast<const VkBufferCreateInfo*>(pointLightMortonCodes->Info)->size;
    const VkDeviceSize spot_light_codes_size = reinterpret_cast<const VkBufferCreateInfo*>(spotLightMortonCodes->Info)->size;

	const std::array<ThsvsBufferBarrier, 4> compute_morton_codes_barriers{
		ThsvsBufferBarrier{
			static_cast<uint32_t>(access_types.size()),
			access_types.data(),
            static_cast<uint32_t>(access_types.size()),
            access_types.data(),
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			(VkBuffer)pointLightIndices->Handle,
			0u,
			point_light_indices_size
		},
		ThsvsBufferBarrier{
            static_cast<uint32_t>(access_types.size()),
            access_types.data(),
            static_cast<uint32_t>(access_types.size()),
            access_types.data(),
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			(VkBuffer)spotLightIndices->Handle,
			0u,
			spot_light_indices_size
		},
		ThsvsBufferBarrier{
            static_cast<uint32_t>(access_types.size()),
            access_types.data(),
            static_cast<uint32_t>(access_types.size()),
            access_types.data(),
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			(VkBuffer)pointLightMortonCodes->Handle,
			0u,
			point_light_codes_size
		},
		ThsvsBufferBarrier{
            static_cast<uint32_t>(access_types.size()),
            access_types.data(),
            static_cast<uint32_t>(access_types.size()),
            access_types.data(),
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			(VkBuffer)spotLightMortonCodes->Handle,
			0u,
			spot_light_codes_size
		}
	};

    if constexpr (VTF_VALIDATION_ENABLED)
    {
        frame.vkDebugFns.vkCmdBeginDebugUtilsLabel(cmd, &debug_label);
    }

	binder.Update();
	ScopedRenderSection profiler(*frame.queryPool, cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, "ComputeMortonCodes");
    binder.Bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, frame.computePipelines["ComputeLightMortonCodesPipeline"].Handle);
    uint32_t num_thread_groups = static_cast<uint32_t>(glm::ceil(glm::max(frame.LightCounts.NumPointLights, frame.LightCounts.NumSpotLights) / 1024.0f));
    vkCmdDispatch(cmd, num_thread_groups, 1, 1);
	thsvsCmdPipelineBarrier(cmd, nullptr, static_cast<uint32_t>(compute_morton_codes_barriers.size()), compute_morton_codes_barriers.data(), 0u, nullptr);

    if constexpr (VTF_VALIDATION_ENABLED)
    {
        frame.vkDebugFns.vkCmdEndDebugUtilsLabel(cmd);
    }

}

void sortMortonCodes(vtf_frame_data_t& frame, VkCommandBuffer cmd)
{

    const static VkDebugUtilsLabelEXT debug_label{
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

    auto& rsrc_context = ResourceContext::Get();
    auto binder = frame.descriptorPack->RetrieveBinder("RadixSort");
    const VkPipelineLayout required_layout = frame.descriptorPack->PipelineLayout("RadixSort");
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

    // Getting around this is going to require a better binding model that we simply don't have yet
	VulkanResource* merge_path_partitions_empty = CreateMergePathPartitions(frame, true);
	vkCmdFillBuffer(cmd, (VkBuffer)merge_path_partitions_empty->Handle, 0u, reinterpret_cast<const VkBufferCreateInfo*>(merge_path_partitions_empty->Info)->size, 0u);
	frame.transientResources.emplace_back(merge_path_partitions_empty);

	binder.BindResourceToIdx("MergeSortResources", "MergePathPartitions", VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, merge_path_partitions_empty);

	constexpr static std::array<ThsvsAccessType, 1> readonly_access_types{
		THSVS_ACCESS_COMPUTE_SHADER_READ_OTHER
	};

	constexpr static std::array<ThsvsAccessType, 1> writeonly_access_types{
		THSVS_ACCESS_COMPUTE_SHADER_WRITE
	};

    std::array<ThsvsBufferBarrier, 4> radix_sort_barriers{
        // Input keys
		ThsvsBufferBarrier{
			static_cast<uint32_t>(readonly_access_types.size()),
			readonly_access_types.data(),
			static_cast<uint32_t>(writeonly_access_types.size()),
			writeonly_access_types.data(),
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkBuffer)pointLightMortonCodes->Handle,
            0u,
            point_light_codes_size
        },
        // Input values
		ThsvsBufferBarrier{
            static_cast<uint32_t>(readonly_access_types.size()),
            readonly_access_types.data(),
            static_cast<uint32_t>(writeonly_access_types.size()),
            writeonly_access_types.data(),
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkBuffer)pointLightIndices->Handle,
            0u,
            point_light_indices_size
        },
        // Output keys
		ThsvsBufferBarrier{
            static_cast<uint32_t>(readonly_access_types.size()),
            readonly_access_types.data(),
            static_cast<uint32_t>(writeonly_access_types.size()),
            writeonly_access_types.data(),
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkBuffer)pointLightMortonCodes_OUT->Handle,
            0u,
            point_light_codes_size
        },
        // Output values
		ThsvsBufferBarrier{
            static_cast<uint32_t>(readonly_access_types.size()),
            readonly_access_types.data(),
            static_cast<uint32_t>(writeonly_access_types.size()),
            writeonly_access_types.data(),
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkBuffer)pointLightIndices_OUT->Handle,
            0u,
            point_light_indices_size
        }
    };

    if constexpr (VTF_VALIDATION_ENABLED)
    {
        frame.vkDebugFns.vkCmdBeginDebugUtilsLabel(cmd, &debug_label);
    }

    // bind radix sort pipeline now
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, frame.computePipelines["SortMortonCodes"].Handle);

    if (frame.LightCounts.NumPointLights > 0u)
    {
        ScopedRenderSection profiler(*frame.queryPool, cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, "SortPointLightMortonCodes");

        // bind proper resources to the descriptor
        binder.BindResourceToIdx("MergeSortResources", "InputKeys", VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, pointLightMortonCodes);
        binder.BindResourceToIdx("MergeSortResources", "InputValues", VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, pointLightIndices);
        binder.BindResourceToIdx("MergeSortResources", "OutputKeys", VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, pointLightMortonCodes_OUT);
        binder.BindResourceToIdx("MergeSortResources", "OutputValues", VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, pointLightIndices_OUT);
        binder.Update();

        // Set sort params: then we'll push updated struct in as raw data
        SortParams sort_params_data;
        sort_params_data.NumElements = frame.LightCounts.NumPointLights;
        sort_params_data.ChunkSize = SORT_NUM_THREADS_PER_THREAD_GROUP;

        binder.Bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
        vkCmdPushConstants(cmd, required_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0u, sizeof(uint32_t), &sort_params_data);
        uint32_t num_thread_groups = static_cast<uint32_t>(glm::ceil(float(frame.LightCounts.NumPointLights) / float(SORT_NUM_THREADS_PER_THREAD_GROUP)));
        vkCmdDispatch(cmd, num_thread_groups, 1u, 1u);

        // copy resources back into results
        const VkBufferCopy point_light_codes_copy{ 0, 0, point_light_codes_size };
        vkCmdCopyBuffer(cmd, (VkBuffer)pointLightMortonCodes_OUT->Handle, (VkBuffer)pointLightMortonCodes->Handle, 1, &point_light_codes_copy);
        const VkBufferCopy point_light_indices_copy{ 0, 0, point_light_indices_size };
        vkCmdCopyBuffer(cmd, (VkBuffer)pointLightIndices_OUT->Handle, (VkBuffer)pointLightIndices->Handle, 1, &point_light_indices_copy);

		thsvsCmdPipelineBarrier(cmd, nullptr, static_cast<uint32_t>(radix_sort_barriers.size()), radix_sort_barriers.data(), 0u, nullptr);

    }

    if (frame.LightCounts.NumSpotLights > 0u)
    {
        ScopedRenderSection profiler(*frame.queryPool, cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, "SortSpotLightMortonCodes");

        // update bindings again
        binder.BindResourceToIdx("MergeSortResources", "InputKeys", VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, spotLightMortonCodes);
        binder.BindResourceToIdx("MergeSortResources", "InputValues", VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, spotLightIndices);
        binder.BindResourceToIdx("MergeSortResources", "OutputKeys", VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, spotLightMortonCodes_OUT);
        binder.BindResourceToIdx("MergeSortResources", "OutputValues", VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, spotLightIndices_OUT);
        binder.Update();

        SortParams sort_params_data;
        sort_params_data.NumElements = frame.LightCounts.NumSpotLights;
        sort_params_data.ChunkSize = SORT_NUM_THREADS_PER_THREAD_GROUP;

        binder.Bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
        vkCmdPushConstants(cmd, required_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0u, sizeof(uint32_t), &sort_params_data);
        uint32_t num_thread_groups = static_cast<uint32_t>(glm::ceil(float(frame.LightCounts.NumSpotLights) / float(SORT_NUM_THREADS_PER_THREAD_GROUP)));
        vkCmdDispatch(cmd, num_thread_groups, 1u, 1u);

        const VkBufferCopy spot_light_codes_copy{ 0, 0, spot_light_codes_size };
        vkCmdCopyBuffer(cmd, (VkBuffer)spotLightMortonCodes_OUT->Handle, (VkBuffer)spotLightMortonCodes->Handle, 1, &spot_light_codes_copy);
        const VkBufferCopy spot_light_indices_copy{ 0, 0, spot_light_indices_size };
        vkCmdCopyBuffer(cmd, (VkBuffer)spotLightIndices_OUT->Handle, (VkBuffer)spotLightIndices->Handle, 1, &spot_light_indices_copy);

        radix_sort_barriers[0].buffer = (VkBuffer)spotLightMortonCodes->Handle;
        radix_sort_barriers[0].size = spot_light_codes_size;
        radix_sort_barriers[1].buffer = (VkBuffer)spotLightIndices->Handle;
        radix_sort_barriers[1].size = spot_light_codes_size;
        radix_sort_barriers[2].buffer = (VkBuffer)spotLightMortonCodes_OUT->Handle;
        radix_sort_barriers[2].size = spot_light_indices_size;
        radix_sort_barriers[3].buffer = (VkBuffer)spotLightIndices_OUT->Handle;
        radix_sort_barriers[3].size = spot_light_indices_size;

        thsvsCmdPipelineBarrier(cmd, nullptr, static_cast<uint32_t>(radix_sort_barriers.size()), radix_sort_barriers.data(), 0u, nullptr);
    }

    if (frame.LightCounts.NumPointLights > 0u)
    {
        constexpr static VkDebugUtilsLabelEXT debug_label_mspl {
            VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
            nullptr,
            "MergeSortPointLights",
            { 66.0f / 255.0f, 119.0f / 255.0f, 244.0f / 255.0f, 1.0f }
        };

        if constexpr (VTF_VALIDATION_ENABLED)
        {
            frame.vkDebugFns.vkCmdInsertDebugUtilsLabel(cmd, &debug_label_mspl);
        }

        MergeSort(frame, cmd, pointLightMortonCodes, pointLightIndices, pointLightMortonCodes_OUT, pointLightIndices_OUT, frame.LightCounts.NumPointLights, SORT_NUM_THREADS_PER_THREAD_GROUP, binder);
    }

    if (frame.LightCounts.NumSpotLights > 0u)
    {
        constexpr static VkDebugUtilsLabelEXT debug_label_msspl{
               VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
               nullptr,
               "MergeSortSpotLights",
               { 66.0f / 255.0f, 119.0f / 255.0f, 244.0f / 255.0f, 1.0f }
        };

        if constexpr (VTF_VALIDATION_ENABLED)
        {
            frame.vkDebugFns.vkCmdInsertDebugUtilsLabel(cmd, &debug_label_msspl);
        }

        MergeSort(frame, cmd, spotLightMortonCodes, spotLightIndices, spotLightMortonCodes_OUT, spotLightIndices_OUT, frame.LightCounts.NumSpotLights, SORT_NUM_THREADS_PER_THREAD_GROUP, binder);
    }

	const ThsvsGlobalBarrier global_barrier{
		static_cast<uint32_t>(writeonly_access_types.size()),
		writeonly_access_types.data(),
		static_cast<uint32_t>(readonly_access_types.size()),
		readonly_access_types.data()
	};

    // another execution barrier. makes sure all compute shaders finish up to this point 
	thsvsCmdPipelineBarrier(cmd, &global_barrier, 0u, nullptr, 0u, nullptr);

    if constexpr (VTF_VALIDATION_ENABLED)
    {
        frame.vkDebugFns.vkCmdEndDebugUtilsLabel(cmd);
    }

}

void buildLightBVH(vtf_frame_data_t& frame, VkCommandBuffer cmd)
{

	ScopedRenderSection profiler(*frame.queryPool, cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, "BuildLightBVH");

    constexpr static VkDebugUtilsLabelEXT debug_label{
        VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        nullptr,
        "BuildLightBVH",
        { 66.0f / 255.0f, 209.0f / 255.0f, 244.0f / 255.0f, 1.0f }
    };
    
    auto& rsrc = ResourceContext::Get();
    auto& bvh_params_rsrc = frame.rsrcMap["BVHParams"];
    auto& point_light_bvh = frame.rsrcMap["PointLightBVH"];
    auto& spot_light_bvh = frame.rsrcMap["SpotLightBVH"];
    const VkPipelineLayout req_layout = frame.descriptorPack->PipelineLayout("BuildBVH");
    const VkDeviceSize point_light_bvh_size = reinterpret_cast<const VkBufferCreateInfo*>(point_light_bvh->Info)->size;
    const VkDeviceSize spot_light_bvh_size = reinterpret_cast<const VkBufferCreateInfo*>(spot_light_bvh->Info)->size;
    uint32_t max_leaves = glm::max(frame.LightCounts.NumPointLights, frame.LightCounts.NumSpotLights);
    uint32_t num_thread_groups = static_cast<uint32_t>(glm::ceil(float(max_leaves) / float(BVH_NUM_THREADS)));

    constexpr static std::array<ThsvsAccessType, 2> access_types {
        THSVS_ACCESS_COMPUTE_SHADER_READ_OTHER,
        THSVS_ACCESS_COMPUTE_SHADER_WRITE
    };

    ThsvsBufferBarrier buffer_barriers[2]{
        ThsvsBufferBarrier{
            static_cast<uint32_t>(access_types.size()),
            access_types.data(),
            static_cast<uint32_t>(access_types.size()),
            access_types.data(),
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkBuffer)point_light_bvh->Handle,
            0u,
            reinterpret_cast<const VkBufferCreateInfo*>(point_light_bvh->Info)->size
        },
        ThsvsBufferBarrier{
            static_cast<uint32_t>(access_types.size()),
            access_types.data(),
            static_cast<uint32_t>(access_types.size()),
            access_types.data(),
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkBuffer)spot_light_bvh->Handle,
            0u,
            reinterpret_cast<const VkBufferCreateInfo*>(spot_light_bvh->Info)->size
        },
    };

    if constexpr (VTF_VALIDATION_ENABLED)
    {
        frame.vkDebugFns.vkCmdBeginDebugUtilsLabel(cmd, &debug_label);
    }

    vkCmdFillBuffer(cmd, (VkBuffer)point_light_bvh->Handle, 0u, point_light_bvh_size, 0u);
    vkCmdFillBuffer(cmd, (VkBuffer)spot_light_bvh->Handle, 0u, spot_light_bvh_size, 0u);

    frame.BVH_Params.PointLightLevels = GetNumLevelsBVH(frame.LightCounts.NumPointLights);
    frame.BVH_Params.SpotLightLevels = GetNumLevelsBVH(frame.LightCounts.NumSpotLights);

    gpu_resource_data_t bvh_update{ &frame.BVH_Params, sizeof(frame.BVH_Params), 0u, VK_QUEUE_FAMILY_IGNORED };
    rsrc.SetBufferData(bvh_params_rsrc, 1, &bvh_update);

    auto binder = frame.descriptorPack->RetrieveBinder("BuildBVH");
    binder.Update();
    
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, frame.computePipelines["BuildBVHBottomPipeline"].Handle);
        binder.Bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
        vkCmdDispatch(cmd, num_thread_groups, 1u, 1u);
    }

    uint32_t max_levels = glm::max(frame.BVH_Params.PointLightLevels, frame.BVH_Params.SpotLightLevels);
    if (max_levels > 0u)
    {
        // Won't we need to barrier things more thoroughly? Track this frame in renderdoc!
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, frame.computePipelines["BuildBVHTopPipeline"].Handle);
        for (uint32_t level = max_levels - 1u; level > 0u; --level) {
            thsvsCmdPipelineBarrier(cmd, nullptr, 2u, buffer_barriers, 0u, nullptr);
            uint32_t num_child_nodes = NumLevelNodes[level];
            num_thread_groups = static_cast<uint32_t>(glm::ceil(float(NumLevelNodes[level]) / float(BVH_NUM_THREADS)));
            const uint32_t pushed_level = level;
            vkCmdPushConstants(cmd, req_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0u, sizeof(uint32_t), &pushed_level);
            vkCmdDispatch(cmd, num_thread_groups, 1u, 1u);
        }
    }

    // and another one to close us out: but from now on we only read these, so update that
    buffer_barriers[0].nextAccessCount = 1u;
    buffer_barriers[1].nextAccessCount = 1u;
    thsvsCmdPipelineBarrier(cmd, nullptr, 2u, buffer_barriers, 0u, nullptr);
    // TODO: this might not be needed, given that we're submitting next?

    if constexpr (VTF_VALIDATION_ENABLED)
    {
        frame.vkDebugFns.vkCmdEndDebugUtilsLabel(cmd);
    }

}

void UpdateClusterGrid(vtf_frame_data_t& frame)
{
    ComputeClusterAABBs(frame);
}

void UpdateFrameResources(vtf_frame_data_t& frame)
{
    uploadLightsToGPU(frame);
}

void ComputeClusterAABBs(vtf_frame_data_t& frame)
{

    constexpr static VkDebugUtilsLabelEXT debug_label{
        VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        nullptr,
        "ComputeClusterAABBs",
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
    auto binder = frame.descriptorPack->RetrieveBinder("ComputeClusterAABBs");
	binder.Update();
    binder.Bind(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE);
    vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, frame.computePipelines["ComputeClusterAABBsPipeline"].Handle);
    const uint32_t dispatch_size = static_cast<uint32_t>(glm::ceil((frame.ClusterData.GridDim.x * frame.ClusterData.GridDim.y * frame.ClusterData.GridDim.z) / 1024.0f));
    vkCmdDispatch(cmd_buffer, dispatch_size, 1u, 1u);
    if constexpr (VTF_VALIDATION_ENABLED) {
        frame.vkDebugFns.vkCmdEndDebugUtilsLabel(cmd_buffer);
    }
    vkEndCommandBuffer(cmd_buffer);

	const VkPipelineStageFlags wait_mask{};

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

void SubmitComputeWork(vtf_frame_data_t& frame, uint32_t num_cmds, VkCommandBuffer* cmds)
{

    constexpr static VkPipelineStageFlags wait_mask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    auto* device = RenderingContext::Get().Device();

    const VkSubmitInfo submit_info{
        VK_STRUCTURE_TYPE_SUBMIT_INFO,
        nullptr,
        0u,
        nullptr,
        0,
        num_cmds,
        cmds,
        1,
        &frame.semaphores.at("ComputeUpdateComplete")->vkHandle()
    };
    VkResult result = vkQueueSubmit(device->ComputeQueue(), 1, &submit_info, frame.computePoolUsageFence->vkHandle());
    VkAssert(result);

}

void ComputeUpdate(vtf_frame_data_t& frame)
{
	constexpr VkCommandBufferBeginInfo base_info{
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		nullptr,
		VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT
	};

	constexpr static VkDebugUtilsLabelEXT queue_debug_label{
		VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
		nullptr,
		"ComputeUpdate",
		{ 63.0f / 255.0f, 197.0f / 255.0f, 255.0f / 255.0f, 1.0f }
	};

	auto* device = RenderingContext::Get().Device();
	if constexpr (VTF_VALIDATION_ENABLED && VTF_USE_DEBUG_INFO)
	{
		frame.vkDebugFns.vkQueueBeginDebugUtilsLabel(device->ComputeQueue(), &queue_debug_label);
	}

	if (!frame.firstComputeSubmit)
	{
		VkResult result = vkWaitForFences(device->vkHandle(), 1u, &frame.computePoolUsageFence->vkHandle(), VK_TRUE, UINT64_MAX);
		VkAssert(result);
		result = vkResetFences(device->vkHandle(), 1u, &frame.computePoolUsageFence->vkHandle());
		VkAssert(result);
		frame.computePool->ResetCmdPool();
	}

	VkCommandBuffer cmd_buffer = frame.computePool->GetCmdBuffer(1u);
	VkResult result = vkBeginCommandBuffer(cmd_buffer, &base_info);
	VkAssert(result);

		computeUpdateLights(frame, cmd_buffer);
        if (frame.optimizedLighting)
        {
            // don't need to do this for regular mode
            reduceLights(frame, cmd_buffer);
            computeMortonCodes(frame, cmd_buffer);
            sortMortonCodes(frame, cmd_buffer);
            buildLightBVH(frame, cmd_buffer);
        }

	result = vkEndCommandBuffer(cmd_buffer);
	VkAssert(result);

	SubmitComputeWork(frame, 1u, &cmd_buffer);

    if constexpr (VTF_VALIDATION_ENABLED && VTF_USE_DEBUG_INFO)
    {
        frame.vkDebugFns.vkQueueEndDebugUtilsLabel(device->ComputeQueue());
    }

	frame.firstComputeSubmit = false;
}

bool tryImageAcquire(vtf_frame_data_t& frame, uint64_t timeout = 0u)
{
    auto* device = RenderingContext::Get().Device();
    auto* swapchain = RenderingContext::Get().Swapchain();
    VkResult result = vkAcquireNextImageKHR(device->vkHandle(), swapchain->vkHandle(), timeout, frame.semaphores.at("ImageAcquire")->vkHandle(), VK_NULL_HANDLE, &frame.imageIdx);
    if (result == VK_SUCCESS)
    {
        return true;
    }
    else
    {
        if (result != VK_NOT_READY && timeout == 0u)
        {
            VkAssert(result); // something besides what we plan to handle occured
        }
        return false;
    }
}

void vtfDrawDebugClusters(vtf_frame_data_t& frame, VkCommandBuffer cmd)
{
    VkViewport viewport;
    VkRect2D scissor;
    auto current_dims = RenderingContext::Get().Swapchain()->Extent();
    viewport.width = static_cast<float>(current_dims.width);
    viewport.height = static_cast<float>(current_dims.height);
    viewport.x = 0;
    viewport.y = 0;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    scissor.extent = current_dims;
    scissor.offset.x = 0;
    scissor.offset.y = 0;

    constexpr static VkDebugUtilsLabelEXT debug_label{
        VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        nullptr,
        "DrawDebugClusters",
        { 244.0f / 255.0f, 176.0f / 255.0f, 66.0f / 255.0f, 1.0f }
    };

    if constexpr (VTF_VALIDATION_ENABLED && VTF_USE_DEBUG_INFO)
    {
        frame.vkDebugFns.vkCmdBeginDebugUtilsLabel(cmd, &debug_label);
    }

    const std::array<glm::mat4, 2> debug_clusters_matrices {
        frame.Matrices.view * glm::inverse(frame.previousViewMatrix),
        frame.Matrices.projection
    };

    const gpu_resource_data_t resource_data{
        debug_clusters_matrices.data(),
        sizeof(glm::mat4) * debug_clusters_matrices.size(),
        0u,
        VK_QUEUE_FAMILY_IGNORED
    };

    auto& ctxt = ResourceContext::Get();
    auto& debug_matrices = frame.rsrcMap.at("DebugClustersMatrices");
    ctxt.SetBufferData(debug_matrices, 1u, &resource_data);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, frame.graphicsPipelines.at("DebugClustersPipeline")->vkHandle());
    vkCmdSetViewport(cmd, 0u, 1u, &viewport);
    vkCmdSetScissor(cmd, 0u, 1u, &scissor);
    auto& descriptor = frame.descriptorPack->RetrieveBinder("DebugClusters");
    auto& prev_unique_clusters = frame.rsrcMap.at("PreviousUniqueClusters");
    auto& cluster_colors = frame.rsrcMap.at("DebugClusterColors");
    descriptor.BindResourceToIdx("VolumetricForward", "UniqueClusters", VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, prev_unique_clusters);
    descriptor.BindResourceToIdx("Debug", "DebugClustersMatrices", VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, debug_matrices);
    descriptor.BindResourceToIdx("Debug", "ClusterColors", VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, cluster_colors);
    descriptor.Update();
    descriptor.Bind(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS);
    vkCmdDrawIndirect(cmd, (VkBuffer)frame.rsrcMap.at("DebugClustersIndirectArgs")->Handle, 0u, 1u, sizeof(VkDrawIndirectCommand));

    if constexpr (VTF_VALIDATION_ENABLED)
    {
        frame.vkDebugFns.vkCmdEndDebugUtilsLabel(cmd);
    }

}

void getClusterSamples(vtf_frame_data_t& frame, VkCommandBuffer cmd) {

    objRenderStateData::materialLayout = frame.descriptorPack->PipelineLayout("ClusterSamples");

	ScopedRenderSection profiler(*frame.queryPool, cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, "DepthPrePassAndClusterSamples");

    constexpr static VkDebugUtilsLabelEXT debug_label{
        VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        nullptr,
        "DepthPrePassAndClusterSamples",
        { 244.0f / 255.0f, 66.0f / 255.0f, 66.0f / 255.0f, 1.0f }
    };

    auto* renderpass = frame.renderPasses.at("DepthAndClusterSamplesPass").get();
    auto& cluster_flags = frame.rsrcMap.at("ClusterFlags");
    auto binder0 = frame.descriptorPack->RetrieveBinder("DepthPrePass");
    auto binder1 = frame.descriptorPack->RetrieveBinder("ClusterSamples");

    objRenderStateData state_data{
        cmd,
        &binder0,
        render_type::PrePass
    };

    renderpass->UpdateBeginInfo(frame.clusterSamplesFramebuffer->vkHandle());
    binder0.Update();
    binder1.Update();

    if constexpr (VTF_VALIDATION_ENABLED)
    {
        frame.vkDebugFns.vkCmdBeginDebugUtilsLabel(cmd, &debug_label);
    }

    vkCmdFillBuffer(cmd, (VkBuffer)cluster_flags->Handle, 0u, reinterpret_cast<const VkBufferCreateInfo*>(cluster_flags->Info)->size, 0u);

    vkCmdBeginRenderPass(cmd, &renderpass->BeginInfo(), VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, frame.graphicsPipelines.at("DepthPrePassPipeline")->vkHandle());

		for (size_t i = 0; i < frame.renderFns.size(); ++i)
		{
			frame.renderFns[i](state_data); // opaque for depth
		}
    vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, frame.graphicsPipelines.at("ClusterSamplesPipeline")->vkHandle());

        state_data.type = render_type::Opaque;
        state_data.binder = &binder1;

		for (size_t i = 0; i < frame.renderFns.size(); ++i)
		{
			frame.renderFns[i](state_data); // opaque for depth
		}
    vkCmdEndRenderPass(cmd);

	constexpr static std::array<ThsvsAccessType, 1> writeonly_access_types {
		THSVS_ACCESS_FRAGMENT_SHADER_WRITE
	};

	constexpr static std::array<ThsvsAccessType, 1> readonly_access_types {
		THSVS_ACCESS_COMPUTE_SHADER_READ_OTHER
	};

	const ThsvsBufferBarrier cluster_flags_barrier{
		static_cast<uint32_t>(writeonly_access_types.size()),
		writeonly_access_types.data(),
		static_cast<uint32_t>(readonly_access_types.size()),
		readonly_access_types.data(),
		VK_QUEUE_FAMILY_IGNORED,
		VK_QUEUE_FAMILY_IGNORED,
		(VkBuffer)cluster_flags->Handle,
		0u,
		reinterpret_cast<const VkBufferCreateInfo*>(cluster_flags->Info)->size
	};

    thsvsCmdPipelineBarrier(cmd, 0u, 1u, &cluster_flags_barrier, 0u, nullptr);

    if constexpr (VTF_VALIDATION_ENABLED)
    {
        frame.vkDebugFns.vkCmdEndDebugUtilsLabel(cmd);
    }

}

void findUniqueClusters(vtf_frame_data_t& frame, VkCommandBuffer cmd)
{

	ScopedRenderSection profiler(*frame.queryPool, cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, "FindUniqueClusters");

    constexpr static VkDebugUtilsLabelEXT debug_label{
        VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        nullptr,
        "FindUniqueClusters",
        { 244.0f / 255.0f, 134.0f / 255.0f, 66.0f / 255.0f, 1.0f }
    };

    auto& unique_clusters = frame.rsrcMap.at("UniqueClusters");
    auto& unique_clusters_counter = frame.rsrcMap.at("UniqueClustersCounter");
    auto& cluster_flags = frame.rsrcMap.at("ClusterFlags");
    auto binder = frame.descriptorPack->RetrieveBinder("FindUniqueClusters");
    binder.Update();

    const VkDeviceSize unique_clusters_size = reinterpret_cast<const VkBufferCreateInfo*>(unique_clusters->Info)->size;
    const VkDeviceSize counter_size = reinterpret_cast<const VkBufferCreateInfo*>(unique_clusters_counter->Info)->size;
    const VkDeviceSize cluster_flags_size = reinterpret_cast<const VkBufferCreateInfo*>(cluster_flags->Info)->size;

    constexpr static std::array<ThsvsAccessType, 2> compute_access_types {
        THSVS_ACCESS_COMPUTE_SHADER_READ_OTHER,
        THSVS_ACCESS_COMPUTE_SHADER_WRITE
    };

    constexpr static std::array<ThsvsAccessType, 1> next_access_types {
        THSVS_ACCESS_COMPUTE_SHADER_READ_OTHER
    };

    const std::array<ThsvsBufferBarrier, 3> buffer_barriers {
        ThsvsBufferBarrier{
            static_cast<uint32_t>(compute_access_types.size()),
            compute_access_types.data(),
            static_cast<uint32_t>(next_access_types.size()),
            next_access_types.data(),
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkBuffer)unique_clusters->Handle,
            0u,
            unique_clusters_size
        },
        ThsvsBufferBarrier{
            static_cast<uint32_t>(compute_access_types.size()),
            compute_access_types.data(),
            static_cast<uint32_t>(next_access_types.size()),
            next_access_types.data(),
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkBuffer)unique_clusters_counter->Handle,
            0u,
            counter_size
        },
        ThsvsBufferBarrier{
            static_cast<uint32_t>(compute_access_types.size()),
            compute_access_types.data(),
            static_cast<uint32_t>(next_access_types.size()),
            next_access_types.data(),
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkBuffer)cluster_flags->Handle,
            0u,
            cluster_flags_size
        },
    };

    if constexpr (VTF_VALIDATION_ENABLED) {
        frame.vkDebugFns.vkCmdBeginDebugUtilsLabel(cmd, &debug_label);
    }

    vkCmdFillBuffer(cmd, (VkBuffer)unique_clusters->Handle, 0u, unique_clusters_size, 0u);
    vkCmdFillBuffer(cmd, (VkBuffer)unique_clusters_counter->Handle, 0u, counter_size, 0u);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, frame.computePipelines.at("FindUniqueClustersPipeline").Handle);
    binder.Bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
    uint32_t max_clusters = frame.ClusterData.GridDim.x * frame.ClusterData.GridDim.y * frame.ClusterData.GridDim.z;
    uint32_t num_thread_groups = static_cast<uint32_t>(glm::ceil((float)max_clusters / 1024.0f));
    vkCmdDispatch(cmd, num_thread_groups, 1u, 1u);
    // probably don't need this since the other stuff is in a separate submit?
    thsvsCmdPipelineBarrier(cmd, nullptr, static_cast<uint32_t>(buffer_barriers.size()), buffer_barriers.data(), 0u, nullptr);

    if constexpr (VTF_VALIDATION_ENABLED) {
        frame.vkDebugFns.vkCmdEndDebugUtilsLabel(cmd);
    }

}

void updateClusterIndirectArgs(vtf_frame_data_t& frame, VkCommandBuffer cmd)
{

	ScopedRenderSection profiler(*frame.queryPool, cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, "UpdateClusterIndirectArgs");

    constexpr static VkDebugUtilsLabelEXT debug_label{
        VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        nullptr,
        "UpdateClusterIndirectArgs",
        { 244.0f / 255.0f, 176.0f / 255.0f, 66.0f / 255.0f, 1.0f }
    };

    constexpr static ThsvsAccessType writeonly_storage_access[1]{
        THSVS_ACCESS_COMPUTE_SHADER_WRITE
    };

    constexpr static ThsvsAccessType indirect_dispatch_access[1]{
        THSVS_ACCESS_INDIRECT_BUFFER
    };

    auto& indir_args_buffer = frame.rsrcMap.at("IndirectArgs");
    auto binder = frame.descriptorPack->RetrieveBinder("UpdateClusterIndirectArgs");
    binder.Update();

    const ThsvsBufferBarrier buffer_barrier{
        1u,
        writeonly_storage_access,
        1u,
        indirect_dispatch_access,
        VK_QUEUE_FAMILY_IGNORED,
        VK_QUEUE_FAMILY_IGNORED,
        (VkBuffer)indir_args_buffer->Handle,
        0u,
        sizeof(VkDispatchIndirectCommand)
    };

    if constexpr (VTF_VALIDATION_ENABLED) {
        frame.vkDebugFns.vkCmdBeginDebugUtilsLabel(cmd, &debug_label);
    }

    const VkPipelineLayout pipeline_layout = frame.descriptorPack->PipelineLayout("UpdateClusterIndirectArgs");

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, frame.computePipelines.at("UpdateIndirectArgsPipeline").Handle);
    const VkBool32 bool_val = (VkBool32)frame.updateUniqueClusters;
    vkCmdPushConstants(cmd, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0u, sizeof(VkBool32), &bool_val);
    binder.Bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
    vkCmdDispatch(cmd, 1u, 1u, 1u);
	thsvsCmdPipelineBarrier(cmd, nullptr, 1u, &buffer_barrier, 0u, nullptr);

    if constexpr (VTF_VALIDATION_ENABLED) {
        frame.vkDebugFns.vkCmdEndDebugUtilsLabel(cmd);
    }

}

void assignLightsToClusters(vtf_frame_data_t& frame, VkCommandBuffer cmd)
{
    auto& rsrc_ctxt = ResourceContext::Get();

    constexpr static const char* label_name{ "AssignLightsToClusters" };
    constexpr static VkDebugUtilsLabelEXT debug_label{
        VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        nullptr,
        label_name,
        { 244.0f / 255.0f, 229.0f / 255.0f, 66.0f / 255.0f, 1.0f }
    };

    VulkanResource* point_light_index_counter = frame.rsrcMap.at("PointLightIndexCounter");
    VulkanResource* spot_light_index_counter = frame.rsrcMap.at("SpotLightIndexCounter");
    VulkanResource* point_light_grid = frame.rsrcMap.at("PointLightGrid");
    VulkanResource* spot_light_grid = frame.rsrcMap.at("SpotLightGrid");
    VulkanResource* point_light_index_list = frame.rsrcMap.at("PointLightIndexList");
    VulkanResource* spot_light_index_list = frame.rsrcMap.at("SpotLightIndexList");
	VulkanResource* indirect_buffer = frame.rsrcMap.at("IndirectArgs");
    VulkanResource* unique_clusters = frame.rsrcMap.at("UniqueClusters");

	/*
		Readonly in this pass:
		UniqueClusters, PointLightIndices, SpotLightIndices
		(Read but not qualified: PointLights, SpotLights, LightAABBs)
		Writeonly:
		PointLightGrid, SpotLightGrid, PointLightIndexList, SpotLightIndexList
		Read/Write:
		PointLightIndexCounter, SpotLightIndexCounter
	*/

    constexpr static std::array<ThsvsAccessType, 2> rw_access_types{
        THSVS_ACCESS_COMPUTE_SHADER_WRITE,
        THSVS_ACCESS_COMPUTE_SHADER_READ_OTHER
    };

    constexpr static ThsvsGlobalBarrier global_barrier{
        static_cast<uint32_t>(rw_access_types.size()),
        rw_access_types.data(),
        static_cast<uint32_t>(rw_access_types.size()),
        rw_access_types.data()
    };

	constexpr static std::array<ThsvsAccessType, 1> writeonly_access_types {
		THSVS_ACCESS_COMPUTE_SHADER_WRITE
	};

	constexpr static std::array<ThsvsAccessType, 2> readonly_access_types {
        THSVS_ACCESS_FRAGMENT_SHADER_READ_OTHER,
        THSVS_ACCESS_COMPUTE_SHADER_READ_OTHER
	};

    const std::array<ThsvsBufferBarrier, 5> assign_lights_barriers {
		ThsvsBufferBarrier{
            static_cast<uint32_t>(writeonly_access_types.size()),
			writeonly_access_types.data(),
			static_cast<uint32_t>(readonly_access_types.size()),
			readonly_access_types.data(),
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkBuffer)point_light_index_list->Handle,
            0u,
            reinterpret_cast<const VkBufferCreateInfo*>(point_light_index_list->Info)->size
        },
		ThsvsBufferBarrier{
            static_cast<uint32_t>(writeonly_access_types.size()),
            writeonly_access_types.data(),
            static_cast<uint32_t>(readonly_access_types.size()),
            readonly_access_types.data(),
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkBuffer)spot_light_index_list->Handle,
            0u,
            reinterpret_cast<const VkBufferCreateInfo*>(spot_light_index_list->Info)->size
        },
		ThsvsBufferBarrier{
            static_cast<uint32_t>(writeonly_access_types.size()),
            writeonly_access_types.data(),
            static_cast<uint32_t>(readonly_access_types.size()),
            readonly_access_types.data(),
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkBuffer)point_light_grid->Handle,
            0u,
            reinterpret_cast<const VkBufferCreateInfo*>(point_light_grid->Info)->size
        },
		ThsvsBufferBarrier{
            static_cast<uint32_t>(writeonly_access_types.size()),
            writeonly_access_types.data(),
            static_cast<uint32_t>(readonly_access_types.size()),
            readonly_access_types.data(),
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkBuffer)spot_light_grid->Handle,
            0u,
            reinterpret_cast<const VkBufferCreateInfo*>(spot_light_grid->Info)->size
        },
        ThsvsBufferBarrier{
            static_cast<uint32_t>(readonly_access_types.size()),
            readonly_access_types.data(),
            static_cast<uint32_t>(readonly_access_types.size()),
            readonly_access_types.data(),
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkBuffer)unique_clusters->Handle,
            0u,
            reinterpret_cast<const VkBufferCreateInfo*>(unique_clusters->Info)->size
        }
    };


    if constexpr (VTF_VALIDATION_ENABLED) {
        frame.vkDebugFns.vkCmdBeginDebugUtilsLabel(cmd, &debug_label);
    }

    vkCmdFillBuffer(cmd, (VkBuffer)point_light_index_counter->Handle, 0u, sizeof(uint32_t), 0u);
    vkCmdFillBuffer(cmd, (VkBuffer)spot_light_index_counter->Handle, 0u, sizeof(uint32_t), 0u);
    vkCmdFillBuffer(cmd, (VkBuffer)point_light_grid->Handle, 0u, reinterpret_cast<const VkBufferCreateInfo*>(point_light_grid->Info)->size, 0u);
    vkCmdFillBuffer(cmd, (VkBuffer)spot_light_grid->Handle, 0u, reinterpret_cast<const VkBufferCreateInfo*>(spot_light_grid->Info)->size, 0u);

    if (frame.optimizedLighting)
    {
        auto descriptor = frame.descriptorPack->RetrieveBinder("AssignLightsToClustersBVH");
        descriptor.BindResourceToIdx("VolumetricForward", "SpotLightIndexList", VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, spot_light_index_list);
        descriptor.BindResourceToIdx("VolumetricForward", "PointLightIndexList", VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, point_light_index_list);
        descriptor.BindResourceToIdx("VolumetricForward", "SpotLightGrid", VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, spot_light_grid);
        descriptor.BindResourceToIdx("VolumetricForward", "PointLightGrid", VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, point_light_grid);
        descriptor.Update();

        frame.BVH_Params.PointLightLevels = GetNumLevelsBVH(frame.LightCounts.NumPointLights);
        frame.BVH_Params.SpotLightLevels = GetNumLevelsBVH(frame.LightCounts.NumSpotLights);

        const gpu_resource_data_t bvh_params_update{
            &frame.BVH_Params,
            sizeof(BVH_Params_t),
            0u, VK_QUEUE_FAMILY_IGNORED
        };

        VulkanResource* bvh_params_rsrc = frame.rsrcMap.at("BVHParams");
        rsrc_ctxt.SetBufferData(bvh_params_rsrc, 1u, &bvh_params_update);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, frame.computePipelines.at("AssignLightsToClustersBVHPipeline").Handle);
        descriptor.Bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
        vkCmdDispatchIndirect(cmd, (VkBuffer)indirect_buffer->Handle, 0u);

    }
    else
    {
        auto descriptor = frame.descriptorPack->RetrieveBinder("AssignLightsToClusters");
        descriptor.BindResourceToIdx("VolumetricForward", "SpotLightIndexList", VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, spot_light_index_list);
        descriptor.BindResourceToIdx("VolumetricForward", "PointLightIndexList", VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, point_light_index_list);
        descriptor.BindResourceToIdx("VolumetricForward", "SpotLightGrid", VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, spot_light_grid);
        descriptor.BindResourceToIdx("VolumetricForward", "PointLightGrid", VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, point_light_grid);
        descriptor.Update();
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, frame.computePipelines.at("AssignLightsToClustersPipeline").Handle);
        descriptor.Bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
        vkCmdDispatchIndirect(cmd, (VkBuffer)indirect_buffer->Handle, 0u);
    }

	thsvsCmdPipelineBarrier(cmd, nullptr, static_cast<uint32_t>(assign_lights_barriers.size()), assign_lights_barriers.data(), 0u, nullptr);

    if constexpr (VTF_VALIDATION_ENABLED) {
        frame.vkDebugFns.vkCmdEndDebugUtilsLabel(cmd);
    }

}

void submitPreSwapchainWritingWork(vtf_frame_data_t& frame, uint32_t num_cmds, VkCommandBuffer* cmd_buffers)
{
    // We're going to submit all work so far, as it doesn't write to the swapchain 
    // and the next chunk of work relies on acquire completing

    const std::array<VkSemaphore, 1> wait_semaphores{
        frame.semaphores.at("ComputeUpdateComplete")->vkHandle()
    };

    const std::array<VkSemaphore, 1> signal_semaphores{
        frame.semaphores.at("PreBackbufferWorkComplete")->vkHandle()
    };

	constexpr static VkPipelineStageFlags wait_mask[2]{ VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT }; // we're waiting a bit early tbh

    const VkSubmitInfo submit_info{
        VK_STRUCTURE_TYPE_SUBMIT_INFO,
        nullptr,
        static_cast<uint32_t>(wait_semaphores.size()),
        wait_semaphores.data(),
        wait_mask,
        num_cmds,
		cmd_buffers,
        static_cast<uint32_t>(signal_semaphores.size()),
        signal_semaphores.data()
    };

    vpr::Device* dvc = RenderingContext::Get().Device();

    VkResult result = vkQueueSubmit(dvc->GraphicsQueue(), 1u, &submit_info, frame.preRenderGraphicsFence->vkHandle());
    VkAssert(result);

}

void vtfMainRenderPass(vtf_frame_data_t& frame, VkCommandBuffer cmd)
{

	ScopedRenderSection profiler(*frame.queryPool, cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, "Draw");

    constexpr static VkDebugUtilsLabelEXT debug_label{
        VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        nullptr, 
        "DrawPass",
        { 66.0f / 255.0f, 244.0f / 255.0f, 95.0f / 255.0f, 1.0f }
    };

    auto& rsrc_ctxt = ResourceContext::Get();

    if constexpr (VTF_VALIDATION_ENABLED) {
        frame.vkDebugFns.vkCmdBeginDebugUtilsLabel(cmd, &debug_label);
    }

    const auto& unique_clusters = frame.rsrcMap.at("UniqueClusters");
    const auto& prev_unique_clusters = frame.rsrcMap.at("PreviousUniqueClusters");
    if (frame.updateUniqueClusters)
    {
        frame.previousViewMatrix = frame.Matrices.view;
        const VkDeviceSize copy_sz = reinterpret_cast<const VkBufferCreateInfo*>(unique_clusters->Info)->size;
        const VkBufferCopy buffer_copy{ 0u, 0u, copy_sz };
        vkCmdCopyBuffer(cmd, (VkBuffer)unique_clusters->Handle, (VkBuffer)prev_unique_clusters->Handle, 1u, &buffer_copy);
    }

    objRenderStateData::materialLayout = frame.descriptorPack->PipelineLayout("DrawPass");
    frame.renderPasses.at("DrawPass")->UpdateBeginInfo(frame.drawFramebuffer->vkHandle());
    auto lights_binder = frame.descriptorPack->RetrieveBinder("DebugLights");
    vkCmdBeginRenderPass(cmd, &frame.renderPasses.at("DrawPass")->BeginInfo(), VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, frame.graphicsPipelines.at("OpaqueDrawPipeline")->vkHandle());
        auto binder0 = frame.descriptorPack->RetrieveBinder("DrawPass");
        binder0.BindResourceToIdx("VolumetricForward", "UniqueClusters", VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, unique_clusters);
        binder0.Update();

        objRenderStateData draw_state{
            cmd,
            &binder0,
            render_type::Opaque
        };

		for (size_t i = 0u; i < frame.renderFns.size(); ++i)
		{
			frame.renderFns[i](draw_state);
		}
		for (size_t i = 0u; i < frame.renderFns.size(); ++i)
		{
            draw_state.type = render_type::Transparent;
			frame.renderFns[i](draw_state);
		}
        for (size_t i = 0u; i < frame.renderFns.size(); ++i)
        {
            draw_state.type = render_type::GUI;
            frame.renderFns[i](draw_state);
        }

        if (frame.renderDebugClusters)
        {
            binder0.BindResourceToIdx("VolumetricForward", "UniqueClusters", VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, prev_unique_clusters);
            binder0.Update();
            binder0.Bind(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS);
            vtfDrawDebugClusters(frame, cmd);
        }

    vkCmdEndRenderPass(cmd);
    if constexpr (VTF_VALIDATION_ENABLED) {
        frame.vkDebugFns.vkCmdEndDebugUtilsLabel(cmd);
    }

    // debug passes would also go here

}

static std::array<size_t, 8> acquisitionFrequencies{ 0u, 0u, 0u, 0u };

#ifdef NDEBUG
constexpr static bool DEBUG_MODE{ false };
#else
constexpr static bool DEBUG_MODE{ true };
#endif

void RenderVtf(vtf_frame_data_t& frame)
{

	constexpr static VkCommandBufferBeginInfo begin_info{
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		nullptr,
		VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
		nullptr
	};

	constexpr static VkDebugUtilsLabelEXT queue_debug_label{
		VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
		nullptr,
		"RenderVtf",
		{ 133.0f / 255.0f, 63.0f / 255.0f, 255.0f / 255.0f, 1.0f }
	};

	auto* device = RenderingContext::Get().Device();

	if constexpr (VTF_VALIDATION_ENABLED && VTF_USE_DEBUG_INFO)
	{
		frame.vkDebugFns.vkQueueBeginDebugUtilsLabel(device->GraphicsQueue(), &queue_debug_label);
	}

	if (!frame.firstGraphicsSubmit)
	{
        // i combined both of the graphics queues here, wait-wise
        const VkFence wait_fences[2]{ frame.preRenderGraphicsFence->vkHandle(), frame.graphicsPoolUsageFence->vkHandle() };
		VkResult result = vkWaitForFences(device->vkHandle(), 2u, wait_fences, VK_TRUE, UINT64_MAX);
		VkAssert(result);
		result = vkResetFences(device->vkHandle(), 2u, wait_fences);
		VkAssert(result);
		frame.graphicsPool->ResetCmdPool();
		// also free transient resources
		auto& ctxt = ResourceContext::Get();
		for (auto* resource : frame.lastFrameTransientResources)
		{
			ctxt.DestroyResource(resource);
		}

        frame.lastFrameTransientResources.clear();
        frame.lastFrameTransientResources.shrink_to_fit();

        /*
            Reset descriptors
        */
        frame.descriptorPack->Reset();

        /*
            Trim command pools
        */
        vkTrimCommandPool(device->vkHandle(), frame.graphicsPool->vkHandle(), 0);
        vkTrimCommandPool(device->vkHandle(), frame.computePool->vkHandle(), 0);

	}

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

	VkCommandBuffer cmd0 = frame.graphicsPool->GetCmdBuffer(0u);
	VkResult result = vkBeginCommandBuffer(cmd0, &begin_info);
	VkAssert(result);

    getClusterSamples(frame, cmd0);
    findUniqueClusters(frame, cmd0);

    if (!acquired) {
        acquired = tryImageAcquire(frame);
        if constexpr (DEBUG_MODE) {
            if (acquired) {
                acquisitionFrequencies[1]++;
            }
        }
    }

    updateClusterIndirectArgs(frame, cmd0);
    assignLightsToClusters(frame, cmd0);
	result = vkEndCommandBuffer(cmd0);
	VkAssert(result);

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
    submitPreSwapchainWritingWork(frame, 1u, &cmd0);
    
    if (!acquired) {
        acquired = tryImageAcquire(frame, UINT64_MAX);
        if constexpr (DEBUG_MODE) {
            std::cerr << "Had to acquire swapchain iamge by blocking!!";
            acquisitionFrequencies[3]++;
        }
    }

	VkCommandBuffer cmd1 = frame.graphicsPool->GetCmdBuffer(1u);
	result = vkBeginCommandBuffer(cmd1, &begin_info);
	VkAssert(result);
    createDrawFrameBuffer(frame);
    vtfMainRenderPass(frame, cmd1);
	result = vkEndCommandBuffer(cmd1);
	VkAssert(result);
	frame.firstGraphicsSubmit = false;

	if (VTF_VALIDATION_ENABLED && VTF_USE_DEBUG_INFO)
	{
		frame.vkDebugFns.vkQueueEndDebugUtilsLabel(device->GraphicsQueue());
	}

}

void SubmitGraphicsWork(vtf_frame_data_t& frame)
{
    static uint32_t frameIdx{ 0u };

    const VkSemaphore wait_semaphores[2]{
        frame.semaphores.at("PreBackbufferWorkComplete")->vkHandle(),
        frame.semaphores.at("ImageAcquire")->vkHandle()
    };

    auto* device = RenderingContext::Get().Device();

    // Need to wait for these as we use the depth pre-pass' output iirc?
	constexpr static VkPipelineStageFlags wait_mask[2]{ VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT };

    const VkSubmitInfo submit_info{
        VK_STRUCTURE_TYPE_SUBMIT_INFO,
        nullptr,
        2u,
        wait_semaphores,
        wait_mask,
        1u,
        &frame.graphicsPool->GetCmdBuffer(1u),
        1u,
        &frame.semaphores.at("RenderComplete")->vkHandle()
    };

    VkResult result = vkQueueSubmit(device->GraphicsQueue(), 1u, &submit_info, frame.graphicsPoolUsageFence->vkHandle());
    VkAssert(result);

    frame.lastImageIdx = frame.imageIdx;

	// get timestamps for last frames work

    auto results = frame.queryPool->GetTimestamps();
    if ((frameIdx % 360u == 0u) || frameIdx == 0u)
    {
        // log results to console briefly
        std::cerr << "Time elapsed per stage in last frame: \n";
        size_t counter{ 0u };
        for (auto& result : results)
        {
            std::cerr << "| ";
            std::cerr << result.first;
            std::cerr << " | ";
            std::cerr << std::to_string(result.second) << "ms |";

            if ((counter % 2u == 0u) && (counter != 0u))
            {
                std::cerr << "\n";
            }

            ++counter;
        }
    }

    ++frameIdx;
    frame.lastFrameTransientResources = std::move(frame.transientResources);
    frame.transientResources.reserve(frame.lastFrameTransientResources.size());
}

void FlushShaderCaches()
{
    auto* device = RenderingContext::Get().Device();
    auto* physical_device = RenderingContext::Get().PhysicalDevice();
    vpr::PipelineCache mergedCache(device->vkHandle(), physical_device->vkHandle(), typeid(vtf_frame_data_t).hash_code());

    std::vector<VkPipelineCache> usedCacheHandles;
    for (const auto& cache : vtf_frame_data_t::pipelineCaches)
    {
        usedCacheHandles.emplace_back(cache.second->vkHandle());
    }

    mergedCache.MergeCaches(static_cast<uint32_t>(usedCacheHandles.size()), usedCacheHandles.data());

    vtf_frame_data_t::pipelineCaches.clear();
}

void destroyFrameResources(vtf_frame_data_t & frame)
{
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

void DestroyFrame(vtf_frame_data_t& frame)
{
    destroyFrameResources(frame); // gets semaphores and framebuffers too
    destroyRenderpasses(frame);
    destroyPipelines(frame);
}

void DestroyShaders(vtf_frame_data_t & frame)
{

    for (auto& cache : frame.pipelineCaches)
    {
        cache.second.reset();
    }

    for (auto& shader : frame.shaderModules)
    {
        shader.second.reset();
    }

}

void MergeSort(vtf_frame_data_t& frame, VkCommandBuffer cmd, VulkanResource* src_keys, VulkanResource* src_values, VulkanResource* dst_keys, VulkanResource* dst_values,
    uint32_t total_values, uint32_t chunk_size, DescriptorBinder dscr_binder)
{

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
    frame.transientResources.emplace_back(merge_path_partitions);
	
	SortParams sort_params_local;
	auto& rsrc_context = ResourceContext::Get();

    const uint32_t compute_idx = RenderingContext::Get().Device()->QueueFamilyIndices().Compute;

    Descriptor* descriptor = frame.descriptorPack->RetrieveDescriptor("MergeSortResources");
    const VkPipelineLayout required_layout = frame.descriptorPack->PipelineLayout("MergeSort");
    const size_t merge_pp_loc = descriptor->BindingLocation("MergePathPartitions");
    const size_t input_keys_loc = descriptor->BindingLocation("InputKeys");
    const size_t output_keys_loc = descriptor->BindingLocation("OutputKeys");
    const size_t input_values_loc = descriptor->BindingLocation("InputValues");
    const size_t output_values_loc = descriptor->BindingLocation("OutputValues");

    dscr_binder.BindResourceToIdx("MergeSortResources", merge_pp_loc, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, merge_path_partitions);
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
            reinterpret_cast<const VkBufferCreateInfo*>(src_keys->Info)->size
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
            reinterpret_cast<const VkBufferCreateInfo*>(src_values->Info)->size
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
            reinterpret_cast<const VkBufferCreateInfo*>(dst_keys->Info)->size
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
            reinterpret_cast<const VkBufferCreateInfo*>(dst_values->Info)->size
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

    if constexpr (VTF_VALIDATION_ENABLED)
    {
        frame.vkDebugFns.vkCmdBeginDebugUtilsLabel(cmd, &debug_label);
    }

    while (num_chunks > 1)
    {

        ++pass;

        sort_params_local.NumElements = total_values;
        sort_params_local.ChunkSize = chunk_size;
        vkCmdPushConstants(cmd, required_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0u, sizeof(SortParams), &sort_params_local);

        uint32_t num_sort_groups = num_chunks / 2u;
        uint32_t num_thread_groups_per_sort_group = static_cast<uint32_t>(glm::ceil((chunk_size * 2) / static_cast<float>(num_values_per_thread_group)));

        dscr_binder.BindResourceToIdx("MergeSortResources", input_keys_loc, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, src_keys);
        dscr_binder.BindResourceToIdx("MergeSortResources", output_keys_loc, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, dst_keys);
        dscr_binder.BindResourceToIdx("MergeSortResources", input_values_loc, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, src_values);
        dscr_binder.BindResourceToIdx("MergeSortResources", output_values_loc, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, dst_values);
        dscr_binder.Update(); // makes sure bindings are actually proper before binding

        {

            const std::string inserted_label_string = std::string{ "MergeSortPass" } +std::to_string(pass) + std::string("_MergePathPartitions");
            const VkDebugUtilsLabelEXT debug_label_recursion{
                VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
                nullptr,
                inserted_label_string.c_str(),
                { 66.0f / 255.0f, 244.0f / 255.0f, 72.0f / 255.0f, 1.0f } // green, distinct from rest
            };

            if constexpr (VTF_VALIDATION_ENABLED)
            {
                frame.vkDebugFns.vkCmdInsertDebugUtilsLabel(cmd, &debug_label_recursion);
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

            const std::string inserted_label_string = std::string{ "MergeSortPass" } +std::to_string(pass);
            const VkDebugUtilsLabelEXT debug_label_mpp{
                VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
                nullptr,
                inserted_label_string.c_str(),
                { 55.0f / 255.0f, 222.0f / 255.0f, 60.0f / 255.0f, 1.0f } // green, distinct from rest
            };

            if constexpr (VTF_VALIDATION_ENABLED) {
                frame.vkDebugFns.vkCmdInsertDebugUtilsLabel(cmd, &debug_label_mpp);
            }

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

    if (pass % 2u == 1u)
    {
        // if the pass count is odd then we have to copy the results into 
        // where they should actually be

        VkBufferCopy copy{ 0, 0, VK_WHOLE_SIZE };
		const VkDeviceSize keys_size = reinterpret_cast<const VkBufferCreateInfo*>(src_keys->Info)->size;
		copy.size = keys_size;
        vkCmdCopyBuffer(cmd, (VkBuffer)src_keys->Handle, (VkBuffer)dst_keys->Handle, 1, &copy);
		const VkDeviceSize values_size = reinterpret_cast<const VkBufferCreateInfo*>(src_values->Info)->size;
		copy.size = values_size;
        vkCmdCopyBuffer(cmd, (VkBuffer)src_values->Handle, (VkBuffer)dst_values->Handle, 1, &copy);
    }

    if constexpr (VTF_VALIDATION_ENABLED)
    {
        frame.vkDebugFns.vkCmdEndDebugUtilsLabel(cmd);
    }

}

VulkanResource* createDepthStencilResource(const VkSampleCountFlagBits samples)
{
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
