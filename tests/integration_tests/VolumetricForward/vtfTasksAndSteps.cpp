#include "vtfTasksAndSteps.hpp"
#include <cstdint>

void ComputeUpdateLights(vtf_frame_data_t& frame_data) {
    // update light counts
    LightCounts.NumPointLights = static_cast<uint32_t>(State.PointLights.size());
    LightCounts.NumSpotLights = static_cast<uint32_t>(State.SpotLights.size());
    LightCounts.NumDirectionalLights = static_cast<uint32_t>(State.DirectionalLights.size());
    VulkanResource* light_counts_buffer = currFrameResources->rsrcMap["lightCounts"];
    const gpu_resource_data_t lcb_update{
        &LightCounts,
        sizeof(LightCounts)
    };
    rsrc.SetBufferData(light_counts_buffer, 1, &lcb_update);
    // update light positions etc
    auto cmd = computePools[0]->GetCmdBuffer(0);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, updateLightsPipeline->Handle);
    //resourcePack->BindGroupSets(cmd, "UpdateLights", VK_PIPELINE_BIND_POINT_COMPUTE);
    uint32_t num_groups_x = glm::max(LightCounts.NumPointLights, glm::max(LightCounts.NumDirectionalLights, LightCounts.NumSpotLights));
    num_groups_x = static_cast<uint32_t>(glm::ceil(num_groups_x / 1024.0f));
    vkCmdDispatch(cmd, num_groups_x, 1, 1);
}
