implementing VolumeTiledForwardShading;
__include VtfConfig;
__include VtfTypes;
__include VtfFunctions;
__include VtfBindings;

import ddCommon;

// One thread group walks the light BVH for one cluster and writes the lights that touch it.
//
// The traversal keeps a stack of nodes in shared memory. Each thread visits one child of the current
// node, tests it, and either records a light or pushes an interior node for later.

internal groupshared uint gs_NodeStack[VTF_NODE_STACK_LIMIT];
internal groupshared Atomic<int> gs_StackPtr;
internal groupshared uint gs_ParentIdx;
internal groupshared uint gs_ClusterIndex1D;
internal groupshared AABB gs_ClusterAABB;
internal groupshared Atomic<uint> gs_PointLightCount;
internal groupshared Atomic<uint> gs_SpotLightCount;
internal groupshared uint gs_PointLightStartOffset;
internal groupshared uint gs_SpotLightStartOffset;
internal groupshared uint gs_PointLightList[VTF_MAX_LIGHTS_PER_CLUSTER];
internal groupshared uint gs_SpotLightList[VTF_MAX_LIGHTS_PER_CLUSTER];

// fix: the stack pointer was an Atomic<int> that PopNode decremented without a guard. An empty stack
// took the pointer to -1, and the next PushNode then wrote gs_NodeStack[-1]. PushNode also kept the
// increment when it dropped a node, so the pointer climbed past the limit and never came back, and
// PopNode then returned 0 for every later call. Zero is also the value that ends the traversal, so a
// full stack ended the walk early and reported nothing.
//
// PushNode now returns the slot when it drops a node. PopNode never takes the pointer below zero.

internal void PushNode(in const uint nodeIndex)
{
    const int slot = gs_StackPtr.add(1);
    if (slot >= 0 && slot < int(VTF_NODE_STACK_LIMIT))
    {
        gs_NodeStack[slot] = nodeIndex;
    }
    else
    {
        // The stack is full. Give the slot back, so the pointer stays inside its range.
        gs_StackPtr.sub(1);
    }
}

// Only thread 0 calls this, and only after a barrier, so it needs no atomic.
internal uint PopNode()
{
    const int ptr = gs_StackPtr.load();
    if (ptr <= 0)
    {
        gs_StackPtr.store(0);
        return 0u;
    }

    const int slot = ptr - 1;
    gs_StackPtr.store(slot);
    return gs_NodeStack[slot];
}

internal uint GetFirstChild(in const uint parentIndex)
{
    // fix: this multiplied by the thread count, which was also the wave width, which was also the
    // branching factor, because all three were 32. Only the branching factor belongs here.
    return (parentIndex * VTF_BVH_BRANCHING) + 1u;
}

// The leaf level starts at the first node index of level numLevels. The tables hold VTF_BVH_MAX_LEVELS
// entries, so a caller must clamp the level before it reads them. ClampBvhLevels does that once.
internal uint ClampBvhLevels(in const uint numLevels)
{
    return min(numLevels, VTF_BVH_MAX_LEVELS - 1u);
}

internal bool IsLeafNode(in const uint childIndex, in const uint numLevels)
{
    return childIndex >= VTF_BVH_FIRST_NODE_INDEX[numLevels];
}

internal uint GetLeafIndex(in const uint nodeIndex, in const uint numLevels)
{
    return nodeIndex - VTF_BVH_FIRST_NODE_INDEX[numLevels];
}

// Counts the lanes that the ballot set.
//
// fix: the spot light path summed all four components of the mask and carried a comment about wider
// waves. The point light path summed only the x component, so a device of more than 32 lanes lost
// every light that lanes 32 and above found. One function now serves both paths, and
// VTF_BALLOT_WORD_COUNT gives the number of 32-lane words that the cooked width needs.
internal uint CountBallotLanes(in const uint4 ballot)
{
    uint total = 0u;
    [unroll]
    for (uint word = 0u; word < VTF_BALLOT_WORD_COUNT; ++word)
    {
        total += countbits(ballot[word]);
    }
    return total;
}

