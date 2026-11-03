implementing VolumeTiledForwardShading;
__include VtfConfig;
__include VtfTypes;
__include VtfFunctions;
__include VtfBindings;

import ddCommon;

// BuildBVH entrypoint and functions, constructs BVH used for light binning and culling.
//
// The tree is VTF_BVH_BRANCHING-ary. Each node holds the box of its children. Pass 0 builds the leaf
// level from the light list, and pass 1 builds one interior level from the level below it.
// 
// The Slang port replaced the shared-memory reduction with wave ops, but we forgot to account for
// inequalities between wave width and branching factor. 64 lane waves reduce two nodes into one box,
// but 16 lane waves reduce only half a node. The reduction is now segmented by the branching factor,
// and implemented to be correct at every wave width: wave ops are an optimization inside the reduction,
// rather than the structure of it.
internal groupshared float3 gs_BvhMin[VTF_BUILD_BVH_NUM_THREADS];
internal groupshared float3 gs_BvhMax[VTF_BUILD_BVH_NUM_THREADS];

// Reduces the per-thread boxes over each aligned run of VTF_BVH_BRANCHING threads. On return, the
// thread whose local index is a multiple of the branching factor holds the box of one node.
internal void ReduceBranchingSegment(
    in const uint localInvocationIndex,
    in const float3 threadMin,
    in const float3 threadMax)
{
    const uint laneCount = WaveGetLaneCount();
    // A wave may reduce a whole segment only when it fits inside one. Both operands are uniform across
    // the dispatch, so the branches below stay in uniform control flow.
    const bool waveFitsSegment = VTF_USE_WAVE_OPS && laneCount <= VTF_BVH_BRANCHING;

    float3 segmentMin = threadMin;
    float3 segmentMax = threadMax;

    if (waveFitsSegment)
    {
        segmentMin = WaveActiveMin(segmentMin);
        segmentMax = WaveActiveMax(segmentMax);
    }

    gs_BvhMin[localInvocationIndex] = segmentMin;
    gs_BvhMax[localInvocationIndex] = segmentMax;
    // something we forgot often in our initial port: we still need these barriers, because what if
    // another wave executes out-of-order and writes to the shared memory below before [current wave]
    // reads it? Then everything is thrashed, out of order, and totally invalid
    GroupMemoryBarrierWithGroupSync();
    [unroll] // should unroll relatively simply given that the stride doubles each iteration
    for (uint stride = waveFitsSegment ? laneCount : 1u; stride < VTF_BVH_BRANCHING; stride <<= 1u)
    {
        const uint partner = localInvocationIndex + stride;
        if ((localInvocationIndex % (stride * 2u)) == 0u && partner < VTF_BUILD_BVH_NUM_THREADS)
        {
            gs_BvhMin[localInvocationIndex] = min(gs_BvhMin[localInvocationIndex], gs_BvhMin[partner]);
            gs_BvhMax[localInvocationIndex] = max(gs_BvhMax[localInvocationIndex], gs_BvhMax[partner]);
        }

        GroupMemoryBarrierWithGroupSync();
    }
}

// True when this thread holds the reduced box of a node, and gives the node offset inside the level.
internal bool GetSegmentLeader(
    in const uint dispatchThreadIndex,
    in const uint localInvocationIndex,
    out uint nodeOffset)
{
    nodeOffset = dispatchThreadIndex / VTF_BVH_BRANCHING;
    return (localInvocationIndex % VTF_BVH_BRANCHING) == 0u;
}

// unfortunately need duplicated code here, as the light buffers are different structured types :'(
internal void BuildBottomPointLights(
    ParameterBlock<VtfBuildBvhResources> resources,
    in const uint dispatchThreadIndex,
    in const uint localInvocationIndex)
{
    const uint numPointLights = resources.Counts.NumPointLights;
    const uint numLevels = resources.BvhParams.PointLightLevels;

    float3 minAABB = float3(VTF_FLT_MAX, VTF_FLT_MAX, VTF_FLT_MAX);
    float3 maxAABB = float3(-VTF_FLT_MAX, -VTF_FLT_MAX, -VTF_FLT_MAX);

    if (dispatchThreadIndex < numPointLights)
    {
        const uint lightIdx = resources.PointLightIndices[dispatchThreadIndex];
        const PointLight currLight = resources.PointLights[lightIdx];
        minAABB = currLight.PositionViewSpace.xyz - currLight.Range.xxx;
        maxAABB = currLight.PositionViewSpace.xyz + currLight.Range.xxx;
    }

    ReduceBranchingSegment(localInvocationIndex, minAABB, maxAABB);

    uint nodeOffset;
    if (GetSegmentLeader(dispatchThreadIndex, localInvocationIndex, nodeOffset))
    {
        if (numLevels > 0u && numLevels <= VTF_BVH_MAX_LEVELS && nodeOffset < VTF_BVH_NUM_LEVEL_NODES[numLevels - 1u])
        {
            const uint nodeIndex = VTF_BVH_FIRST_NODE_INDEX[numLevels - 1u] + nodeOffset;
            resources.PointLightBVH[nodeIndex].Min = gs_BvhMin[localInvocationIndex];
            resources.PointLightBVH[nodeIndex].Max = gs_BvhMax[localInvocationIndex];
        }
    }
}

