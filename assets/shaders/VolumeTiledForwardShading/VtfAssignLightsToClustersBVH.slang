implementing VolumeTiledForwardShading;
__include VtfTypes;
__include VtfFunctions;

import ddCommon;

[SpecializationConstant]
const uint AssignLightsToClustersNumThreads = 32;
[SpecializationConstant]
const uint MaxLights = 2048;
[SpecializationConstant]
const uint NODE_STACK_LIMITS = 1024;

internal static const uint NumChildNodes[6] =
{
    1u,
    33u,
    1057u,
    33825u,
    1082401u,
    34636833u
};

groupshared uint gs_NodeStack[NODE_STACK_LIMITS];
groupshared Atomic<int> gs_StackPtr;
groupshared uint gs_ParentIdx;
groupshared uint gs_ClusterIndex1D;
groupshared AABB gs_ClusterAABB;
groupshared Atomic<uint> gs_PointLightCount;
groupshared Atomic<uint> gs_SpotLightCount;
groupshared uint gs_PointLightStartOffset;
groupshared uint gs_SpotLightStartOffset;
groupshared uint gs_PointLightList[MaxLights];
groupshared uint gs_SpotLightList[MaxLights];
groupshared bool gs_NonZeroLevels;

internal struct AssignLightsResourcePointers
{
    uint* uniqueClusters;
    AABB* clusterAABBs;
    BVHParams* bvhParams;
    LightCounts* lightCounts;
    PointLight* pointLights;
    SpotLight* spotLights;
    AABB* pointLightBVH;
    AABB* spotLightBVH;
    uint* pointLightIndices;
    uint* spotLightIndices;
    uint* pointLightIndexList;
    uint* spotLightIndexList;
    uint* pointLightGrid;
    uint* spotLightGrid;
    uint* pointLightIndexCounter;
    uint* spotLightIndexCounter;
}

internal void PushNode(in const uint nodeIndex)
{
    int prevPtr = gs_StackPtr.add(1);
    if (prevPtr < NODE_STACK_LIMITS)
    {
        gs_NodeStack[prevPtr] = nodeIndex;
    }
}

internal uint PopNode()
{
    int prevPtr = gs_StackPtr.sub(1);
    return (prevPtr > 0 && prevPtr < NODE_STACK_LIMITS) ? gs_NodeStack[prevPtr - 1] : 0;
}

internal uint GetFirstChild()
{
    return (gs_ParentIdx * NumThreads) + 1u;
}

internal bool IsLeafNode(const in uint childIndex, const in uint numLevels)
{
    return childIndex > (NumChildNodes[numLevels - 1u] - 1u);
}

internal uint GetLeafIndex(in const uint nodeIndex, in const uint numLevels)
{
    return nodeIndex - NumChildNodes[numLevels - 1u];
}

internal void ProcessSpotLights(
    uniform AssignLightsResourcePointers resources,
    uint childOffset)
{
    while (true)
    {
        uint childIndex = GetFirstChild() + childOffset;
        bool shouldAddLight = false;
        uint lightIndex = 0;

        if (IsLeafNode(childIndex, resources.bvhParams.SpotLightLevels))
        {
            uint leafIndex = GetLeafIndex(childIndex, resources.bvhParams.SpotLightLevels);
            if (leafIndex < resources.lightCounts.NumSpotLights)
            {
                lightIndex = resources.spotLightIndices[leafIndex];
                float4 sphere;
                sphere.xyz = resources.spotLights[lightIndex].PositionViewSpace.xyz;
                sphere.w = resources.spotLights[lightIndex].Range;

                shouldAddLight = SphereInsideAABBFast(sphere, gs_ClusterAABB.Min, gs_ClusterAABB.Max) && resources.spotLights[lightIndex].Enabled;
            }
        }
        else if (AABBIntersectAABB(
            gs_ClusterAABB.Min,
            gs_ClusterAABB.Max,
            resources.spotLightBVH[childIndex].Min,
            resources.spotLightBVH[childIndex].Max))
        {
            PushNode(childIndex);
        }

        // Use wave operations to collect lights from all lanes
        // why is this a uint4 return? 
        uint4 activeLaneMask = WaveActiveBallot(shouldAddLight);
        uint numActiveLights = countbits(activeLaneMask.x);
        
        if (numActiveLights > 0)
        {
            uint baseOffset = gs_SpotLightCount.add(numActiveLights);
            uint laneOffset = WavePrefixSum(shouldAddLight ? 1 : 0);
            
            if (shouldAddLight && (baseOffset + laneOffset) < MaxLights)
            {
                gs_SpotLightList[baseOffset + laneOffset] = lightIndex;
            }
        }

        GroupMemoryBarrierWithGroupSync();

        if (WaveIsFirstLane())
        {
            gs_ParentIdx = PopNode();
        }

        GroupMemoryBarrierWithGroupSync();

        if (gs_ParentIdx == 0u)
        {
            break;
        }
    }
}