// Reserves a slot in a per-cluster list for each lane that found a light. It returns
// VTF_MAX_LIGHTS_PER_CLUSTER when this lane has nothing to write, or when the list is full.
//
// The counter travels as an inout uint that the caller loads and stores, because a groupshared
// Atomic cannot cross a function boundary. The atomic work happens here, and the caller applies the
// result to its own list.
//
// fix: the wave arm ran `gs_*LightCount.add(numActiveLights)` on every lane. The value is the same
// for each lane of the wave, so the counter advanced by the wave width times the real count, and each
// lane received a different base offset. One lane performs the atomic now, and WaveReadLaneFirst
// gives the result to the rest. The pattern already existed in VtfSmallEntrypoints.slang as
// AtomicAddWaveAggregated, and it was never called.
internal uint ReserveWaveSlots(in const bool shouldAddLight, in const uint baseOffset)
{
    const uint laneOffset = WavePrefixSum(shouldAddLight ? 1u : 0u);
    const uint slot = baseOffset + laneOffset;
    return (shouldAddLight && slot < VTF_MAX_LIGHTS_PER_CLUSTER) ? slot : VTF_MAX_LIGHTS_PER_CLUSTER;
}

// Appends to the point light list. The spot light twin follows it.
// The two functions read their groupshared state directly, because Slang cannot pass a groupshared
// array or a groupshared Atomic as a parameter.
internal void AppendPointLight(in const bool shouldAddLight, in const uint lightIndex)
{
    if (VTF_USE_WAVE_OPS)
    {
        const uint numActiveLights = CountBallotLanes(WaveActiveBallot(shouldAddLight));
        if (numActiveLights == 0u)
        {
            return;
        }

        uint baseOffset = 0u;
        if (WaveIsFirstLane())
        {
            baseOffset = gs_PointLightCount.add(numActiveLights);
        }
        baseOffset = WaveReadLaneFirst(baseOffset);

        const uint slot = ReserveWaveSlots(shouldAddLight, baseOffset);
        if (slot < VTF_MAX_LIGHTS_PER_CLUSTER)
        {
            gs_PointLightList[slot] = lightIndex;
        }
    }
    else
    {
        // The shared-memory arm, which is the original GLSL. One atomic for each light.
        if (shouldAddLight)
        {
            const uint slot = gs_PointLightCount.add(1u);
            if (slot < VTF_MAX_LIGHTS_PER_CLUSTER)
            {
                gs_PointLightList[slot] = lightIndex;
            }
        }
    }
}

internal void AppendSpotLight(in const bool shouldAddLight, in const uint lightIndex)
{
    if (VTF_USE_WAVE_OPS)
    {
        const uint numActiveLights = CountBallotLanes(WaveActiveBallot(shouldAddLight));
        if (numActiveLights == 0u)
        {
            return;
        }

        uint baseOffset = 0u;
        if (WaveIsFirstLane())
        {
            baseOffset = gs_SpotLightCount.add(numActiveLights);
        }
        baseOffset = WaveReadLaneFirst(baseOffset);

        const uint slot = ReserveWaveSlots(shouldAddLight, baseOffset);
        if (slot < VTF_MAX_LIGHTS_PER_CLUSTER)
        {
            gs_SpotLightList[slot] = lightIndex;
        }
    }
    else
    {
        if (shouldAddLight)
        {
            const uint slot = gs_SpotLightCount.add(1u);
            if (slot < VTF_MAX_LIGHTS_PER_CLUSTER)
            {
                gs_SpotLightList[slot] = lightIndex;
            }
        }
    }
}