internal void BuildBottomSpotLights(
    ParameterBlock<VtfBuildBvhResources> resources,
    in const uint dispatchThreadIndex,
    in const uint localInvocationIndex)
{
    const uint numSpotLights = resources.Counts.NumSpotLights;
    const uint numLevels = resources.BvhParams.SpotLightLevels;

    float3 minAABB = float3(VTF_FLT_MAX, VTF_FLT_MAX, VTF_FLT_MAX);
    float3 maxAABB = float3(-VTF_FLT_MAX, -VTF_FLT_MAX, -VTF_FLT_MAX);

    if (dispatchThreadIndex < numSpotLights)
    {
        // fix: this function started with `uint lightIdx = -1;`, which is 0xFFFFFFFF, where the point
        // light twin started with 0. The value is dead now, because the index is read inside the guard.
        const uint lightIdx = resources.SpotLightIndices[dispatchThreadIndex];
        const SpotLight currLight = resources.SpotLights[lightIdx];
        // This is technically incorrect and quite conservative: we're treating the spot light as a sphere for AABB calculation.
        // Ideally we could use the cone angles to compute a tighter AABB, but until that proves to be an issue we'll just use a sphere.
        // Also, spot lights aren't as common anyways, so hopefully this won't be a big deal.
        minAABB = currLight.PositionViewSpace.xyz - currLight.Range.xxx;
        maxAABB = currLight.PositionViewSpace.xyz + currLight.Range.xxx;
    }

    ReduceBranchingSegment(localInvocationIndex, minAABB, maxAABB);

    uint nodeOffset;
    if (GetSegmentLeader(dispatchThreadIndex, localInvocationIndex, nodeOffset))
    {
        if (numLevels > 0u && numLevels <= VTF_BVH_MAX_LEVELS && nodeOffset < VTF_BVH_NUM_LEVEL_NODES[numLevels - 1u])
        {
            const uint nodeIndex = VTF_BVH_FIRST_NODE_INDEX[numLevels - 1u] + nodeOffset;
            resources.SpotLightBVH[nodeIndex].Min = gs_BvhMin[localInvocationIndex];
            resources.SpotLightBVH[nodeIndex].Max = gs_BvhMax[localInvocationIndex];
        }
    }
}

// This stage can be generic, since we just deal with the BVH/AABBs and not the actual light data. phew!
//
// fix: the guard read `childLevel < numLevels` and the body then indexed the tables at `childLevel - 1`.
// A childLevel of zero wrapped to 0xFFFFFFFF and read far outside both tables. The parent of level
// zero does not exist, so the level must be at least one.
internal void BuildUpperImpl(
    RWStructuredBuffer<AABB> lightBVH,
    in const uint dispatchThreadIndex,
    in const uint localInvocationIndex,
    in const uint childLevel,
    in const uint numLevels)
{
    const bool levelIsValid = childLevel > 0u && childLevel < numLevels && childLevel < VTF_BVH_MAX_LEVELS;

    float3 minAABB = float3(VTF_FLT_MAX, VTF_FLT_MAX, VTF_FLT_MAX);
    float3 maxAABB = float3(-VTF_FLT_MAX, -VTF_FLT_MAX, -VTF_FLT_MAX);

    if (levelIsValid && dispatchThreadIndex < VTF_BVH_NUM_LEVEL_NODES[childLevel])
    {
        const uint childIndex = VTF_BVH_FIRST_NODE_INDEX[childLevel] + dispatchThreadIndex;
        minAABB = lightBVH[childIndex].Min;
        maxAABB = lightBVH[childIndex].Max;
    }

    ReduceBranchingSegment(localInvocationIndex, minAABB, maxAABB);

    uint nodeOffset;
    if (GetSegmentLeader(dispatchThreadIndex, localInvocationIndex, nodeOffset))
    {
        if (levelIsValid && nodeOffset < VTF_BVH_NUM_LEVEL_NODES[childLevel - 1u])
        {
            const uint nodeIndex = VTF_BVH_FIRST_NODE_INDEX[childLevel - 1u] + nodeOffset;
            lightBVH[nodeIndex].Min = gs_BvhMin[localInvocationIndex];
            lightBVH[nodeIndex].Max = gs_BvhMax[localInvocationIndex];
        }
    }
}

// ChildLevel comes from the BVH parameter buffer. The GLSL pushed it as a constant and also held it in
// the uniform block, and the two could disagree. One source is enough.
[shader("compute")]
[NumThreads(VTF_BUILD_BVH_NUM_THREADS, 1, 1)]
public void VtfBuildBVH(
    uint3 dispatchThreadID: SV_DispatchThreadID,
    uint localInvocationIndex: SV_GroupIndex,
    uniform ParameterBlock<VtfBuildBvhResources> resources)
{
    if (VTF_BUILD_STAGE == 0u)
    {
        // The two calls share gs_BvhMin and gs_BvhMax, so the second must not start before the first
        // has read the last value out of them. ReduceBranchingSegment ends with a barrier, and the
        // node write that follows it reads shared memory, so one more barrier separates the pair.
        if (resources.Counts.NumPointLights > 0u)
        {
            BuildBottomPointLights(resources, dispatchThreadID.x, localInvocationIndex);
        }

        GroupMemoryBarrierWithGroupSync();

        if (resources.Counts.NumSpotLights > 0u)
        {
            BuildBottomSpotLights(resources, dispatchThreadID.x, localInvocationIndex);
        }
    }
    else
    {
        const uint childLevel = resources.BvhParams.ChildLevel;

        if (resources.Counts.NumPointLights > 0u)
        {
            BuildUpperImpl(
                resources.PointLightBVH,
                dispatchThreadID.x,
                localInvocationIndex,
                childLevel,
                resources.BvhParams.PointLightLevels);
        }
        // same deal as above: both calls share the groupshared memory and must be separated by a barrier
        GroupMemoryBarrierWithGroupSync();

        if (resources.Counts.NumSpotLights > 0u)
        {
            BuildUpperImpl(
                resources.SpotLightBVH,
                dispatchThreadID.x,
                localInvocationIndex,
                childLevel,
                resources.BvhParams.SpotLightLevels);
        }
    }
}
