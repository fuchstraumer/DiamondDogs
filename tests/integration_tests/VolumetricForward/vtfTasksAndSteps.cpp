#include "vtfTasksAndSteps.hpp"
#include "vtfStructs.hpp"
#include "vtfFrameData.hpp"
#include <cstdint>
#include <vulkan/vulkan.h>
#include "CommandPool.hpp"
#include "LogicalDevice.hpp"
#include "Fence.hpp"
#include "Semaphore.hpp"
#include "Swapchain.hpp"
#include "ResourceContext.hpp"
#include "RenderingContext.hpp"
#include "DescriptorPack.hpp"
#include "Descriptor.hpp"
#include "DescriptorBinder.hpp"
#include "PerspectiveCamera.hpp"
#include "vkAssert.hpp"

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

    rsrcs["clusterAABBs"] = rsrc_context.CreateBuffer(&cluster_aabbs_info, nullptr, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr);

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

    rsrcs["pointLightGrid"] = rsrc_context.CreateBuffer(&point_light_grid_info, &point_light_grid_view_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr);
    rsrcs["spotLightGrid"] = rsrc_context.CreateBuffer(&point_light_grid_info, &point_light_grid_view_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr);

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

    rsrcs["pointLightIndexList"] = rsrc_context.CreateBuffer(&indices_info, &indices_view_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr);
    rsrcs["spotLightIndexList"] = rsrc_context.CreateBuffer(&indices_info, &indices_view_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr);

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