// Walks the spot light BVH. The loop ends when the stack gives back the root sentinel.
internal void ProcessSpotLights(
    ParameterBlock<VtfAssignLightsResources> resources,
    in const uint localInvocationIndex)
{
    const uint numLevels = ClampBvhLevels(resources.BvhParams.SpotLightLevels);
    const uint threadCount = VTF_ASSIGN_LIGHTS_NUM_THREADS;

    while (true)
    {
        // A thread group that holds fewer threads than the branching factor visits several children in
        // turn. The default group holds exactly VTF_BVH_BRANCHING threads, so this runs once.
        for (uint childOffset = localInvocationIndex; childOffset < VTF_BVH_BRANCHING; childOffset += threadCount)
        {
            const uint childIndex = GetFirstChild(gs_ParentIdx) + childOffset;

            bool shouldAddLight = false;
            uint lightIndex = 0u;

            if (IsLeafNode(childIndex, numLevels))
            {
                const uint leafIndex = GetLeafIndex(childIndex, numLevels);
                if (leafIndex < resources.Counts.NumSpotLights)
                {
                    lightIndex = resources.SpotLightIndices[leafIndex];
                    const SpotLight light = resources.SpotLights[lightIndex];
                    const float4 sphere = float4(light.PositionViewSpace.xyz, light.Range);

                    shouldAddLight = SphereInsideAABBFast(sphere, gs_ClusterAABB.Min, gs_ClusterAABB.Max)
                        && light.Enabled != 0u;
                }
            }
            else if (AABBIntersectAABB(
                gs_ClusterAABB.Min,
                gs_ClusterAABB.Max,
                resources.SpotLightBVH[childIndex].Min,
                resources.SpotLightBVH[childIndex].Max))
            {
                PushNode(childIndex);
            }

            AppendSpotLight(shouldAddLight, lightIndex);
        }

        GroupMemoryBarrierWithGroupSync();

        // fix: this used WaveIsFirstLane, which is the first lane of each wave. A group of more than
        // one wave therefore popped once for each wave and lost nodes. The pop belongs to the group.
        if (localInvocationIndex == 0u)
        {
            gs_ParentIdx = PopNode();
        }

        // fix: this was a bare GroupMemoryBarrier, which orders memory and does not wait for the other
        // threads. The point light twin used the full barrier. Every thread reads gs_ParentIdx on the
        // next line, so it must wait.
        GroupMemoryBarrierWithGroupSync();

        if (gs_ParentIdx == 0u)
        {
            break;
        }
    }
}

