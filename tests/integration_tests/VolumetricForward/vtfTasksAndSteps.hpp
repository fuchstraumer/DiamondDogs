#pragma once
#ifndef VTF_TASKS_AND_STEPS_HPP
#define VTF_TASKS_AND_STEPS_HPP

class vtf_frame_data_t;

void ComputeUpdateLights(vtf_frame_data_t& frame_data);
void ComputeReduceLights(vtf_frame_data_t& frame_data);
void ComputeAndSortMortonCodes(vtf_frame_data_t& frame_data);
void BuildLightBVH(vtf_frame_data_t& frame_data);
void UpdateClusterGrid(vtf_frame_data_t& frame_data);
void ComputeClusterAABBs(vtf_frame_data_t& frame_data);


#endif // !VTF_TASKS_AND_STEPS_HPP
