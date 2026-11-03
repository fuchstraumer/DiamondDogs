implementing VolumeTiledForwardShading;
__include VtfConfig;
__include VtfTypes;
__include VtfBindings;

import ddCommon; // indirect arg structs

// One atomic for each wave instead of one for each lane. Each lane gives its own value and receives
// its own slot.
// fix: waveOffset had no initial value, so each unbranched lane read it before WaveReadLaneFirst
// fix: prefix sum before branch, bc a wave intrinsic must run for every lane of the wave
// both of these were probably "fine", but can mess with the compiler. defensive fixes, effectively
internal uint AtomicAddWaveAggregated(RWStructuredBuffer<Atomic<uint>> counter, uint value)
{
    const uint waveSum = WaveActiveSum(value);
    const uint wavePrefix = WavePrefixSum(value);

    uint waveOffset = 0u;
    if (WaveIsFirstLane())
    {
        waveOffset = counter[0].add(waveSum);
    }
    waveOffset = WaveReadLaneFirst(waveOffset);

    return waveOffset + wavePrefix;
}

// fix: this entry point indexed clusterFlags with SV_GroupIndex instead of SV_DispatchThreadID
// fix: the output slot was tested against the thread count, instead of cluster count (discarding many)
[shader("compute")]
[NumThreads(VTF_SMALL_ENTRYPOINT_NUM_THREADS, 1, 1)]
public void VtfFindUniqueClusters(
    uint3 dispatchThreadID: SV_DispatchThreadID,
    uniform ParameterBlock<VtfFindUniqueClustersResources> resources)
{
    const uint clusterIndex = dispatchThreadID.x;
    const bool isUnique = clusterIndex < VTF_CLUSTER_COUNT && resources.ClusterFlags[clusterIndex] != 0u;

    if (VTF_USE_WAVE_OPS)
    {
        // Each lane gives 1 or 0, so the wave performs one atomic and each lane still gets its slot.
        const uint outputSlot = AtomicAddWaveAggregated(resources.UniqueClustersCounter, isUnique ? 1u : 0u);
        if (isUnique && outputSlot < VTF_CLUSTER_COUNT)
        {
            resources.UniqueClusters[outputSlot] = clusterIndex;
        }
    }
    else
    {
        if (isUnique)
        {
            const uint outputSlot = resources.UniqueClustersCounter[0].add(1u);
            if (outputSlot < VTF_CLUSTER_COUNT)
            {
                resources.UniqueClusters[outputSlot] = clusterIndex;
            }
        }
    }
}

// fix: used SV_GroupIndex when we meant SV_DispatchThreadID
[shader("compute")]
[NumThreads(VTF_SMALL_ENTRYPOINT_NUM_THREADS, 1, 1)]
public void VtfUpdateLights(
    uint3 dispatchThreadID: SV_DispatchThreadID,
    uniform ParameterBlock<VtfUpdateLightsResources> resources)
{
    // compute shader method of updating light view-space positions and directions each frame
    // becomes more efficient with larger numbers of lights
    const uint lightIndex = dispatchThreadID.x;

    const bool3 exits = bool3(
        lightIndex >= resources.Counts.NumPointLights,
        lightIndex >= resources.Counts.NumSpotLights,
        lightIndex >= resources.Counts.NumDirectionalLights);

    if (all(exits))
    {
        return;
    }

    const float4x4 view = resources.Matrices.viewMatrix;

    if (!exits.x)
    {
        resources.PointLights[lightIndex].PositionViewSpace = mul(view, resources.PointLights[lightIndex].Position);
    }

    if (!exits.y)
    {
        resources.SpotLights[lightIndex].PositionViewSpace = mul(view, resources.SpotLights[lightIndex].Position);
        // A direction has no position, so the w component must stay at zero through the transform.
        const float4 direction = float4(resources.SpotLights[lightIndex].Direction.xyz, 0.0f);
        resources.SpotLights[lightIndex].DirectionViewSpace = float4(normalize(mul(view, direction).xyz), 0.0f);
    }

    if (!exits.z)
    {
        const float4 direction = float4(resources.DirectionalLights[lightIndex].Direction.xyz, 0.0f);
        resources.DirectionalLights[lightIndex].DirectionViewSpace = float4(normalize(mul(view, direction).xyz), 0.0f);
    }
}

// fix: the debug draw switch was a `uniform bool`. WebGPU cannot hold a bool in a uniform buffer, and
// the value never changes inside one cook, so it is a permutation constant.
[shader("compute")]
[NumThreads(1, 1, 1)]
public void VtfUpdateClusterIndirectArgs(
    uniform ParameterBlock<VtfClusterIndirectArgsResources> resources)
{
    const uint uniqueClusterCount = resources.UniqueClustersCounter[0];

    resources.IndirectArgs[0].GroupCountX = uniqueClusterCount;
    resources.IndirectArgs[0].GroupCountY = 1u;
    resources.IndirectArgs[0].GroupCountZ = 1u;

    if (VTF_UPDATE_UNIQUE_CLUSTERS)
    {
        // The cluster debug pass draws one box for each unique cluster as a line list, and the box
        // takes VTF_DEBUG_CLUSTER_VERTS vertices. See VtfDebugClusters.slang, which replaces the
        // geometry stage that the GLSL used.
        resources.DebugClustersIndirectArgs[0].VertexCount = uniqueClusterCount * VTF_DEBUG_CLUSTER_VERTS;
        resources.DebugClustersIndirectArgs[0].InstanceCount = 1u;
        resources.DebugClustersIndirectArgs[0].FirstVertex = 0u;
        resources.DebugClustersIndirectArgs[0].FirstInstance = 0u;
    }
}
