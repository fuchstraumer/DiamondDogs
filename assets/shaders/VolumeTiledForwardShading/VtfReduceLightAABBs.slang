implementing VolumeTiledForwardShading;
__include VtfTypes;

import ddCommon;

[SpecializationConstant]
const uint ReductionType = 0;
[SpecializationConstant]
const uint ReduceLightsNumThreads = 512;

internal const static float VTF_FLT_MAX = 3.402823466e+38f;

groupshared float3 gs_AABBMin[ReduceLightsNumThreads];
groupshared float3 gs_AABBMax[ReduceLightsNumThreads];

struct ReduceLightAABBParams
{
    uint NumThreadGroupsX;
    uint ReductionNumElements;
}

struct ReduceLightAABBResourcePointers
{
    LightCounts* lightCounts;
    PointLight* pointLights;
    SpotLight* spotLights;
    AABB* lightAABBs;
    ReduceLightAABBParams* params;
}

/// <summary>
/// Wave-optimized reduction using hardware acceleration where possible
/// Falls back to traditional log-step reduction for the final cross-wave reduction
/// </summary>
internal void WaveOptimizedReduction(
    uniform ReduceLightAABBResourcePointers resources,
    uint localInvocationIndex,
    uint3 workGroupID)
{
    // First, use wave operations to reduce within each wave
    float3 waveMinAABB = WaveActiveMin(gs_AABBMin[localInvocationIndex]);
    float3 waveMaxAABB = WaveActiveMax(gs_AABBMax[localInvocationIndex]);
    
    // Store wave results in shared memory (one per wave)
    uint waveIndex = localInvocationIndex / WaveGetLaneCount();
    if (WaveIsFirstLane())
    {
        gs_AABBMin[waveIndex] = waveMinAABB;
        gs_AABBMax[waveIndex] = waveMaxAABB;
    }
    
    GroupMemoryBarrierWithGroupSync();
    
    // Now do traditional log-step reduction on the much smaller set of wave results
    uint numWaves = (ReduceLightsNumThreads + WaveGetLaneCount() - 1) / WaveGetLaneCount();
    uint reduceIndex = numWaves >> 1;
    
    // Only the first wave needs to do the cross-wave reduction
    if (localInvocationIndex < numWaves)
    {
        while (reduceIndex > 0)
        {
            if (localInvocationIndex < reduceIndex)
            {
                gs_AABBMin[localInvocationIndex] = min(gs_AABBMin[localInvocationIndex], gs_AABBMin[localInvocationIndex + reduceIndex]);
                gs_AABBMax[localInvocationIndex] = max(gs_AABBMax[localInvocationIndex], gs_AABBMax[localInvocationIndex + reduceIndex]);
            }
            
            GroupMemoryBarrierWithGroupSync();
            reduceIndex >>= 1;
        }
    }
    
    // Write final result
    if (localInvocationIndex == 0)
    {
        resources.lightAABBs[workGroupID.x].Min = gs_AABBMin[0];
        resources.lightAABBs[workGroupID.x].Max = gs_AABBMax[0];
    }
}

/// <summary>
/// Traditional log-step reduction (fallback for compatibility)
/// </summary>
internal void LogStepReduction(
    uniform ReduceLightAABBResourcePointers resources,
    uint localInvocationIndex,
    uint3 workGroupID)
{
    uint reduceIndex = ReduceLightsNumThreads >> 1;

    [unroll]
    while (reduceIndex > 32)
    {
        if (localInvocationIndex < reduceIndex)
        {
            gs_AABBMin[localInvocationIndex] = min(gs_AABBMin[localInvocationIndex], gs_AABBMin[localInvocationIndex + reduceIndex]);
            gs_AABBMax[localInvocationIndex] = max(gs_AABBMax[localInvocationIndex], gs_AABBMax[localInvocationIndex + reduceIndex]);
        }

        GroupMemoryBarrierWithGroupSync();
        reduceIndex >>= 1;
    }

    if (localInvocationIndex < 32)
    {
        [unroll]
        while (reduceIndex > 0)
        {
            if (ReduceLightsNumThreads >= (reduceIndex << 1))
            {
                gs_AABBMin[localInvocationIndex] = min(gs_AABBMin[localInvocationIndex], gs_AABBMin[localInvocationIndex + reduceIndex]);
                gs_AABBMax[localInvocationIndex] = max(gs_AABBMax[localInvocationIndex], gs_AABBMax[localInvocationIndex + reduceIndex]);
            }

            reduceIndex >>= 1;
        }

        if (localInvocationIndex == 0)
        {
            resources.lightAABBs[workGroupID.x].Min = gs_AABBMin[localInvocationIndex];
            resources.lightAABBs[workGroupID.x].Max = gs_AABBMax[localInvocationIndex];
        }
    }
}