// Walks the point light BVH. This is the twin of ProcessSpotLights, and the two must stay the same.
internal void ProcessPointLights(
    ParameterBlock<VtfAssignLightsResources> resources,
    in const uint localInvocationIndex)
{
    const uint numLevels = ClampBvhLevels(resources.BvhParams.PointLightLevels);
    const uint threadCount = VTF_ASSIGN_LIGHTS_NUM_THREADS;

    while (true)
    {
        for (uint childOffset = localInvocationIndex; childOffset < VTF_BVH_BRANCHING; childOffset += threadCount)
        {
            const uint childIndex = GetFirstChild(gs_ParentIdx) + childOffset;

            bool shouldAddLight = false;
            uint lightIndex = 0u;

            if (IsLeafNode(childIndex, numLevels))
            {
                const uint leafIndex = GetLeafIndex(childIndex, numLevels);
                if (leafIndex < resources.Counts.NumPointLights)
                {
                    lightIndex = resources.PointLightIndices[leafIndex];
                    const PointLight light = resources.PointLights[lightIndex];
                    const float4 sphere = float4(light.PositionViewSpace.xyz, light.Range);

                    shouldAddLight = SphereInsideAABBFast(sphere, gs_ClusterAABB.Min, gs_ClusterAABB.Max)
                        && light.Enabled != 0u;
                }
            }
            else if (AABBIntersectAABB(
                gs_ClusterAABB.Min,
                gs_ClusterAABB.Max,
                resources.PointLightBVH[childIndex].Min,
                resources.PointLightBVH[childIndex].Max))
            {
                PushNode(childIndex);
            }

            AppendPointLight(shouldAddLight, lightIndex);
        }

        GroupMemoryBarrierWithGroupSync();

        if (localInvocationIndex == 0u)
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

// Resets the stack and the counters for one traversal.
internal void ResetTraversal(in const uint localInvocationIndex)
{
    if (localInvocationIndex == 0u)
    {
        gs_StackPtr.store(0);
        gs_ParentIdx = 0u;
        PushNode(0u);
    }

    GroupMemoryBarrierWithGroupSync();
}

// fix: every one of the five WaveIsFirstLane calls in this entry point stood for "thread 0 of the
// group". The two are the same number only while the group holds one wave, which held while the thread
// count, the wave width and the branching factor were all 32. Each one is now an explicit test on the
// group index.
[shader("compute")]
[NumThreads(VTF_ASSIGN_LIGHTS_NUM_THREADS, 1, 1)]
public void VtfAssignLightsToClusters(
    uint3 workGroupID: SV_GroupID,
    uint localInvocationIndex: SV_GroupIndex,
    uniform ParameterBlock<VtfAssignLightsResources> resources)
{
    if (localInvocationIndex == 0u)
    {
        gs_PointLightCount.store(0u);
        gs_SpotLightCount.store(0u);
        gs_PointLightStartOffset = 0u;
        gs_SpotLightStartOffset = 0u;
        gs_ClusterIndex1D = resources.UniqueClusters[workGroupID.x];
        gs_ClusterAABB.Min = resources.ClusterAABBs[gs_ClusterIndex1D].Min;
        gs_ClusterAABB.Max = resources.ClusterAABBs[gs_ClusterIndex1D].Max;
    }

    GroupMemoryBarrierWithGroupSync();

    // A tree of zero levels holds no node, so the traversal has nothing to walk. The test reads a
    // uniform buffer, so every thread takes the same arm and the barriers inside stay uniform.
    if (resources.BvhParams.SpotLightLevels > 0u)
    {
        ResetTraversal(localInvocationIndex);
        ProcessSpotLights(resources, localInvocationIndex);
    }

    GroupMemoryBarrierWithGroupSync();

    if (resources.BvhParams.PointLightLevels > 0u)
    {
        ResetTraversal(localInvocationIndex);
        ProcessPointLights(resources, localInvocationIndex);
    }

    GroupMemoryBarrierWithGroupSync();

    // Reserve the output range for this cluster, then publish the offsets and counts.
    if (localInvocationIndex == 0u)
    {
        // The counts cannot exceed the list capacity, because the append drops anything past it. The
        // clamp keeps the published count in step with what the list actually holds.
        const uint pointLightCount = min(gs_PointLightCount.load(), VTF_MAX_LIGHTS_PER_CLUSTER);
        const uint spotLightCount = min(gs_SpotLightCount.load(), VTF_MAX_LIGHTS_PER_CLUSTER);

        gs_PointLightStartOffset = resources.PointLightIndexCounter[0].add(pointLightCount);
        gs_SpotLightStartOffset = resources.SpotLightIndexCounter[0].add(spotLightCount);

        resources.PointLightGrid[gs_ClusterIndex1D] = uint2(gs_PointLightStartOffset, pointLightCount);
        resources.SpotLightGrid[gs_ClusterIndex1D] = uint2(gs_SpotLightStartOffset, spotLightCount);
    }

    GroupMemoryBarrierWithGroupSync();

    const uint pointLightCount = min(gs_PointLightCount.load(), VTF_MAX_LIGHTS_PER_CLUSTER);
    const uint spotLightCount = min(gs_SpotLightCount.load(), VTF_MAX_LIGHTS_PER_CLUSTER);

    for (uint i = localInvocationIndex; i < pointLightCount; i += VTF_ASSIGN_LIGHTS_NUM_THREADS)
    {
        resources.PointLightIndexList[gs_PointLightStartOffset + i] = gs_PointLightList[i];
    }

    for (uint i = localInvocationIndex; i < spotLightCount; i += VTF_ASSIGN_LIGHTS_NUM_THREADS)
    {
        resources.SpotLightIndexList[gs_SpotLightStartOffset + i] = gs_SpotLightList[i];
    }
}
