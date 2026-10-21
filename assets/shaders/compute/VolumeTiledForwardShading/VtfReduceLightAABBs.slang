implementing VolumeTiledForwardShading;
__include VtfConfig;
__include VtfTypes;
__include VtfBindings;

import ddCommon;

// Two passes reduce every light into one bounding box. Pass 0 gives one box for each thread group.
// Pass 1 reduces those boxes into the box at index 0, which the Morton stage then reads.

// The shared-memory path is the original algorithm from ReduceLightsAABB.comp. The wave path replaced
// it during the Slang port and left it unreachable. Both legs are live again, and VTF_USE_WAVE_OPS
// selects one.
groupshared float3 gs_AABBMin[VTF_REDUCE_LIGHTS_NUM_THREADS];
groupshared float3 gs_AABBMax[VTF_REDUCE_LIGHTS_NUM_THREADS];

internal void WriteGroupResult(
    ParameterBlock<VtfReduceLightAABBsResources> resources,
    in const uint3 workGroupID)
{
    resources.LightAABBs[workGroupID.x].Min = gs_AABBMin[0];
    resources.LightAABBs[workGroupID.x].Max = gs_AABBMax[0];
}

// Wave path. Each wave reduces its own lanes with one instruction, then the group reduces the much
// smaller set of per-wave results through shared memory. (instead of every wave going through LDS)
// We use WaveGetLaneCount() here since the wave size permutation var is intended for compile-time
// of things like array sizes or precise loop steps etc etc 
internal void WaveOptimizedReduction(
    ParameterBlock<VtfReduceLightAABBsResources> resources,
    in const uint localInvocationIndex,
    in const uint3 workGroupID)
{
    const float3 waveMinAABB = WaveActiveMin(gs_AABBMin[localInvocationIndex]);
    const float3 waveMaxAABB = WaveActiveMax(gs_AABBMax[localInvocationIndex]);

    // fix: the wave results were written straight back into the array that the two lines above read.
    // Wave 1 could overwrite slot 1 before lane 1 of wave 0 had read it. This barrier separates the
    // read of the array from the write to it.
    GroupMemoryBarrierWithGroupSync();

    const uint laneCount = WaveGetLaneCount();
    const uint waveIndex = localInvocationIndex / laneCount;

    if (WaveIsFirstLane())
    {
        gs_AABBMin[waveIndex] = waveMinAABB;
        gs_AABBMax[waveIndex] = waveMaxAABB;
    }

    GroupMemoryBarrierWithGroupSync();

    const uint numWaves = (VTF_REDUCE_LIGHTS_NUM_THREADS + laneCount - 1u) / laneCount;
    // fix: WGSL doesn't allow barriers inside conditionals that not all threads in a group execute,
    // so we removed that conditional and instead gate on the inner loop, where we actually write
    // fix: the stride only handled powers of 2. this would break at wavesize=64
    for (uint stride = 1u; stride < numWaves; stride <<= 1u)
    {
        const uint partner = localInvocationIndex + stride;
        if ((localInvocationIndex % (stride * 2u)) == 0u && partner < numWaves)
        {
            gs_AABBMin[localInvocationIndex] = min(gs_AABBMin[localInvocationIndex], gs_AABBMin[partner]);
            gs_AABBMax[localInvocationIndex] = max(gs_AABBMax[localInvocationIndex], gs_AABBMax[partner]);
        }

        GroupMemoryBarrierWithGroupSync();
    }

    if (localInvocationIndex == 0u)
    {
        WriteGroupResult(resources, workGroupID);
    }
}

// Shared-memory path, and the control arm. It uses no wave intrinsic, so it runs on a device that
// reports no subgroup support.
//
// fix: the original halved the stride to 32 and then ran the last five steps with no barrier, because
// a wave of 32 lanes advances in step. That assumption is not portable, and WGSL has no way to state
// it. Every step now ends with a barrier. VTF_REDUCE_LIGHTS_NUM_THREADS is a power of two, so the
// halving form loses no element.
internal void LogStepReduction(
    ParameterBlock<VtfReduceLightAABBsResources> resources,
    in const uint localInvocationIndex,
    in const uint3 workGroupID)
{
    for (uint reduceIndex = VTF_REDUCE_LIGHTS_NUM_THREADS >> 1u; reduceIndex > 0u; reduceIndex >>= 1u)
    {
        if (localInvocationIndex < reduceIndex)
        {
            const uint partner = localInvocationIndex + reduceIndex;
            gs_AABBMin[localInvocationIndex] = min(gs_AABBMin[localInvocationIndex], gs_AABBMin[partner]);
            gs_AABBMax[localInvocationIndex] = max(gs_AABBMax[localInvocationIndex], gs_AABBMax[partner]);
        }

        GroupMemoryBarrierWithGroupSync();
    }

    if (localInvocationIndex == 0u)
    {
        WriteGroupResult(resources, workGroupID);
    }
}

