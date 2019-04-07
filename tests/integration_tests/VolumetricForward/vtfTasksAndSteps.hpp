#pragma once
#ifndef VTF_TASKS_AND_STEPS_HPP
#define VTF_TASKS_AND_STEPS_HPP
#include <cstdint>
#include <vulkan/vulkan.h>

class vtf_frame_data_t;

namespace st {
    class ShaderPack;
}

/*
    Setup functions for frame data: should be called in this order. 
    CreateShaders() actually only progresses once. Multithreaded
    reads are performed on several containers - accesses to VkPipelineCache
    entries should be safe if internal state is mutated, as those 
    are one of few internally synchronized objects
*/
void CreateShaders(const st::ShaderPack* pack);
void SetupDescriptors(vtf_frame_data_t& frame);
void CreateResources(vtf_frame_data_t& frame); // populates rsrcMap nearly in full
void CreateSemaphores(vtf_frame_data_t& frame);
void CreateComputePipelines(vtf_frame_data_t& frame);
void CreateRenderpasses(vtf_frame_data_t& frame); // must be done before graphics pipelines, they need renderpass handles
void CreateDrawFrameBuffers(vtf_frame_data_t& frame, const size_t frame_idx);
void CreateGraphicsPipelines(vtf_frame_data_t& frame);
// Executes everything but creating the framebuffers for drawing
void FullFrameSetup(vtf_frame_data_t* frame);
void CalculateGridDims(uint32_t& grid_x, uint32_t& grid_y, uint32_t& grid_z);

/*
    Compute work for a frame has been reduced into discrete
    independent functions, split by pipeline (and thus shader)
    usage. These should still be invoked in the proper order,
    but can now be executed across multiple threads far easier
*/

void UpdateClusterGrid(vtf_frame_data_t& frame);
void ComputeClusterAABBs(vtf_frame_data_t& frame);
void ComputeUpdate(vtf_frame_data_t& frame);
void SubmitComputeWork(vtf_frame_data_t& frame, uint32_t num_cmds, VkCommandBuffer* cmds);
void RenderVtf(vtf_frame_data_t& frame);
void SubmitGraphicsWork(vtf_frame_data_t& frame_data);

/*
    Destruction functions. Just as important for shutdown, or for 
    re-creating things after a swapchain event.
*/
void DestroyFrame(vtf_frame_data_t& frame);
void DestroyShaders(vtf_frame_data_t& frame);

#endif // !VTF_TASKS_AND_STEPS_HPP