/// <summary>
/// Reduce light AABBs from individual light data (ReductionType 0)
/// </summary>
internal void ReduceFromLights(
    uniform ReduceLightAABBResourcePointers resources,
    uint globalInvocationIndex,
    uint localInvocationIndex,
    uint3 workGroupID)
{
    float3 aabbMin = float3(VTF_FLT_MAX, VTF_FLT_MAX, VTF_FLT_MAX);
    float3 aabbMax = float3(-VTF_FLT_MAX, -VTF_FLT_MAX, -VTF_FLT_MAX);
    const uint IterationStride = ReduceLightsNumThreads * resources.params.NumThreadGroupsX;

    // Process point lights
    for (uint i = globalInvocationIndex; i < resources.lightCounts.NumPointLights; i += IterationStride)
    {
        float3 lightPos = resources.pointLights[i].PositionViewSpace.xyz;
        float range = resources.pointLights[i].Range;
        aabbMin = min(aabbMin, lightPos - range.xxx);
        aabbMax = max(aabbMax, lightPos + range.xxx);
    }

    // Process spot lights
    for (uint i = globalInvocationIndex; i < resources.lightCounts.NumSpotLights; i += IterationStride)
    {
        float3 lightPos = resources.spotLights[i].PositionViewSpace.xyz;
        float range = resources.spotLights[i].Range;
        aabbMin = min(aabbMin, lightPos - range.xxx);
        aabbMax = max(aabbMax, lightPos + range.xxx);
    }

    gs_AABBMin[localInvocationIndex] = aabbMin;
    gs_AABBMax[localInvocationIndex] = aabbMax;

    GroupMemoryBarrierWithGroupSync();

    WaveOptimizedReduction(resources, localInvocationIndex, workGroupID);
}

/// <summary>
/// Reduce AABBs from previously computed AABB data (ReductionType 1)
/// </summary>
internal void ReduceFromAABBs(
    uniform ReduceLightAABBResourcePointers resources,
    uint localInvocationIndex,
    uint3 workGroupID)
{
    float3 aabbMin = float3(VTF_FLT_MAX, VTF_FLT_MAX, VTF_FLT_MAX);
    float3 aabbMax = float3(-VTF_FLT_MAX, -VTF_FLT_MAX, -VTF_FLT_MAX);
    const uint IterationStride = ReduceLightsNumThreads * resources.params.NumThreadGroupsX;

    for (uint i = localInvocationIndex; i < resources.params.ReductionNumElements; i += IterationStride)
    {
        aabbMin = min(aabbMin, resources.lightAABBs[i].Min);
        aabbMax = max(aabbMax, resources.lightAABBs[i].Max);
    }

    gs_AABBMin[localInvocationIndex] = aabbMin;
    gs_AABBMax[localInvocationIndex] = aabbMax;

    GroupMemoryBarrierWithGroupSync();

    WaveOptimizedReduction(resources, localInvocationIndex, workGroupID);
}

[shader("compute")][NumThreads(ReduceLightsNumThreads, 1, 1)]
void ReduceLightAABBs(
    uint3 dispatchThreadID : SV_DispatchThreadID,
    uint3 workGroupID : SV_GroupID,
    uint localInvocationIndex : SV_GroupIndex,
    uniform ReduceLightAABBResourcePointers resources)
{
    switch (ReductionType)
    {
        case 0:
            ReduceFromLights(resources, dispatchThreadID.x, localInvocationIndex, workGroupID);
            break;
        case 1:
            ReduceFromAABBs(resources, localInvocationIndex, workGroupID);
            break;
    }
}