internal void ReduceSharedState(
    ParameterBlock<VtfReduceLightAABBsResources> resources,
    in const uint localInvocationIndex,
    in const uint3 workGroupID)
{
    if (VTF_USE_WAVE_OPS)
    {
        WaveOptimizedReduction(resources, localInvocationIndex, workGroupID);
    }
    else
    {
        LogStepReduction(resources, localInvocationIndex, workGroupID);
    }
}

// Pass 0. Every thread group walks the whole light list with a grid stride.
internal void ReduceFromLights(
    ParameterBlock<VtfReduceLightAABBsResources> resources,
    in const uint globalInvocationIndex,
    in const uint localInvocationIndex,
    in const uint3 workGroupID)
{
    // first step: load data into LDS
    float3 aabbMin = float3(VTF_FLT_MAX, VTF_FLT_MAX, VTF_FLT_MAX);
    float3 aabbMax = float3(-VTF_FLT_MAX, -VTF_FLT_MAX, -VTF_FLT_MAX);

    const uint iterationStride = VTF_REDUCE_LIGHTS_NUM_THREADS * resources.Params.NumThreadGroupsX;
    for (uint i = globalInvocationIndex; i < resources.Counts.NumPointLights; i += iterationStride)
    {
        const float3 lightPos = resources.PointLights[i].PositionViewSpace.xyz;
        const float range = resources.PointLights[i].Range;
        aabbMin = min(aabbMin, lightPos - range.xxx);
        aabbMax = max(aabbMax, lightPos + range.xxx);
    }

    for (uint i = globalInvocationIndex; i < resources.Counts.NumSpotLights; i += iterationStride)
    {
        const float3 lightPos = resources.SpotLights[i].PositionViewSpace.xyz;
        const float range = resources.SpotLights[i].Range;
        aabbMin = min(aabbMin, lightPos - range.xxx);
        // The GLSL used min here, so the upper corner of the box collapsed towards the first spot
        // light instead of growing to hold each one.
        aabbMax = max(aabbMax, lightPos + range.xxx);
    }

    gs_AABBMin[localInvocationIndex] = aabbMin;
    gs_AABBMax[localInvocationIndex] = aabbMax;

    GroupMemoryBarrierWithGroupSync();

    ReduceSharedState(resources, localInvocationIndex, workGroupID);
}

// Pass 1. One thread group reduces the boxes that pass 0 wrote.
internal void ReduceFromAABBs(
    ParameterBlock<VtfReduceLightAABBsResources> resources,
    in const uint localInvocationIndex,
    in const uint3 workGroupID)
{
    float3 aabbMin = float3(VTF_FLT_MAX, VTF_FLT_MAX, VTF_FLT_MAX);
    float3 aabbMax = float3(-VTF_FLT_MAX, -VTF_FLT_MAX, -VTF_FLT_MAX);
    // fix: stride was prev only multiplied by NumThreadGroupsX, which is only group count in pass 0
    // pass 1 runs as a single group, so the stride needs to be varied per pass with a permutation param
    const uint iterationStride = VTF_REDUCE_LIGHTS_NUM_THREADS;

    for (uint i = localInvocationIndex; i < resources.Params.ReductionNumElements; i += iterationStride)
    {
        aabbMin = min(aabbMin, resources.LightAABBs[i].Min);
        aabbMax = max(aabbMax, resources.LightAABBs[i].Max);
    }

    gs_AABBMin[localInvocationIndex] = aabbMin;
    gs_AABBMax[localInvocationIndex] = aabbMax;

    GroupMemoryBarrierWithGroupSync();

    ReduceSharedState(resources, localInvocationIndex, workGroupID);
}

[shader("compute")]
[NumThreads(VTF_REDUCE_LIGHTS_NUM_THREADS, 1, 1)]
public void VtfReduceLightAABBs(
    uint3 dispatchThreadID: SV_DispatchThreadID,
    uint3 workGroupID: SV_GroupID,
    uint localInvocationIndex: SV_GroupIndex,
    uniform ParameterBlock<VtfReduceLightAABBsResources> resources)
{
    // VTF_REDUCTION_TYPE is a link-time constant, used to select which reduction path actually
    // becomes source code for a shader (vs uniform constants, previously)
    if (VTF_REDUCTION_TYPE == 0u)
    {
        ReduceFromLights(resources, dispatchThreadID.x, localInvocationIndex, workGroupID);
    }
    else
    {
        ReduceFromAABBs(resources, localInvocationIndex, workGroupID);
    }
}