internal void ProcessPointLights(
    uniform AssignLightsResourcePointers resources,
    uint childOffset)
{
    while (true)
    {
        uint childIndex = GetFirstChild() + childOffset;
        bool shouldAddLight = false;
        uint lightIndex = 0;

        if (IsLeafNode(childIndex, resources.bvhParams.PointLightLevels))
        {
            uint leafIndex = GetLeafIndex(childIndex, resources.bvhParams.PointLightLevels);
            if (leafIndex < resources.lightCounts.NumPointLights)
            {
                lightIndex = resources.pointLightIndices[leafIndex];
                float4 sphere;
                sphere.xyz = resources.pointLights[lightIndex].PositionViewSpace.xyz;
                sphere.w = resources.pointLights[lightIndex].Range;

                shouldAddLight = SphereInsideAABBFast(sphere, gs_ClusterAABB.Min, gs_ClusterAABB.Max) && resources.pointLights[lightIndex].Enabled;
            }
        }
        else if (AABBIntersectAABB(
            gs_ClusterAABB.Min,
            gs_ClusterAABB.Max,
            resources.pointLightBVH[childIndex].Min,
            resources.pointLightBVH[childIndex].Max))
        {
            PushNode(childIndex);
        }

        // Use wave operations to collect lights from all lanes
        uint4 activeLaneMask = WaveActiveBallot(shouldAddLight);
        uint numActiveLights = countbits(activeLaneMask.x);
        
        if (numActiveLights > 0)
        {
            uint baseOffset = gs_PointLightCount.add(numActiveLights);
            uint laneOffset = WavePrefixSum(shouldAddLight ? 1 : 0);
            
            if (shouldAddLight && (baseOffset + laneOffset) < MaxLights)
            {
                gs_PointLightList[baseOffset + laneOffset] = lightIndex;
            }
        }

        GroupMemoryBarrierWithGroupSync();

        if (WaveIsFirstLane())
        {
            gs_ParentIdx = PopNode();
        }

        GroupMemoryBarrierWithGroupSync();

        if (gs_ParentIdx == 0u)
            break;
    }
}

[shader("compute")][NumThreads(AssignLightsToClustersNumThreads, 1, 1)]
void AssignLightsToClusters(
    uint3 dispatchThreadID: SV_DispatchThreadID,
    uint3 workGroupID: SV_GroupID,
    uint localInvocationIndex: SV_GroupIndex,
    uniform AssignLightsResourcePointers resources)
{
    uint childOffset = localInvocationIndex;

    // Initialize shared memory
    if (WaveIsFirstLane())
    {
        gs_PointLightCount = 0;
        gs_SpotLightCount = 0;
        gs_StackPtr = 0;
        gs_ParentIdx = 0;
        gs_ClusterIndex1D = resources.uniqueClusters[workGroupID.x];
        gs_ClusterAABB.Min = resources.clusterAABBs[gs_ClusterIndex1D].Min;
        gs_ClusterAABB.Max = resources.clusterAABBs[gs_ClusterIndex1D].Max;
        gs_NonZeroLevels = resources.bvhParams.SpotLightLevels != 0u;
        PushNode(0u);
    }

    GroupMemoryBarrierWithGroupSync();

    // Process spot lights
    if (gs_NonZeroLevels)
    {
        ProcessSpotLights(resources, childOffset);
    }

    GroupMemoryBarrierWithGroupSync();

    // Reset for point lights
    if (WaveIsFirstLane())
    {
        gs_StackPtr = 0;
        gs_ParentIdx = 0;
        PushNode(0u);
        gs_NonZeroLevels = resources.bvhParams.PointLightLevels != 0u;
    }

    GroupMemoryBarrierWithGroupSync();

    // Process point lights
    if (gs_NonZeroLevels)
    {
        ProcessPointLights(resources, childOffset);
    }

    GroupMemoryBarrierWithGroupSync();

    // Allocate output space and write grid data
    if (WaveIsFirstLane())
    {
        uint pointLightCount = gs_PointLightCount.load();
        uint spotLightCount = gs_SpotLightCount.load();
        InterlockedAdd(resources.pointLightIndexCounter[0], pointLightCount, gs_PointLightStartOffset);
        InterlockedAdd(resources.spotLightIndexCounter[0], spotLightCount, gs_SpotLightStartOffset);
        
        // Store grid data (offset and count)
        resources.pointLightGrid[gs_ClusterIndex1D * 2] = gs_PointLightStartOffset;
        resources.pointLightGrid[gs_ClusterIndex1D * 2 + 1] = pointLightCount;
        
        resources.spotLightGrid[gs_ClusterIndex1D * 2] = gs_SpotLightStartOffset;
        resources.spotLightGrid[gs_ClusterIndex1D * 2 + 1] = spotLightCount;
    }

    GroupMemoryBarrierWithGroupSync();

    // Write light indices to output lists using wave-optimized loops
    uint pointLightCount = gs_PointLightCount.load();
    uint spotLightCount = gs_SpotLightCount.load();
    
    for (uint i = childOffset; i < pointLightCount; i += NumThreads)
    {
        resources.pointLightIndexList[gs_PointLightStartOffset + i] = gs_PointLightList[i];
    }

    for (uint i = childOffset; i < spotLightCount; i += NumThreads)
    {
        resources.spotLightIndexList[gs_SpotLightStartOffset + i] = gs_SpotLightList[i];
    }
}